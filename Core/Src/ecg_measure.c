#include "ecg_measure.h"

#include "ecg_rpeak.h"

#include <string.h>

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
  const uint32_t count = last_index - first_index + 1U;

  if (count > ECG_MEASURE_HISTORY_LENGTH)
  {
    return;
  }

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
