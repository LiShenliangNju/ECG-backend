#ifndef ECG_SIGNAL_H
#define ECG_SIGNAL_H

#include "arm_math.h"

#include <stdint.h>

#define ECG_ADC_RESOLUTION_BITS 12U
#define ECG_ADC_MAX_CODE ((1U << ECG_ADC_RESOLUTION_BITS) - 1U)
#define ECG_ADC_VREF_MV 3300.0f
#define ECG_ECG_OFFSET_V 1.5f
#define ECG_FRONTEND_GAIN_DB 64.0f
#define ECG_DOWNSAMPLE_FACTOR 10U

typedef struct
{
  float32_t accumulator;
  uint32_t samples_in_group;
} ECG_DownsamplerState;

void ECG_Signal_AdcCodeToMillivolts(uint16_t adc_code, float32_t *millivolts);
void ECG_Signal_ConvertRawToMillivolts(const uint16_t *raw_samples,
                                       uint32_t sample_count,
                                       float32_t *millivolts);
void ECG_Signal_Downsampler_Init(ECG_DownsamplerState *state);
uint32_t ECG_Signal_DownsampleAverage(ECG_DownsamplerState *state,
                                      const float32_t *input,
                                      uint32_t input_count,
                                      float32_t *output,
                                      uint32_t output_capacity);

#endif
