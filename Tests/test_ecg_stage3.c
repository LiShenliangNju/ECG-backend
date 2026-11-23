#include <stdio.h>
#include <string.h>

#include "ecg_measure.h"
#include "ecg_rpeak.h"
#include "heart_rate.h"

#define TEST_SIGNAL_LENGTH 9000U
#define TEST_MAX_EVENTS 64U
#define TEST_TOLERANCE 0.0001f

static float32_t g_signal[TEST_SIGNAL_LENGTH];
static uint32_t g_failures;

#define CHECK_TRUE(condition)                                              \
  do                                                                       \
  {                                                                        \
    if (!(condition))                                                      \
    {                                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
      g_failures++;                                                        \
    }                                                                      \
  } while (0)

#define CHECK_FLOAT_EQUAL(actual, expected)                                \
  do                                                                       \
  {                                                                        \
    const float32_t actual_value = (actual);                               \
    const float32_t expected_value = (expected);                           \
    if ((actual_value - expected_value > TEST_TOLERANCE) ||                \
        (expected_value - actual_value > TEST_TOLERANCE))                  \
    {                                                                      \
      printf("FAIL %s:%d: %s=%f expected %f\n", __FILE__, __LINE__,         \
             #actual, (double)actual_value, (double)expected_value);        \
      g_failures++;                                                        \
    }                                                                      \
  } while (0)

typedef struct
{
  ECG_RPeakEvent events[TEST_MAX_EVENTS];
  ECG_HeartRateResult last_heart_rate;
  ECG_BeatMeasurement last_measurement;
  uint32_t event_count;
  uint32_t completed_beats;
} PipelineResult;

static void ClearSignal(void)
{
  memset(g_signal, 0, sizeof(g_signal));
}

static void AddPulse(uint32_t center, float32_t amplitude)
{
  static const float32_t weights[5] = {0.4f, 0.7f, 1.0f, 0.7f, 0.4f};

  for (uint32_t offset = 0U; offset < 5U; ++offset)
  {
    g_signal[center - 2U + offset] = weights[offset] * amplitude;
  }
}

static void RunPipeline(const float32_t *samples,
                        uint32_t length,
                        uint32_t block_size,
                        uint32_t start_index,
                        PipelineResult *result)
{
  ECG_RPeakDetector detector;
  ECG_HeartRate heart_rate;
  ECG_MeasureState measure;

  ECG_RPeak_Init(&detector);
  ECG_HeartRate_Init(&heart_rate);
  ECG_Measure_Init(&measure);
  memset(result, 0, sizeof(*result));

  for (uint32_t block_start = 0U; block_start < length; block_start += block_size)
  {
    uint32_t block_end = block_start + block_size;
    if (block_end > length)
    {
      block_end = length;
    }

    for (uint32_t offset = block_start; offset < block_end; ++offset)
    {
      const uint32_t sample_index = start_index + offset;
      ECG_RPeakEvent event;
      ECG_BeatMeasurement measurement;

      ECG_Measure_ProcessSample(&measure, sample_index, samples[offset]);

      if (ECG_RPeak_ProcessSample(&detector, samples[offset], sample_index,
                                  &event) != 0U)
      {
        if (result->event_count < TEST_MAX_EVENTS)
        {
          result->events[result->event_count] = event;
        }
        result->event_count++;

        ECG_HeartRate_OnPeak(&heart_rate, event.sample_index,
                             &result->last_heart_rate);

        if (ECG_Measure_OnRPeak(&measure, event.sample_index, &measurement) != 0U)
        {
          result->completed_beats++;
          result->last_measurement = measurement;
        }
      }
    }
  }
}

static void TestDetectionAcrossBlocks(void)
{
  static const uint32_t centers[] = {1024U, 2024U, 3024U, 4024U,
                                     5024U, 6024U, 7024U, 8024U};
  const uint32_t center_count = sizeof(centers) / sizeof(centers[0]);
  PipelineResult result;

  ClearSignal();
  for (uint32_t index = 0U; index < center_count; ++index)
  {
    AddPulse(centers[index], 0.5f);
  }

  RunPipeline(g_signal, TEST_SIGNAL_LENGTH, 64U, 0U, &result);

  CHECK_TRUE(result.event_count == center_count);
  for (uint32_t index = 0U;
       (index < center_count) && (index < result.event_count); ++index)
  {
    CHECK_TRUE(result.events[index].sample_index == centers[index]);
    CHECK_FLOAT_EQUAL(result.events[index].value_mv, 0.5f);
  }
  CHECK_TRUE(result.completed_beats == center_count - 1U);
  CHECK_TRUE(result.last_heart_rate.stable == 1U);
  CHECK_TRUE(result.last_heart_rate.average_bpm == 60U);
}

static void TestRefractoryAndInvalidCandidates(void)
{
  PipelineResult result;

  ClearSignal();
  AddPulse(1000U, 0.5f);
  AddPulse(1100U, 0.5f);
  AddPulse(2000U, 0.05f);
  AddPulse(3000U, 0.5f);

  RunPipeline(g_signal, 4000U, 128U, 0U, &result);

  CHECK_TRUE(result.event_count == 2U);
  if (result.event_count == 2U)
  {
    CHECK_TRUE(result.events[0].sample_index == 1000U);
    CHECK_TRUE(result.events[1].sample_index == 3000U);
  }
}

static void TestShortAndLongRrDetection(void)
{
  static const uint32_t centers[] = {1000U, 1300U, 2700U};
  const uint32_t center_count = sizeof(centers) / sizeof(centers[0]);
  PipelineResult result;

  ClearSignal();
  for (uint32_t index = 0U; index < center_count; ++index)
  {
    AddPulse(centers[index], 0.5f);
  }

  RunPipeline(g_signal, 4000U, 256U, 0U, &result);

  CHECK_TRUE(result.event_count == center_count);
  for (uint32_t index = 0U;
       (index < center_count) && (index < result.event_count); ++index)
  {
    CHECK_TRUE(result.events[index].sample_index == centers[index]);
  }
  CHECK_TRUE(result.last_heart_rate.rr_ms == 1400U);
  CHECK_TRUE(result.last_heart_rate.rr_in_range == 0U);
  CHECK_TRUE(result.last_heart_rate.stable == 0U);
}

static void TestFlatPeakUsesFirstMaximum(void)
{
  ECG_RPeakDetector detector;
  ECG_RPeakEvent event;

  ECG_RPeak_Init(&detector);
  CHECK_TRUE(ECG_RPeak_ProcessSample(&detector, 0.0f, 0U, &event) == 0U);
  CHECK_TRUE(ECG_RPeak_ProcessSample(&detector, 0.0f, 1U, &event) == 0U);
  CHECK_TRUE(ECG_RPeak_ProcessSample(&detector, 0.5f, 2U, &event) == 0U);
  CHECK_TRUE(ECG_RPeak_ProcessSample(&detector, 0.5f, 3U, &event) == 0U);

  for (uint32_t index = 4U; index < 31U; ++index)
  {
    CHECK_TRUE(ECG_RPeak_ProcessSample(&detector, 0.0f, index, &event) == 0U);
  }

  CHECK_TRUE(ECG_RPeak_ProcessSample(&detector, 0.0f, 31U, &event) == 1U);
  CHECK_TRUE(event.sample_index == 2U);
  CHECK_FLOAT_EQUAL(event.value_mv, 0.5f);
}

static void TestThresholdAdjustment(void)
{
  ECG_RPeakDetector detector;
  ECG_RPeakEvent event;
  uint8_t detected = 0U;

  ECG_RPeak_Init(&detector);
  CHECK_FLOAT_EQUAL(ECG_RPeak_SetThreshold(&detector, 2.0f),
                    ECG_RPEAK_THRESHOLD_MAX_MV);
  CHECK_FLOAT_EQUAL(ECG_RPeak_SetThreshold(&detector, 0.001f),
                    ECG_RPEAK_THRESHOLD_MIN_MV);
  CHECK_FLOAT_EQUAL(ECG_RPeak_SetThreshold(&detector, 0.4f), 0.4f);
  CHECK_FLOAT_EQUAL(ECG_RPeak_GetThreshold(&detector), 0.4f);

  ClearSignal();
  AddPulse(1000U, 0.35f);
  for (uint32_t index = 0U; index < 2000U; ++index)
  {
    detected = (uint8_t)(detected |
                         ECG_RPeak_ProcessSample(&detector, g_signal[index],
                                                 index, &event));
  }
  CHECK_TRUE(detected == 0U);

  ClearSignal();
  AddPulse(1000U, 0.45f);
  detected = 0U;
  for (uint32_t index = 0U; index < 2000U; ++index)
  {
    detected = (uint8_t)(detected |
                         ECG_RPeak_ProcessSample(&detector, g_signal[index],
                                                 index, &event));
  }
  CHECK_TRUE(detected == 1U);
  CHECK_TRUE(event.sample_index == 1000U);
}

static void TestHeartRateStartupAndSmoothing(void)
{
  ECG_HeartRate heart_rate;
  ECG_HeartRateResult result;

  ECG_HeartRate_Init(&heart_rate);

  CHECK_TRUE(ECG_HeartRate_OnPeak(&heart_rate, 1000U, &result) == 0U);
  CHECK_TRUE(result.has_interval == 0U);
  CHECK_TRUE(result.stable == 0U);

  for (uint32_t index = 1U; index <= ECG_HEART_RATE_STABLE_RR_COUNT; ++index)
  {
    CHECK_TRUE(ECG_HeartRate_OnPeak(&heart_rate, 1000U + 1000U * index,
                                    &result) == 1U);
    CHECK_TRUE(result.rr_ms == 1000U);
    CHECK_TRUE(result.rr_in_range == 1U);
    CHECK_TRUE(result.instant_bpm == 60U);
  }
  CHECK_TRUE(result.stable == 1U);
  CHECK_TRUE(result.average_bpm == 60U);

  CHECK_TRUE(ECG_HeartRate_OnPeak(&heart_rate, 6800U, &result) == 1U);
  CHECK_TRUE(result.rr_ms == 800U);
  CHECK_TRUE(result.instant_bpm == 75U);
  CHECK_TRUE(result.average_bpm == 63U);

  CHECK_TRUE(ECG_HeartRate_OnPeak(&heart_rate, 7200U, &result) == 1U);
  CHECK_TRUE(result.rr_ms == 400U);
  CHECK_TRUE(result.rr_in_range == 0U);
  CHECK_TRUE(result.stable == 0U);
  CHECK_TRUE(result.average_bpm == 63U);

  for (uint32_t index = 1U; index <= ECG_HEART_RATE_STABLE_RR_COUNT; ++index)
  {
    CHECK_TRUE(ECG_HeartRate_OnPeak(&heart_rate, 7200U + 1000U * index,
                                    &result) == 1U);
  }
  CHECK_TRUE(result.stable == 1U);
  CHECK_TRUE(result.average_bpm == 60U);
}

static void TestHeartRateRrBounds(void)
{
  ECG_HeartRate heart_rate;
  ECG_HeartRateResult result;

  ECG_HeartRate_Init(&heart_rate);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U, &result);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U + 410U, &result);
  CHECK_TRUE(result.rr_in_range == 1U);

  ECG_HeartRate_Init(&heart_rate);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U, &result);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U + 409U, &result);
  CHECK_TRUE(result.rr_in_range == 0U);
  CHECK_TRUE(result.stable == 0U);

  ECG_HeartRate_Init(&heart_rate);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U, &result);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U + 1375U, &result);
  CHECK_TRUE(result.rr_in_range == 1U);
  CHECK_TRUE(result.instant_bpm == 43U);
  CHECK_TRUE(result.average_bpm == 44U);

  ECG_HeartRate_Init(&heart_rate);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U, &result);
  ECG_HeartRate_OnPeak(&heart_rate, 1000U + 1376U, &result);
  CHECK_TRUE(result.rr_in_range == 0U);
  CHECK_TRUE(result.average_bpm == 0U);
}

