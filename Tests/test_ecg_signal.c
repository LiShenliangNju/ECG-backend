#include <math.h>
#include <stdio.h>
#include <string.h>

#include "IIR.h"
#include "ecg_signal.h"

#define TEST_TOLERANCE 0.0001f
#define TEST_SAMPLE_RATE_HZ 1000U
#define TEST_SIGNAL_AMPLITUDE_MV 0.5f
#define TEST_SIGNAL_LENGTH 4000U
#define TEST_SETTLE_LENGTH 2000U
#define TEST_MEASURE_LENGTH (TEST_SIGNAL_LENGTH - TEST_SETTLE_LENGTH)

#define CHECK_TRUE(condition)                                               \
  do                                                                        \
  {                                                                         \
    if (!(condition))                                                       \
    {                                                                       \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
      return 1;                                                             \
    }                                                                       \
  } while (0)

static int NearlyEqual(float32_t actual, float32_t expected)
{
  return fabsf(actual - expected) <= TEST_TOLERANCE;
}

static int TestAdcConversion(void)
{
  float32_t millivolts;
  const uint16_t raw_samples[] = {0U, 4095U};
  float32_t converted[2];

  ECG_Signal_AdcCodeToMillivolts(0U, &millivolts);
  CHECK_TRUE(NearlyEqual(millivolts, -0.94644f));

  ECG_Signal_AdcCodeToMillivolts(4095U, &millivolts);
  CHECK_TRUE(NearlyEqual(millivolts, 1.1357f));

  ECG_Signal_ConvertRawToMillivolts(raw_samples, 2U, converted);
  CHECK_TRUE(NearlyEqual(converted[0], -0.94644f));
  CHECK_TRUE(NearlyEqual(converted[1], 1.1357f));
  return 0;
}

static int TestDownsamplerAcrossBlocks(void)
{
  ECG_DownsamplerState state;
  const float32_t first_block[] = {1.0f, 2.0f, 3.0f, 4.0f};
  const float32_t second_block[] = {5.0f, 6.0f, 7.0f,
                                    8.0f, 9.0f, 10.0f};
  const float32_t third_block[] = {11.0f, 12.0f, 13.0f, 14.0f, 15.0f};
  float32_t output[2];

  ECG_Signal_Downsampler_Init(&state);
  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, first_block, 4U, output,
                                          2U) == 0U);
  CHECK_TRUE(state.samples_in_group == 4U);
  CHECK_TRUE(state.output_sample_count == 0U);

  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, second_block, 6U, output,
                                          2U) == 1U);
  CHECK_TRUE(NearlyEqual(output[0], 5.5f));
  CHECK_TRUE(state.samples_in_group == 0U);
  CHECK_TRUE(state.output_sample_count == 1U);

  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, third_block, 5U, output,
                                          2U) == 0U);
  CHECK_TRUE(state.samples_in_group == 5U);
  CHECK_TRUE(state.output_sample_count == 1U);
  return 0;
}

static int TestDownsamplerMaximumOutputCount(void)
{
  ECG_DownsamplerState state;
  float32_t input[256];
  float32_t output[26];

  for (uint32_t index = 0U; index < 256U; ++index)
  {
    input[index] = (float32_t)(index + 1U);
  }

  ECG_Signal_Downsampler_Init(&state);
  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, input, 256U, output,
                                          26U) == 25U);
  CHECK_TRUE(state.samples_in_group == 6U);
  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, input, 256U, output,
                                          26U) == 26U);
  CHECK_TRUE(state.samples_in_group == 2U);
  return 0;
}

