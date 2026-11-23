#include "ecg_rpeak.h"

#include <string.h>

/**
 * 数据流：
 * 1 kHz ECG 样本
 * -> 低阈值回差及不应期检查
 * -> 30 点局部最大值确认窗口
 * -> ECG_RPeakEvent（窗口内最大值及连续索引）
 * -> 心率模块和每搏波形模块
 *
 * Init、阈值调整及逐点处理都运行于主循环/任务上下文，不在 DMA ISR
 * 中调用。各状态字段只由 detector 的串行调用者读写，DMA 块边界不参与
 * 状态流转或重置。
 *
 * CONFIRM 中窗口计数从 1 开始，并严格递增到 30；window_max_* 始终保存
 * 已观察到的最大值及其时间戳。平顶窗口保留第一个最大值对应的索引。
 * 无符号 32 位索引差值用于不应期和计数器回绕，后者按模 2^32 处理。
 */

static const ECG_RPeakThresholdConfig g_default_threshold_config = {
  ECG_RPEAK_DEFAULT_THRESHOLD_MV,
  ECG_RPEAK_THRESHOLD_MIN_MV,
  ECG_RPEAK_THRESHOLD_MAX_MV
};

/** 统一阈值钳位逻辑；主循环/任务上下文调用，不访问共享状态。 */
static float32_t ClampThreshold(float32_t threshold_mv,
                                float32_t min_threshold_mv,
                                float32_t max_threshold_mv)
{
  if (threshold_mv < min_threshold_mv)
  {
    return min_threshold_mv;
  }
  if (threshold_mv > max_threshold_mv)
  {
    return max_threshold_mv;
  }
  return threshold_mv;
}

void ECG_RPeak_Init(ECG_RPeakDetector *detector)
{
  if (detector == NULL)
  {
    return;
  }

  memset(detector, 0, sizeof(*detector));
  detector->threshold_mv = g_default_threshold_config.current_threshold_mv;
  detector->low_threshold_mv = detector->threshold_mv - ECG_RPEAK_HYSTERESIS_MV;
  if (detector->low_threshold_mv < 0.0f)
  {
    detector->low_threshold_mv = 0.0f;
  }
  detector->state = ECG_RPEAK_STATE_ARMED;
  detector->low_seen = 1U;
}

float32_t ECG_RPeak_SetThreshold(ECG_RPeakDetector *detector, float32_t threshold_mv)
{
  if (detector == NULL)
  {
    return 0.0f;
  }

  detector->threshold_mv = ClampThreshold(threshold_mv,
                                          ECG_RPEAK_THRESHOLD_MIN_MV,
                                          ECG_RPEAK_THRESHOLD_MAX_MV);
  detector->low_threshold_mv = detector->threshold_mv - ECG_RPEAK_HYSTERESIS_MV;
  if (detector->low_threshold_mv < 0.0f)
  {
    detector->low_threshold_mv = 0.0f;
  }
  return detector->threshold_mv;
}

float32_t ECG_RPeak_GetThreshold(const ECG_RPeakDetector *detector)
{
  if (detector == NULL)
  {
    return 0.0f;
  }

  return detector->threshold_mv;
}

void ECG_RPeak_SetThresholdConfig(ECG_RPeakDetector *detector,
                                 const ECG_RPeakThresholdConfig *config)
{
  if ((detector == NULL) || (config == NULL))
  {
    return;
  }

  detector->threshold_mv = ClampThreshold(config->current_threshold_mv,
                                          config->min_threshold_mv,
                                          config->max_threshold_mv);
  detector->low_threshold_mv = detector->threshold_mv - ECG_RPEAK_HYSTERESIS_MV;
  if (detector->low_threshold_mv < 0.0f)
  {
    detector->low_threshold_mv = 0.0f;
  }
}

float32_t ECG_RPeak_ApplyKeyStep(ECG_RPeakDetector *detector,
                                 ECG_KeyEvent event,
                                 uint8_t increase)
{
  float32_t step_mv = 0.0f;

  if (detector == NULL)
  {
    return 0.0f;
  }

  switch (event)
  {
    case ECG_KEY_EVENT_SHORT_PRESS:
      step_mv = ECG_KEY_SHORT_STEP_MV;
      break;
    case ECG_KEY_EVENT_LONG_PRESS:
    case ECG_KEY_EVENT_REPEAT:
      step_mv = ECG_KEY_LONG_STEP_MV;
      break;
    default:
      return detector->threshold_mv;
  }

  if (increase != 0U)
  {
    return ECG_RPeak_SetThreshold(detector, detector->threshold_mv + step_mv);
  }
  return ECG_RPeak_SetThreshold(detector, detector->threshold_mv - step_mv);
}

static uint8_t IsRefractoryElapsed(const ECG_RPeakDetector *detector,
                                   uint32_t sample_index)
{
  if (detector->has_last_peak == 0U)
  {
    return 1U;
  }

  /* Unsigned subtraction keeps the comparison correct across counter wrap. */
  return (uint8_t)((sample_index - detector->last_peak_index) >=
                   ECG_RPEAK_REFRACTORY_SAMPLES);
}

uint8_t ECG_RPeak_ProcessSample(ECG_RPeakDetector *detector,
                                float32_t sample_mv,
                                uint32_t sample_index,
                                ECG_RPeakEvent *event)
{
  if ((detector == NULL) || (event == NULL))
  {
    return 0U;
  }

  if (detector->state == ECG_RPEAK_STATE_CONFIRM)
  {
    /* Strict greater-than preserves the first sample when the window has a flat maximum. */
    if (sample_mv > detector->window_max_mv)
    {
      detector->window_max_mv = sample_mv;
      detector->window_max_index = sample_index;
    }

    detector->confirm_samples++;
    if (detector->confirm_samples < ECG_RPEAK_CONFIRM_WINDOW_SAMPLES)
    {
      return 0U;
    }

    detector->state = ECG_RPEAK_STATE_ARMED;
    detector->low_seen = (uint8_t)(sample_mv <= detector->low_threshold_mv);

    if (detector->window_max_mv < detector->threshold_mv)
    {
      return 0U;
    }

    detector->has_last_peak = 1U;
    detector->last_peak_index = detector->window_max_index;
    event->sample_index = detector->window_max_index;
    event->value_mv = detector->window_max_mv;
    return 1U;
  }

  if (sample_mv <= detector->low_threshold_mv)
  {
    detector->low_seen = 1U;
  }

  if ((sample_mv >= detector->threshold_mv) && (detector->low_seen != 0U))
  {
    if (IsRefractoryElapsed(detector, sample_index) != 0U)
    {
      detector->state = ECG_RPEAK_STATE_CONFIRM;
      detector->low_seen = 0U;
      detector->window_max_mv = sample_mv;
      detector->window_max_index = sample_index;
      detector->confirm_samples = 1U;
    }
  }

  return 0U;
}