static void TestHeartRateCounterWrap(void)
{
  ECG_HeartRate heart_rate;
  ECG_HeartRateResult result;

  ECG_HeartRate_Init(&heart_rate);
  ECG_HeartRate_OnPeak(&heart_rate, 0xFFFFFFFFU - 500U, &result);
  CHECK_TRUE(ECG_HeartRate_OnPeak(&heart_rate, 499U, &result) == 1U);
  CHECK_TRUE(result.rr_ms == 1000U);
  CHECK_TRUE(result.rr_in_range == 1U);
  CHECK_TRUE(result.instant_bpm == 60U);
}

static void ComputeExpectedMeasurement(const float32_t *samples,
                                       uint32_t first_index,
                                       uint32_t last_index,
                                       float32_t *min_mv,
                                       float32_t *max_mv)
{
  *min_mv = samples[first_index];
  *max_mv = samples[first_index];

  for (uint32_t index = first_index + 1U; index <= last_index; ++index)
  {
    if (samples[index] < *min_mv)
    {
      *min_mv = samples[index];
    }
    if (samples[index] > *max_mv)
    {
      *max_mv = samples[index];
    }
  }
}

static void TestBeatPeakToPeak(void)
{
  static const uint32_t centers[] = {1000U, 2000U, 3000U, 4000U};
  const uint32_t center_count = sizeof(centers) / sizeof(centers[0]);
  PipelineResult result;

  ClearSignal();
  for (uint32_t index = 0U; index < center_count; ++index)
  {
    AddPulse(centers[index], 0.5f);
    g_signal[centers[index] + 1U] = -1.0f;
  }
  for (uint32_t index = 1100U; index < 1200U; ++index)
  {
    g_signal[index] = -0.2f;
  }
  for (uint32_t index = 2100U; index < 2200U; ++index)
  {
    g_signal[index] = -0.3f;
  }

  RunPipeline(g_signal, 5000U, 128U, 0U, &result);

  CHECK_TRUE(result.event_count == center_count);
  CHECK_TRUE(result.completed_beats == center_count - 1U);

  if (result.event_count == center_count)
  {
    for (uint32_t index = 1U; index < center_count; ++index)
    {
      ECG_MeasureState measure;
      ECG_BeatMeasurement measurement;
      float32_t expected_min;
      float32_t expected_max;

      ECG_Measure_Init(&measure);
      /* Mirror the pipeline: peaks are committed after the confirm delay. */
      for (uint32_t sample = 0U;
           sample <= centers[index - 1U] + ECG_RPEAK_MAX_CONFIRM_DELAY_SAMPLES;
           ++sample)
      {
        ECG_Measure_ProcessSample(&measure, sample, g_signal[sample]);
      }
      ECG_Measure_OnRPeak(&measure, centers[index - 1U], &measurement);

      for (uint32_t sample =
               centers[index - 1U] + ECG_RPEAK_MAX_CONFIRM_DELAY_SAMPLES + 1U;
           sample <= centers[index] + ECG_RPEAK_MAX_CONFIRM_DELAY_SAMPLES;
           ++sample)
      {
        ECG_Measure_ProcessSample(&measure, sample, g_signal[sample]);
      }
      CHECK_TRUE(ECG_Measure_OnRPeak(&measure, centers[index],
                                     &measurement) == 1U);

      ComputeExpectedMeasurement(g_signal, centers[index - 1U] + 1U,
                                 centers[index], &expected_min, &expected_max);
      CHECK_TRUE(measurement.valid == 1U);
      CHECK_FLOAT_EQUAL(measurement.min_mv, expected_min);
      CHECK_FLOAT_EQUAL(measurement.max_mv, expected_max);
      CHECK_FLOAT_EQUAL(measurement.peak_to_peak_mv,
                        expected_max - expected_min);
    }
  }
}

