#include "ecg_measure.h"

#include "ecg_rpeak.h"

#include <string.h>

/**
 * 数据流：
 * 1 kHz 降采样滤波样本及连续时间戳
 * -> 64 点环形历史
 * -> 等待确认窗口的 29 点延迟提交
 * -> 相邻 ecg_rpeak 峰事件补算区间
 * -> 最小值、最大值、峰峰值
 *
 * ProcessSample 和 OnRPeak 都运行在主循环/任务上下文且必须串行调用；
 * 没有 ISR 写入，不提供临界区保护。输入流发生索引跳变时丢弃当前搏动，
 * 且 latest 无效，等待下一次 OnRPeak 从新峰建立基准。
 *
 * 不变量：committed_index 不超过 29 点延迟之外的最近已提交点；
 * 首峰只建立下一搏，下一峰才完成上一段区间；峰位置本身和峰后已到达样本
 * 归入下一搏，不重复计入上一搏。
 */

#define ECG_MEASURE_COMMIT_LAG_SAMPLES ECG_RPEAK_MAX_CONFIRM_DELAY_SAMPLES

_Static_assert((ECG_MEASURE_HISTORY_LENGTH & ECG_MEASURE_HISTORY_MASK) == 0U,
               "ECG_MEASURE_HISTORY_LENGTH must be a power of two");
_Static_assert(ECG_MEASURE_HISTORY_LENGTH >
                   (ECG_MEASURE_COMMIT_LAG_SAMPLES + 1U),
               "history must cover the R peak confirmation delay");

static void IncludeSample(ECG_MeasureState *state, float32_t sample_mv)
{
  if (state->beat_has_samples == 0U)
  {
    state->beat_min_mv = sample_mv;
    state->beat_max_mv = sample_mv;
    state->beat_has_samples = 1U;
    return;
  }

  if (sample_mv < state->beat_min_mv)
  {
    state->beat_min_mv = sample_mv;
  }
  if (sample_mv > state->beat_max_mv)
  {
    state->beat_max_mv = sample_mv;
  }
}

static void IncludeRange(ECG_MeasureState *state,
                         uint32_t first_index,
                         uint32_t last_index)
{
  /* Unsigned forward difference is valid across 32-bit timestamp wrap.
   * Reject an empty/reversed interval before indexing the ring. */
  const uint32_t forward_samples = last_index - first_index;
  if (forward_samples >= ECG_MEASURE_HISTORY_LENGTH)
  {
    return;
  }

  const uint32_t count = forward_samples + 1U;
  for (uint32_t offset = 0U; offset < count; ++offset)
  {
    const uint32_t index = first_index + offset;
    IncludeSample(state, state->history_mv[index & ECG_MEASURE_HISTORY_MASK]);
  }
}

void ECG_Measure_Init(ECG_MeasureState *state)
{
  if (state == NULL)
  {
    return;
  }

  memset(state, 0, sizeof(*state));
}

void ECG_Measure_ProcessSample(ECG_MeasureState *state,
                               uint32_t sample_index,
                               float32_t sample_mv)
{
  if (state == NULL)
  {
    return;
  }

  /* A gap means the downstream stream restarted; drop stale beat state. */
  if ((state->has_sample != 0U) &&
      ((uint32_t)(sample_index - state->last_index) != 1U))
  {
    state->beat_active = 0U;
    state->beat_has_samples = 0U;
    state->latest.valid = 0U;
  }

  state->history_mv[sample_index & ECG_MEASURE_HISTORY_MASK] = sample_mv;
  state->last_index = sample_index;
  state->has_sample = 1U;

  if (state->beat_active == 0U)
  {
    return;
  }

  /* Only samples that can no longer precede a pending R peak are committed. */
  if ((uint32_t)(sample_index - state->committed_index) >
      ECG_MEASURE_COMMIT_LAG_SAMPLES)
  {
    const uint32_t commit_index = sample_index - ECG_MEASURE_COMMIT_LAG_SAMPLES;
    IncludeSample(state,
                  state->history_mv[commit_index & ECG_MEASURE_HISTORY_MASK]);
    state->committed_index = commit_index;
  }
}

uint8_t ECG_Measure_OnRPeak(ECG_MeasureState *state,
                            uint32_t peak_index,
                            ECG_BeatMeasurement *measurement)
{
  uint8_t completed = 0U;

  if (measurement != NULL)
  {
    memset(measurement, 0, sizeof(*measurement));
  }

  if ((state == NULL) || (measurement == NULL) || (state->has_sample == 0U))
  {
    return 0U;
  }

  /* The peak must still be inside the sample history. */
  if ((uint32_t)(state->last_index - peak_index) >
      ECG_MEASURE_COMMIT_LAG_SAMPLES)
  {
    return 0U;
  }

  if (state->beat_active != 0U)
  {
    IncludeRange(state, state->committed_index + 1U, peak_index);

    if (state->beat_has_samples != 0U)
    {
      measurement->min_mv = state->beat_min_mv;
      measurement->max_mv = state->beat_max_mv;
      measurement->peak_to_peak_mv =
          state->beat_max_mv - state->beat_min_mv;
      measurement->valid = 1U;
      state->latest = *measurement;
      completed = 1U;
    }
  }

  state->beat_active = 1U;
  state->beat_start_index = peak_index;
  state->beat_has_samples = 0U;
  state->committed_index = state->last_index;

  /* Samples already received after the peak belong to the next beat. */
  IncludeRange(state, peak_index + 1U, state->last_index);

  return completed;
}

const ECG_BeatMeasurement *ECG_Measure_GetLatest(const ECG_MeasureState *state)
{
  if (state == NULL)
  {
    return NULL;
  }

  return &state->latest;
}
