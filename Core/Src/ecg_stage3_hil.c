#include "ecg_stage3_hil.h"

#include <string.h>

ECG_Stage3HILCapture g_ecg_stage3_hil_capture;

void ECG_Stage3HIL_Init(void)
{
  memset(&g_ecg_stage3_hil_capture, 0,
         sizeof(g_ecg_stage3_hil_capture));
  g_ecg_stage3_hil_capture.magic = 0xEC630001U;
  g_ecg_stage3_hil_capture.sample_target = ECG_STAGE3_HIL_SAMPLE_COUNT;
}

void ECG_Stage3HIL_RecordPeak(const ECG_RPeakEvent *peak,
                              const ECG_HeartRateResult *heart_rate,
                              const ECG_BeatMeasurement *measurement)
{
  if ((peak == NULL) || (heart_rate == NULL) || (measurement == NULL))
  {
    return;
  }

  g_ecg_stage3_hil_capture.peak_count++;
  const uint32_t record_count =
      g_ecg_stage3_hil_capture.peak_record_count;
  if (record_count >= ECG_STAGE3_HIL_PEAK_RECORD_CAPACITY)
  {
    g_ecg_stage3_hil_capture.peak_record_overflow_count++;
    return;
  }

  ECG_Stage3HILPeakRecord *record =
      &g_ecg_stage3_hil_capture.peaks[record_count];
  record->peak_index = peak->sample_index;
  record->peak_value_mv = peak->value_mv;
  record->rr_ms = heart_rate->rr_ms;
  record->instant_bpm = heart_rate->instant_bpm;
  record->average_bpm = heart_rate->average_bpm;
  record->has_interval = heart_rate->has_interval;
  record->rr_in_range = heart_rate->rr_in_range;
  record->stable = heart_rate->stable;
  record->measurement_min_mv = measurement->min_mv;
  record->measurement_max_mv = measurement->max_mv;
  record->measurement_peak_to_peak_mv = measurement->peak_to_peak_mv;
  record->measurement_valid = measurement->valid;
  g_ecg_stage3_hil_capture.peak_record_count = record_count + 1U;
}

void ECG_Stage3HIL_RecordSamples(uint32_t sample_count)
{
  g_ecg_stage3_hil_capture.processed_sample_count += sample_count;
  if (g_ecg_stage3_hil_capture.processed_sample_count >=
      g_ecg_stage3_hil_capture.sample_target)
  {
    g_ecg_stage3_hil_capture.capture_complete = 1U;
  }
}

uint8_t ECG_Stage3HIL_IsCaptureComplete(void)
{
  return (uint8_t)(g_ecg_stage3_hil_capture.capture_complete != 0U);
}
