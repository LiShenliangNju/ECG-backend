#ifndef HEART_RATE_H
#define HEART_RATE_H

#include <stdint.h>

#define ECG_HEART_RATE_RR_MIN_MS 410U
#define ECG_HEART_RATE_RR_MAX_MS 1375U
#define ECG_HEART_RATE_RR_WINDOW 5U
#define ECG_HEART_RATE_STABLE_RR_COUNT 5U

typedef struct
{
  uint32_t rr_window_ms[ECG_HEART_RATE_RR_WINDOW];
  uint32_t previous_timestamp;
  uint32_t last_rr_ms;
  uint32_t last_instant_bpm;
  uint32_t valid_interval_count;
  uint32_t rejected_interval_count;
  uint8_t rr_window_count;
  uint8_t rr_window_head;
  uint8_t consecutive_valid_rr;
  uint8_t has_previous;
  uint8_t stable;
} ECG_HeartRate;

typedef struct
{
  uint32_t rr_ms;
  uint32_t instant_bpm;
  uint32_t average_bpm;
  uint8_t has_interval;
  uint8_t rr_in_range;
  uint8_t stable;
} ECG_HeartRateResult;

void ECG_HeartRate_Init(ECG_HeartRate *state);

/* peak_timestamp is the 1 kHz sample index reported by ecg_rpeak. */
uint8_t ECG_HeartRate_OnPeak(ECG_HeartRate *state,
                             uint32_t peak_timestamp,
                             ECG_HeartRateResult *result);

#endif
