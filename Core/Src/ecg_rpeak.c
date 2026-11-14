#include "ecg_rpeak.h"

#include <string.h>

static float32_t ClampThreshold(float32_t threshold_mv)
{
  if (threshold_mv < ECG_RPEAK_THRESHOLD_MIN_MV)
  {
    return ECG_RPEAK_THRESHOLD_MIN_MV;
  }
  if (threshold_mv > ECG_RPEAK_THRESHOLD_MAX_MV)
  {
    return ECG_RPEAK_THRESHOLD_MAX_MV;
  }
  return threshold_mv;
}

static void ApplyThreshold(ECG_RPeakDetector *detector, float32_t threshold_mv)
{
  detector->threshold_mv = ClampThreshold(threshold_mv);
  detector->low_threshold_mv = detector->threshold_mv - ECG_RPEAK_HYSTERESIS_MV;
  if (detector->low_threshold_mv < 0.0f)
  {
    detector->low_threshold_mv = 0.0f;
  }
}

void ECG_RPeak_Init(ECG_RPeakDetector *detector)
{
  if (detector == NULL)
  {
    return;
  }

  memset(detector, 0, sizeof(*detector));
  ApplyThreshold(detector, ECG_RPEAK_DEFAULT_THRESHOLD_MV);
  detector->state = ECG_RPEAK_STATE_ARMED;
  detector->low_seen = 1U;
}

float32_t ECG_RPeak_SetThreshold(ECG_RPeakDetector *detector, float32_t threshold_mv)
{
  if (detector == NULL)
  {
    return 0.0f;
  }

  ApplyThreshold(detector, threshold_mv);
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
