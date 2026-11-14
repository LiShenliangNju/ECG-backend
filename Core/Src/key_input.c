#include "key_input.h"

void ECG_KeyInput_Init(ECG_KeyInput *key)
{
  if (key == NULL)
  {
    return;
  }

  key->last_change_ms = 0U;
  key->press_start_ms = 0U;
  key->repeat_due_ms = 0U;
  key->debounced_level = 0U;
  key->state = ECG_KEY_STATE_RELEASED;
  key->long_press_armed = 0U;
}

uint8_t ECG_KeyInput_IsPressed(const ECG_KeyInput *key)
{
  if (key == NULL)
  {
    return 0U;
  }

  return key->debounced_level;
}

ECG_KeyEvent ECG_KeyInput_Update(ECG_KeyInput *key,
                                uint32_t now_ms,
                                uint8_t raw_pressed)
{
  ECG_KeyEvent event = ECG_KEY_EVENT_NONE;

  if (key == NULL)
  {
    return event;
  }

  switch (key->state)
  {
    case ECG_KEY_STATE_RELEASED:
      if (raw_pressed != 0U)
      {
        key->state = ECG_KEY_STATE_DEBOUNCE_PRESS;
        key->last_change_ms = now_ms;
        key->debounced_level = 0U;
      }
      break;

    case ECG_KEY_STATE_DEBOUNCE_PRESS:
      if (raw_pressed == 0U)
      {
        key->state = ECG_KEY_STATE_RELEASED;
        key->last_change_ms = now_ms;
        key->debounced_level = 0U;
      }
      else if ((uint32_t)(now_ms - key->last_change_ms) >= ECG_KEY_DEBOUNCE_MS)
      {
        key->state = ECG_KEY_STATE_PRESSED;
        key->press_start_ms = now_ms;
        key->repeat_due_ms = now_ms + ECG_KEY_REPEAT_MS;
        key->debounced_level = 1U;
        key->long_press_armed = 0U;
      }
      break;

    case ECG_KEY_STATE_PRESSED:
      if (raw_pressed == 0U)
      {
        key->state = ECG_KEY_STATE_DEBOUNCE_RELEASE;
        key->last_change_ms = now_ms;
        key->debounced_level = 0U;
      }
      else
      {
        if ((key->long_press_armed == 0U) &&
            ((uint32_t)(now_ms - key->press_start_ms) >= ECG_KEY_LONG_PRESS_MS))
        {
          key->long_press_armed = 1U;
          key->repeat_due_ms = now_ms + ECG_KEY_REPEAT_MS;
          event = ECG_KEY_EVENT_LONG_PRESS;
        }
        else if (key->long_press_armed != 0U)
        {
          if (now_ms >= key->repeat_due_ms)
          {
            key->repeat_due_ms = now_ms + ECG_KEY_REPEAT_MS;
            event = ECG_KEY_EVENT_REPEAT;
          }
        }
      }
      break;

    case ECG_KEY_STATE_DEBOUNCE_RELEASE:
      if (raw_pressed != 0U)
      {
        key->state = ECG_KEY_STATE_PRESSED;
        key->last_change_ms = now_ms;
        key->debounced_level = 1U;
      }
      else if ((uint32_t)(now_ms - key->last_change_ms) >= ECG_KEY_DEBOUNCE_MS)
      {
        key->state = ECG_KEY_STATE_RELEASED;
        key->debounced_level = 0U;
        if ((key->long_press_armed == 0U) &&
            ((uint32_t)(now_ms - key->press_start_ms) < ECG_KEY_LONG_PRESS_MS))
        {
          event = ECG_KEY_EVENT_SHORT_PRESS;
        }
      }
      break;

    default:
      key->state = ECG_KEY_STATE_RELEASED;
      break;
  }

  return event;
}
