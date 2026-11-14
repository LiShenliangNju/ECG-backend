#include <stdio.h>

#include "key_input.h"

#define TEST_TOLERANCE 0.0001f
#define CHECK_TRUE(condition)                                              \
  do                                                                       \
  {                                                                        \
    if (!(condition))                                                      \
    {                                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
      return 1;                                                            \
    }                                                                      \
  } while (0)

#define CHECK_EVENT(actual, expected)                                      \
  do                                                                       \
  {                                                                        \
    if ((actual) != (expected))                                            \
    {                                                                      \
      printf("FAIL %s:%d: event=%d expected=%d\n", __FILE__, __LINE__,   \
             (int)(actual), (int)(expected));                            \
      return 1;                                                            \
    }                                                                      \
  } while (0)

#define CHECK_FLOAT_EQUAL(actual, expected)                                \
  do                                                                       \
  {                                                                        \
    const float actual_value = (float)(actual);                            \
    const float expected_value = (float)(expected);                        \
    if ((actual_value - expected_value) > TEST_TOLERANCE ||                 \
        (expected_value - actual_value) > TEST_TOLERANCE)                  \
    {                                                                      \
      printf("FAIL %s:%d: %f expected %f\n", __FILE__, __LINE__,         \
             actual_value, expected_value);                                \
      return 1;                                                            \
    }                                                                      \
  } while (0)

static int TestShortPressAndDebounce(void)
{
  ECG_KeyInput key;
  ECG_KeyEvent event;

  ECG_KeyInput_Init(&key);
  event = ECG_KeyInput_Update(&key, 0U, 1U);
  CHECK_EVENT(event, ECG_KEY_EVENT_NONE);
  event = ECG_KeyInput_Update(&key, 10U, 1U);
  CHECK_EVENT(event, ECG_KEY_EVENT_NONE);
  event = ECG_KeyInput_Update(&key, 20U, 1U);
  CHECK_EVENT(event, ECG_KEY_EVENT_NONE);
  event = ECG_KeyInput_Update(&key, 40U, 0U);
  CHECK_EVENT(event, ECG_KEY_EVENT_NONE);
  event = ECG_KeyInput_Update(&key, 60U, 0U);
  CHECK_EVENT(event, ECG_KEY_EVENT_SHORT_PRESS);
  CHECK_TRUE(ECG_KeyInput_IsPressed(&key) == 0U);
  return 0;
}

static int TestLongPressRepeat(void)
{
  ECG_KeyInput key;
  ECG_KeyEvent event;

  ECG_KeyInput_Init(&key);
  for (uint32_t ms = 0U; ms <= ECG_KEY_DEBOUNCE_MS; ++ms)
  {
    event = ECG_KeyInput_Update(&key, ms, 1U);
    CHECK_EVENT(event, ECG_KEY_EVENT_NONE);
  }

  event = ECG_KeyInput_Update(&key, ECG_KEY_LONG_PRESS_MS + ECG_KEY_DEBOUNCE_MS, 1U);
  CHECK_EVENT(event, ECG_KEY_EVENT_LONG_PRESS);
  event = ECG_KeyInput_Update(&key, ECG_KEY_LONG_PRESS_MS + ECG_KEY_DEBOUNCE_MS + ECG_KEY_REPEAT_MS, 1U);
  CHECK_EVENT(event, ECG_KEY_EVENT_REPEAT);
  return 0;
}

static int TestThresholdStepHelper(void)
{
  float32_t current = 0.10f;

  CHECK_FLOAT_EQUAL(ECG_KeyInput_ApplyStep(current, ECG_KEY_EVENT_SHORT_PRESS, 1U),
                    0.15f);
  CHECK_FLOAT_EQUAL(ECG_KeyInput_ApplyStep(current, ECG_KEY_EVENT_SHORT_PRESS, 0U),
                    0.05f);
  CHECK_FLOAT_EQUAL(ECG_KeyInput_ApplyStep(current, ECG_KEY_EVENT_LONG_PRESS, 1U),
                    0.20f);
  CHECK_FLOAT_EQUAL(ECG_KeyInput_ApplyStep(current, ECG_KEY_EVENT_REPEAT, 0U),
                    0.0f);
  return 0;
}

int main(void)
{
  if (TestShortPressAndDebounce() != 0)
  {
    return 1;
  }
  if (TestLongPressRepeat() != 0)
  {
    return 1;
  }
  if (TestThresholdStepHelper() != 0)
  {
    return 1;
  }

  printf("all stage 4 key checks passed\n");
  return 0;
}
