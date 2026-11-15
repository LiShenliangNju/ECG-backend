#include <math.h>
#include <stdio.h>

#include "ecg_signal.h"

#define TEST_TOLERANCE 0.0001f

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

  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, second_block, 6U, output,
                                          2U) == 1U);
  CHECK_TRUE(NearlyEqual(output[0], 5.5f));
  CHECK_TRUE(state.samples_in_group == 0U);

  CHECK_TRUE(ECG_Signal_DownsampleAverage(&state, third_block, 5U, output,
                                          2U) == 0U);
  CHECK_TRUE(state.samples_in_group == 5U);
  return 0;
}

int main(void)
{
  if ((TestAdcConversion() != 0) || (TestDownsamplerAcrossBlocks() != 0))
  {
    return 1;
  }

  printf("all ECG signal checks passed\n");
  return 0;
}
