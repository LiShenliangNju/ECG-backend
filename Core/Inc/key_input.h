#ifndef KEY_INPUT_H
#define KEY_INPUT_H

#include "arm_math.h"

#include <stdint.h>

#define ECG_KEY_DEBOUNCE_MS 20U
#define ECG_KEY_STEP_MV 0.1695f

typedef enum
{
  ECG_KEY_EVENT_NONE = 0U,
  ECG_KEY_EVENT_PRESS = 1U
} ECG_KeyEvent;

typedef enum
{
  ECG_KEY_STATE_RELEASED = 0U,
  ECG_KEY_STATE_DEBOUNCE_PRESS = 1U,
  ECG_KEY_STATE_PRESSED = 2U,
  ECG_KEY_STATE_DEBOUNCE_RELEASE = 3U
} ECG_KeyState;

typedef struct
{
  uint32_t last_change_ms;
  uint8_t debounced_level;
  uint8_t state;
} ECG_KeyInput;

void ECG_KeyInput_Init(ECG_KeyInput *key);
ECG_KeyEvent ECG_KeyInput_Update(ECG_KeyInput *key,
                                uint32_t now_ms,
                                uint8_t raw_pressed);
uint8_t ECG_KeyInput_IsPressed(const ECG_KeyInput *key);

static inline float32_t ECG_KeyInput_GetStepMv(ECG_KeyEvent event)
{
  return (event == ECG_KEY_EVENT_PRESS) ? ECG_KEY_STEP_MV : 0.0f;
}

static inline float32_t ECG_KeyInput_ApplyStep(float32_t current_mv,
                                               ECG_KeyEvent event,
                                               uint8_t increase)
{
  const float32_t step_mv = ECG_KeyInput_GetStepMv(event);
  if (increase != 0U)
  {
    return current_mv + step_mv;
  }
  return current_mv - step_mv;
}

#endif
