#ifndef ECG_MEASURE_H
#define ECG_MEASURE_H

#include "arm_math.h"

#define ECG_MEASURE_HISTORY_LENGTH 64U
#define ECG_MEASURE_HISTORY_MASK (ECG_MEASURE_HISTORY_LENGTH - 1U)

typedef struct
{
  float32_t min_mv;
  float32_t max_mv;
  float32_t peak_to_peak_mv;
  uint8_t valid;
} ECG_BeatMeasurement;

typedef struct
{
  float32_t history_mv[ECG_MEASURE_HISTORY_LENGTH];
  float32_t beat_min_mv;
  float32_t beat_max_mv;
  uint32_t last_index;
  uint32_t beat_start_index;
  uint32_t committed_index;
  ECG_BeatMeasurement latest;
  uint8_t has_sample;
  uint8_t beat_active;
  uint8_t beat_has_samples;
} ECG_MeasureState;

void ECG_Measure_Init(ECG_MeasureState *state);

/* Feed every 1 kHz sample before committing its R peak. */
void ECG_Measure_ProcessSample(ECG_MeasureState *state,
                               uint32_t sample_index,
                               float32_t sample_mv);

/* peak_index is the ecg_rpeak timestamp; returns 1 when a beat completed. */
uint8_t ECG_Measure_OnRPeak(ECG_MeasureState *state,
                            uint32_t peak_index,
                            ECG_BeatMeasurement *measurement);
const ECG_BeatMeasurement *ECG_Measure_GetLatest(const ECG_MeasureState *state);

#endif