static void TestPipelineCounterWrap(void)
{
  static const uint32_t centers[] = {500U, 1500U, 2500U, 3500U};
  const uint32_t center_count = sizeof(centers) / sizeof(centers[0]);
  const uint32_t start_index = 0xFFFFFFFFU - 3499U;
  PipelineResult result;

  ClearSignal();
  for (uint32_t index = 0U; index < center_count; ++index)
  {
    AddPulse(centers[index], 0.5f);
  }

  RunPipeline(g_signal, 4500U, 256U, start_index, &result);

  CHECK_TRUE(result.event_count == center_count);
  for (uint32_t index = 0U;
       (index < center_count) && (index < result.event_count); ++index)
  {
    CHECK_TRUE(result.events[index].sample_index ==
               start_index + centers[index]);
  }
  CHECK_TRUE(result.last_heart_rate.rr_ms == 1000U);
  CHECK_TRUE(result.last_heart_rate.instant_bpm == 60U);
  CHECK_TRUE(result.completed_beats == center_count - 1U);
  CHECK_TRUE(result.last_measurement.valid == 1U);
}

static int RunModuleTests(const char *module)
{
  if (strcmp(module, "rpeak") == 0)
  {
    TestRefractoryAndInvalidCandidates();
    TestShortAndLongRrDetection();
    TestFlatPeakUsesFirstMaximum();
    TestThresholdAdjustment();
  }
  else if (strcmp(module, "heart_rate") == 0)
  {
    TestHeartRateStartupAndSmoothing();
    TestHeartRateRrBounds();
    TestHeartRateCounterWrap();
  }
  else if (strcmp(module, "measure") == 0)
  {
    TestBeatPeakToPeak();
  }
  else if (strcmp(module, "pipeline") == 0)
  {
    TestDetectionAcrossBlocks();
    TestPipelineCounterWrap();
  }
  else
  {
    return 0;
  }

  return 1;
}

int main(int argc, char *argv[])
{
  if (argc == 1)
  {
    TestDetectionAcrossBlocks();
    TestRefractoryAndInvalidCandidates();
    TestShortAndLongRrDetection();
    TestFlatPeakUsesFirstMaximum();
    TestThresholdAdjustment();
    TestHeartRateStartupAndSmoothing();
    TestHeartRateRrBounds();
    TestHeartRateCounterWrap();
    TestBeatPeakToPeak();
    TestPipelineCounterWrap();

    if (g_failures != 0U)
    {
      printf("%u checks failed\n", g_failures);
      return 1;
    }

    printf("all stage 3 checks passed\n");
    return 0;
  }

  if ((argc == 3) && (strcmp(argv[1], "--module") == 0))
  {
    if (RunModuleTests(argv[2]) == 0)
    {
      printf("unknown module: %s\n", argv[2]);
      return 2;
    }

    if (g_failures != 0U)
    {
      printf("%u checks failed\n", g_failures);
      return 1;
    }

    printf("all %s checks passed\n", argv[2]);
    return 0;
  }

  printf("usage: %s [--module rpeak|heart_rate|measure|pipeline]\n", argv[0]);
  return 2;
}
