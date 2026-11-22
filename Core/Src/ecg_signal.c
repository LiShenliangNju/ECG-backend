/*
 * Signal conversion/decimation data flow:
 *   ADC DMA uint16_t codes
 *     -> code-to-voltage conversion using the configured 12-bit ADC scale,
 *        3.3 V reference, 1.5 V offset, and 64 dB gain
 *     -> float32_t ECG samples in millivolts
 *     -> ten-sample running average
 *     -> one output sample per complete group (10 kHz to 1 kHz by design).
 *
 * This file owns no persistent state. The caller owns ECG_DownsamplerState
 * and must retain it across blocks. These routines are expected to run in
 * the main loop/task context, not in the ADC DMA ISR; there is no locking.
 */
#include "ecg_signal.h"

#include <math.h>
#include <stddef.h>

/*
 * Convert one code using the module's fixed calibration. Main-loop/task
 * context; performs floating-point arithmetic and powf, with no shared state.
 * A NULL destination is a silent no-op.
 */
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

/*
 * Convert a raw block element by element. The caller must provide a writable
 * output array at least sample_count elements long. Invalid pointers or an
 * empty block are silently ignored; valid calls have no failure return.
 * Main-loop/task context; no module state is changed.
 */
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

/*
 * Start a fresh averaging stream. Resetting between DMA blocks breaks the
 * ten-sample phase and therefore the output sample timing.
 * Main-loop/task context; NULL is ignored.
 */
void ECG_Signal_Downsampler_Init(ECG_DownsamplerState *state)
{
  if (state == NULL)
  {
    return;
  }

  state->accumulator = 0.0f;
  state->samples_in_group = 0U;
}

/*
 * Consume samples while preserving any incomplete group in caller state.
 * On return, samples_in_group is less than ECG_DOWNSAMPLE_FACTOR. When the
 * output array fills, complete groups are still consumed and reset but their
 * averages are dropped; the return value counts only values actually stored.
 * Main-loop/task context; state must not be shared by concurrent callers.
 */
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