static float32_t MeasureAmplitude(const float32_t *samples,
                                  uint32_t first_sample,
                                  uint32_t sample_count,
                                  uint32_t frequency_hz)
{
  double sine_sum = 0.0;
  double cosine_sum = 0.0;

  for (uint32_t index = 0U; index < sample_count; ++index)
  {
    const uint32_t sample_index = first_sample + index;
    const double phase = (2.0 * 3.14159265358979323846 *
                          (double)frequency_hz * (double)sample_index) /
                         (double)TEST_SAMPLE_RATE_HZ;
    sine_sum += (double)samples[sample_index] * sin(phase);
    cosine_sum += (double)samples[sample_index] * cos(phase);
  }

  return (float32_t)(2.0 * sqrt(sine_sum * sine_sum + cosine_sum * cosine_sum) /
                     (double)sample_count);
}

static int TestNotchFrequencyResponse(uint32_t frequency_hz,
                                      float32_t expected_gain_db,
                                      float32_t gain_tolerance_db,
                                      uint8_t check_passband_range)
{
  float32_t samples[TEST_SIGNAL_LENGTH];

  for (uint32_t index = 0U; index < TEST_SIGNAL_LENGTH; ++index)
  {
    const double phase = (2.0 * 3.14159265358979323846 *
                          (double)frequency_hz * (double)index) /
                         (double)TEST_SAMPLE_RATE_HZ;
    samples[index] = (float32_t)((double)TEST_SIGNAL_AMPLITUDE_MV *
                                 sin(phase));
  }

  IIR_Notch_Init();
  for (uint32_t offset = 0U; offset < TEST_SIGNAL_LENGTH;
       offset += IIR_NOTCH_MAX_BLOCK_SIZE)
  {
    const uint32_t remaining = TEST_SIGNAL_LENGTH - offset;
    const uint32_t block_size = remaining < IIR_NOTCH_MAX_BLOCK_SIZE ?
                                    remaining : IIR_NOTCH_MAX_BLOCK_SIZE;
    CHECK_TRUE(IIR_Notch_Process(&samples[offset], block_size) ==
               IIR_NOTCH_STATUS_OK);
  }

  for (uint32_t index = 0U; index < TEST_SIGNAL_LENGTH; ++index)
  {
    CHECK_TRUE(isfinite(samples[index]));
  }

  const float32_t output_amplitude =
      MeasureAmplitude(samples, TEST_SETTLE_LENGTH, TEST_MEASURE_LENGTH,
                       frequency_hz);
  const float32_t measured_gain_db =
      20.0f * log10f(output_amplitude / TEST_SIGNAL_AMPLITUDE_MV);

  if (frequency_hz == 50U)
  {
    CHECK_TRUE(measured_gain_db <= -60.0f);
  }
  else
  {
    CHECK_TRUE(fabsf(measured_gain_db - expected_gain_db) <=
               gain_tolerance_db);
  }

  if (check_passband_range != 0U)
  {
    float32_t peak = 0.0f;
    for (uint32_t index = TEST_SETTLE_LENGTH; index < TEST_SIGNAL_LENGTH; ++index)
    {
      const float32_t magnitude = fabsf(samples[index]);
      if (magnitude > peak)
      {
        peak = magnitude;
      }
    }
    CHECK_TRUE(fabsf((output_amplitude / TEST_SIGNAL_AMPLITUDE_MV) -
                     1.0f) <= 0.01f);
    CHECK_TRUE(peak <= TEST_SIGNAL_AMPLITUDE_MV * 1.02f);
    printf("response %lu Hz: gain %.3f dB, peak %.6f mV\n",
           (unsigned long)frequency_hz, (double)measured_gain_db,
           (double)peak);
  }
  else
  {
    printf("response %lu Hz: gain %.3f dB\n",
           (unsigned long)frequency_hz, (double)measured_gain_db);
  }

  return 0;
}

