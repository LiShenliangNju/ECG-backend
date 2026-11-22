#ifndef ECG_SIGNAL_H
#define ECG_SIGNAL_H

/*
 * ECG signal conversion and averaging decimator.
 *
 * Convert 12-bit ADC codes to calibrated ECG millivolts, then pass the
 * converted stream through one caller-owned downsampler state. Keep that
 * state across consecutive input blocks so groups of ten samples do not
 * restart at DMA boundaries. The state and output buffers are caller-owned;
 * this module has no internal synchronization.
 *
 * Typical use:
 *   ECG_Signal_Downsampler_Init(&state);
 *   ECG_Signal_ConvertRawToMillivolts(raw, count, mv);
 *   produced = ECG_Signal_DownsampleAverage(&state, mv, count, out, capacity);
 *
 * Invalid pointers or zero-sized inputs are ignored and reported only by
 * producing no output; conversion functions have no status return. When the
 * decimator's output capacity is exhausted, additional complete groups are
 * consumed but discarded.
 */
#include "arm_math.h"

#include <stdint.h>

/* ADC calibration and the number of input samples averaged into each output. */
#define ECG_ADC_RESOLUTION_BITS 12U
#define ECG_ADC_MAX_CODE ((1U << ECG_ADC_RESOLUTION_BITS) - 1U)
#define ECG_ADC_VREF_MV 3300.0f
#define ECG_ECG_OFFSET_V 1.5f
#define ECG_FRONTEND_GAIN_DB 64.0f
#define ECG_DOWNSAMPLE_FACTOR 10U

/*
 * Caller-owned partial-group state. Preserve the same instance across blocks;
 * initialize once per stream, not once per DMA callback.
 */
typedef struct
{
  float32_t accumulator;
  uint32_t samples_in_group;
} ECG_DownsamplerState;

/*
 * Convert one ADC code to ECG millivolts. millivolts must point to writable
 * storage; NULL is ignored. adc_code is interpreted using the module's
 * 12-bit, 3.3 V, 1.5 V-offset, 64 dB calibration.
 */
void ECG_Signal_AdcCodeToMillivolts(uint16_t adc_code, float32_t *millivolts);

/*
 * Convert sample_count raw ADC codes into the equally sized millivolts array.
 * Both arrays must hold sample_count elements. NULL pointers or a zero count
 * are ignored; no status is returned to distinguish these cases.
 */
void ECG_Signal_ConvertRawToMillivolts(const uint16_t *raw_samples,
                                       uint32_t sample_count,
                                       float32_t *millivolts);

/* Reset a caller-owned decimator state; NULL is ignored. */
void ECG_Signal_Downsampler_Init(ECG_DownsamplerState *state);

/*
 * Consume input_count float samples and emit averages of consecutive groups
 * of ECG_DOWNSAMPLE_FACTOR samples. Returns the number of values actually
 * written. A partial group remains in state for the next call.
 *
 * state, input, and output must be non-NULL and output_capacity nonzero;
 * otherwise the function returns zero without consuming input. If capacity
 * is insufficient, later completed groups in this call are discarded while
 * input is still consumed and the group accumulator continues to advance.
 * Serialize calls using a given state; this function is not internally locked.
 */
uint32_t ECG_Signal_DownsampleAverage(ECG_DownsamplerState *state,
                                      const float32_t *input,
                                      uint32_t input_count,
                                      float32_t *output,
                                      uint32_t output_capacity);

#endif
