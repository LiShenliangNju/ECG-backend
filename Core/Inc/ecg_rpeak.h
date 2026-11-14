#ifndef ECG_RPEAK_H
#define ECG_RPEAK_H

#include "arm_math.h"
#include "key_input.h"

#define ECG_RPEAK_SAMPLE_RATE_HZ 1000U
#define ECG_RPEAK_DEFAULT_THRESHOLD_MV 0.10f
#define ECG_RPEAK_THRESHOLD_MIN_MV 0.05f
#define ECG_RPEAK_THRESHOLD_MAX_MV 0.50f
#define ECG_RPEAK_HYSTERESIS_MV 0.05f
#define ECG_RPEAK_CONFIRM_WINDOW_SAMPLES 30U
#define ECG_RPEAK_MAX_CONFIRM_DELAY_SAMPLES (ECG_RPEAK_CONFIRM_WINDOW_SAMPLES - 1U)
#define ECG_RPEAK_REFRACTORY_SAMPLES 200U

typedef enum
{
  ECG_RPEAK_STATE_ARMED = 0U,
  ECG_RPEAK_STATE_CONFIRM = 1U
} ECG_RPeakState;

typedef struct
{
  float32_t threshold_mv;
  float32_t low_threshold_mv;
  float32_t window_max_mv;
  uint32_t window_max_index;
  uint32_t confirm_samples;
  uint32_t last_peak_index;
  ECG_RPeakState state;
  uint8_t low_seen;
  uint8_t has_last_peak;
} ECG_RPeakDetector;

typedef struct
{
  uint32_t sample_index;
  float32_t value_mv;
} ECG_RPeakEvent;

typedef struct
{
  float32_t current_threshold_mv;
  float32_t min_threshold_mv;
  float32_t max_threshold_mv;
  float32_t step_mv;
  float32_t long_step_mv;
} ECG_RPeakThresholdConfig;

void ECG_RPeak_Init(ECG_RPeakDetector *detector);
float32_t ECG_RPeak_SetThreshold(ECG_RPeakDetector *detector, float32_t threshold_mv);
float32_t ECG_RPeak_GetThreshold(const ECG_RPeakDetector *detector);
void ECG_RPeak_SetThresholdConfig(ECG_RPeakDetector *detector,
                                 const ECG_RPeakThresholdConfig *config);
float32_t ECG_RPeak_ApplyKeyStep(ECG_RPeakDetector *detector,
                                 ECG_KeyEvent event,
                                 uint8_t increase);

/* sample_index must be a continuous 1 kHz counter shared with the other ECG
 * modules so that timestamp differences are milliseconds across counter wrap. */
uint8_t ECG_RPeak_ProcessSample(ECG_RPeakDetector *detector,
                                float32_t sample_mv,
                                uint32_t sample_index,
                                ECG_RPeakEvent *event);

#endif
