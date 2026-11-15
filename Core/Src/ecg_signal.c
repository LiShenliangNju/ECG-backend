#include "ecg_signal.h"

#include <math.h>
#include <stddef.h>

void ECG_Signal_AdcCodeToMillivolts(uint16_t adc_code, float32_t *millivolts)
{
  if (millivolts == NULL)
  {
    return;
  }

  const float32_t adc_voltage_v = ((float32_t)adc_code / (float32_t)ECG_ADC_MAX_CODE) *
                                  ((float32_t)ECG_ADC_VREF_MV / 1000.0f);
  const float32_t signal_voltage_v = adc_voltage_v - ECG_ECG_OFFSET_V;
  const float32_t gain_linear = powf(10.0f, ECG_FRONTEND_GAIN_DB / 20.0f);

  *millivolts = (signal_voltage_v / gain_linear) * 1000.0f;
}

void ECG_Signal_ConvertRawToMillivolts(const uint16_t *raw_samples,
                                       uint32_t sample_count,
                                       float32_t *millivolts)
{
  if ((raw_samples == NULL) || (millivolts == NULL) || (sample_count == 0U))
  {
    return;
  }

  for (uint32_t index = 0U; index < sample_count; ++index)
  {
    ECG_Signal_AdcCodeToMillivolts(raw_samples[index], &millivolts[index]);
  }
}

void ECG_Signal_Downsampler_Init(ECG_DownsamplerState *state)
{
  if (state == NULL)
  {
    return;
  }

  state->accumulator = 0.0f;
  state->samples_in_group = 0U;
}

uint32_t ECG_Signal_DownsampleAverage(ECG_DownsamplerState *state,
                                      const float32_t *input,
                                      uint32_t input_count,
                                      float32_t *output,
                                      uint32_t output_capacity)
{
  uint32_t output_count = 0U;

  if ((state == NULL) || (input == NULL) || (output == NULL) || (output_capacity == 0U))
  {
    return 0U;
  }

  for (uint32_t index = 0U; index < input_count; ++index)
  {
    state->accumulator += input[index];
    state->samples_in_group += 1U;

    if (state->samples_in_group == ECG_DOWNSAMPLE_FACTOR)
    {
      if (output_count < output_capacity)
      {
        output[output_count] = state->accumulator / (float32_t)ECG_DOWNSAMPLE_FACTOR;
        output_count += 1U;
      }

      state->accumulator = 0.0f;
      state->samples_in_group = 0U;
    }
  }

  return output_count;
}
