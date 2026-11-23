#ifndef ECG_STAGE3_HIL_H
#define ECG_STAGE3_HIL_H

#include "ecg_measure.h"
#include "ecg_rpeak.h"
#include "heart_rate.h"

#define ECG_STAGE3_HIL_SAMPLE_COUNT 12800U
#define ECG_STAGE3_HIL_PEAK_RECORD_CAPACITY 32U

typedef struct
{
  uint32_t peak_index;
  uint32_t rr_ms;
  uint32_t instant_bpm;
  uint32_t average_bpm;
  float32_t peak_value_mv;
  float32_t measurement_min_mv;
  float32_t measurement_max_mv;
  float32_t measurement_peak_to_peak_mv;
  uint8_t has_interval;
  uint8_t rr_in_range;
  uint8_t stable;
  uint8_t measurement_valid;
} ECG_Stage3HILPeakRecord;

typedef struct
{
  volatile uint32_t magic;
  volatile uint32_t sample_target;
  volatile uint32_t processed_sample_count;
  volatile uint32_t peak_count;
  volatile uint32_t peak_record_count;
  volatile uint32_t peak_record_overflow_count;
  volatile uint32_t capture_complete;
  ECG_Stage3HILPeakRecord peaks[ECG_STAGE3_HIL_PEAK_RECORD_CAPACITY];
} ECG_Stage3HILCapture;

extern ECG_Stage3HILCapture g_ecg_stage3_hil_capture;

void ECG_Stage3HIL_Init(void);
void ECG_Stage3HIL_RecordPeak(const ECG_RPeakEvent *peak,
                              const ECG_HeartRateResult *heart_rate,
                              const ECG_BeatMeasurement *measurement);
void ECG_Stage3HIL_RecordSamples(uint32_t sample_count);
uint8_t ECG_Stage3HIL_IsCaptureComplete(void);

#endif