static int TestNotchFailureStatuses(void)
{
  float32_t sample = 0.25f;
  float32_t oversized[IIR_NOTCH_MAX_BLOCK_SIZE + 1U];

  CHECK_TRUE(IIR_Notch_Process(&sample, 1U) ==
             IIR_NOTCH_STATUS_NOT_INITIALIZED);
  CHECK_TRUE(IIR_Notch_Process(NULL, 1U) ==
             IIR_NOTCH_STATUS_INVALID_ARGUMENT);

  IIR_Notch_Init();
  CHECK_TRUE(IIR_Notch_Process(&sample, 0U) ==
             IIR_NOTCH_STATUS_INVALID_ARGUMENT);
  CHECK_TRUE(sample == 0.25f);
  CHECK_TRUE(IIR_Notch_Process(oversized,
                               IIR_NOTCH_MAX_BLOCK_SIZE + 1U) ==
             IIR_NOTCH_STATUS_BLOCK_TOO_LARGE);
  return 0;
}

static int TestNotchBlockContinuity(void)
{
  float32_t input[2048];
  float32_t reference[2048];
  float32_t segmented[2048];
  static const uint32_t segment_sizes[] = {73U, 128U, 41U, 256U, 17U, 191U};
  uint32_t offset;

  for (uint32_t index = 0U; index < 2048U; ++index)
  {
    const float32_t phase = (float32_t)index;
    input[index] = 0.6f * sinf(0.0628318531f * phase) +
                   0.2f * sinf(0.3141592654f * phase) +
                   0.05f * cosf(0.7980172273f * phase);
  }

  memcpy(reference, input, sizeof(input));
  IIR_Notch_Init();
  for (offset = 0U; offset < 2048U; offset += IIR_NOTCH_MAX_BLOCK_SIZE)
  {
    CHECK_TRUE(IIR_Notch_Process(&reference[offset],
                                 IIR_NOTCH_MAX_BLOCK_SIZE) ==
               IIR_NOTCH_STATUS_OK);
  }

  memcpy(segmented, input, sizeof(input));
  IIR_Notch_Init();
  offset = 0U;
  uint32_t segment_index = 0U;
  while (offset < 2048U)
  {
    uint32_t segment_size =
        segment_sizes[segment_index % (sizeof(segment_sizes) /
                                       sizeof(segment_sizes[0]))];
    if (segment_size > 2048U - offset)
    {
      segment_size = 2048U - offset;
    }
    CHECK_TRUE(IIR_Notch_Process(&segmented[offset], segment_size) ==
               IIR_NOTCH_STATUS_OK);
    offset += segment_size;
    segment_index++;
  }

  float32_t maximum_error = 0.0f;
  for (uint32_t index = 0U; index < 2048U; ++index)
  {
    const float32_t error = fabsf(reference[index] - segmented[index]);
    if (error > maximum_error)
    {
      maximum_error = error;
    }
  }
  CHECK_TRUE(maximum_error <= 1.0e-6f);
  printf("block continuity: max absolute difference %.9g\n",
         (double)maximum_error);
  return 0;
}

int main(void)
{
  if ((TestAdcConversion() != 0) ||
      (TestDownsamplerAcrossBlocks() != 0) ||
      (TestDownsamplerMaximumOutputCount() != 0) ||
      (TestNotchFailureStatuses() != 0) ||
      (TestNotchFrequencyResponse(1U, 0.0f, 0.1f, 1U) != 0) ||
      (TestNotchFrequencyResponse(10U, 0.0f, 0.1f, 1U) != 0) ||
      (TestNotchFrequencyResponse(40U, 0.0f, 0.1f, 1U) != 0) ||
      (TestNotchFrequencyResponse(49U, -3.0103f, 0.1f, 0U) != 0) ||
      (TestNotchFrequencyResponse(50U, 0.0f, 0.0f, 0U) != 0) ||
      (TestNotchFrequencyResponse(51U, -3.0103f, 0.1f, 0U) != 0) ||
      (TestNotchFrequencyResponse(60U, 0.0f, 0.1f, 1U) != 0) ||
      (TestNotchFrequencyResponse(100U, 0.0f, 0.1f, 1U) != 0) ||
      (TestNotchBlockContinuity() != 0))
  {
    return 1;
  }

  printf("all ECG stage 2 validation checks passed\n");
  return 0;
}
