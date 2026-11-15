#include "ecg_display.h"

#include <string.h>

#include "delay.h"

static void ECG_Display_DrawGrid(void)
{
  LCD_Fill(0U, 0U, ECG_DISPLAY_WIDTH, ECG_DISPLAY_HEIGHT, ECG_DISPLAY_BG_COLOR);

  LCD_DrawRectangle(0U, ECG_DISPLAY_WAVEFORM_Y,
                    ECG_DISPLAY_WIDTH - 1U,
                    ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H - 1U,
                    ECG_DISPLAY_GRID_COLOR);

  for (uint16_t x = 0U; x < ECG_DISPLAY_WIDTH; x += 20U)
  {
    LCD_DrawLine(x, ECG_DISPLAY_WAVEFORM_Y,
                 x, ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H - 1U,
                 ECG_DISPLAY_GRID_COLOR);
  }

  for (uint16_t y = ECG_DISPLAY_WAVEFORM_Y; y < ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H; y += 20U)
  {
    LCD_DrawLine(0U, y,
                 ECG_DISPLAY_WIDTH - 1U, y,
                 ECG_DISPLAY_GRID_COLOR);
  }

  LCD_DrawLine(0U, ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H / 2U,
               ECG_DISPLAY_WIDTH - 1U, ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H / 2U,
               ECG_DISPLAY_AXIS_COLOR);

  LCD_Fill(0U, ECG_DISPLAY_TEXT_Y, ECG_DISPLAY_WIDTH, ECG_DISPLAY_HEIGHT,
           ECG_DISPLAY_BG_COLOR);
}

static uint16_t ECG_Display_MapY(float32_t sample_mv)
{
  const float32_t y_range = 1.5f;
  const float32_t clamped = sample_mv;
  const float32_t normalized = (clamped + y_range) / (2.0f * y_range);
  uint16_t y = (uint16_t)(ECG_DISPLAY_WAVEFORM_Y +
                          (1.0f - normalized) * (float32_t)(ECG_DISPLAY_WAVEFORM_H - 1U));

  if (y < ECG_DISPLAY_WAVEFORM_Y)
  {
    y = ECG_DISPLAY_WAVEFORM_Y;
  }
  if (y >= ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H)
  {
    y = ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H - 1U;
  }

  return y;
}

static void ECG_Display_EraseColumn(ECG_DisplayState *state, uint16_t column)
{
  if (column >= ECG_DISPLAY_WIDTH)
  {
    return;
  }

  LCD_Fill(column, ECG_DISPLAY_WAVEFORM_Y,
           column + 1U, ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H,
           ECG_DISPLAY_BG_COLOR);

  if ((column == 0U) || (column == ECG_DISPLAY_WIDTH - 1U))
  {
    LCD_DrawLine(column, ECG_DISPLAY_WAVEFORM_Y,
                 column, ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H - 1U,
                 ECG_DISPLAY_GRID_COLOR);
  }
}

static void ECG_Display_DrawColumn(uint16_t column,
                                   const float32_t *samples,
                                   uint32_t sample_count)
{
  float32_t min_value = samples[0];
  float32_t max_value = samples[0];

  for (uint32_t index = 1U; index < sample_count; ++index)
  {
    if (samples[index] < min_value)
    {
      min_value = samples[index];
    }
    if (samples[index] > max_value)
    {
      max_value = samples[index];
    }
  }

  const uint16_t x = column;
  const uint16_t y_min = ECG_Display_MapY(min_value);
  const uint16_t y_max = ECG_Display_MapY(max_value);

  if (y_min < y_max)
  {
    LCD_DrawLine(x, y_min, x, y_max, ECG_DISPLAY_TRACE_COLOR);
  }
  else
  {
    LCD_DrawPoint(x, y_min, ECG_DISPLAY_TRACE_COLOR);
  }
}

static void ECG_Display_HandleColumnUpdate(ECG_DisplayState *state,
                                          uint16_t column,
                                          float32_t min_value,
                                          float32_t max_value)
{
  const uint16_t y_min = ECG_Display_MapY(min_value);
  const uint16_t y_max = ECG_Display_MapY(max_value);

  ECG_Display_EraseColumn(state, state->erase_column);

  if (y_min < y_max)
  {
    LCD_DrawLine(column, y_min, column, y_max, ECG_DISPLAY_TRACE_COLOR);
  }
  else
  {
    LCD_DrawPoint(column, y_min, ECG_DISPLAY_TRACE_COLOR);
  }

  state->write_column = (state->write_column + 1U) % ECG_DISPLAY_WIDTH;
  state->erase_column = (state->erase_column + 1U) % ECG_DISPLAY_WIDTH;
}

void ECG_Display_Init(ECG_DisplayState *state)
{
  if (state == NULL)
  {
    return;
  }

  memset(state, 0, sizeof(*state));
  state->write_column = 0U;
  state->erase_column = ECG_DISPLAY_COLUMN_COUNT - ECG_DISPLAY_ERASE_LAG_COLUMNS;
  state->initialized = 0U;
  state->latest_bpm = 0.0f;
  state->latest_peak_to_peak_mv = 0.0f;
}

void ECG_Display_Clear(ECG_DisplayState *state)
{
  if (state == NULL)
  {
    return;
  }

  state->write_column = 0U;
  state->erase_column = ECG_DISPLAY_COLUMN_COUNT - ECG_DISPLAY_ERASE_LAG_COLUMNS;
  state->samples_processed = 0U;
  state->history_head = 0U;
  state->history_count = 0U;
  LCD_Fill(0U, 0U, ECG_DISPLAY_WIDTH, ECG_DISPLAY_HEIGHT, ECG_DISPLAY_BG_COLOR);
}

void ECG_Display_DrawStaticFrame(ECG_DisplayState *state)
{
  (void)state;
  ECG_Display_DrawGrid();
  LCD_ShowString(8U, 8U, (const uint8_t *)"ECG", RED, BLACK, 2U, 0U);
  LCD_ShowString(150U, 8U, (const uint8_t *)"BPM", GREEN, BLACK, 2U, 0U);
  LCD_ShowString(150U, 36U, (const uint8_t *)"mVpp", CYAN, BLACK, 2U, 0U);
}

void ECG_Display_UpdateWaveform(ECG_DisplayState *state,
                               const float32_t *samples,
                               uint32_t sample_count)
{
  if ((state == NULL) || (samples == NULL) || (sample_count == 0U))
  {
    return;
  }

  if (state->initialized == 0U)
  {
    ECG_Display_DrawStaticFrame(state);
    state->initialized = 1U;
  }

  for (uint32_t index = 0U; index < sample_count; ++index)
  {
    const uint32_t history_index = state->history_head;
    state->history[history_index] = samples[index];
    state->history_head = (state->history_head + 1U) % ECG_DISPLAY_HISTORY_SAMPLES;
    if (state->history_count < ECG_DISPLAY_HISTORY_SAMPLES)
    {
      state->history_count++;
    }

    const uint32_t sample_block_id = state->samples_processed + 1U;
    if ((sample_block_id % ECG_DISPLAY_SAMPLES_PER_COLUMN) == 0U)
    {
      float32_t min_value = samples[index];
      float32_t max_value = samples[index];

      for (uint32_t block_index = 1U; block_index < ECG_DISPLAY_SAMPLES_PER_COLUMN; ++block_index)
      {
        const uint32_t offset = (state->history_head + ECG_DISPLAY_HISTORY_SAMPLES - ECG_DISPLAY_SAMPLES_PER_COLUMN + block_index) % ECG_DISPLAY_HISTORY_SAMPLES;
        const float32_t value = state->history[offset];
        if (value < min_value)
        {
          min_value = value;
        }
        if (value > max_value)
        {
          max_value = value;
        }
      }

      const uint16_t column = state->write_column;
      if (column < ECG_DISPLAY_WIDTH)
      {
        ECG_Display_HandleColumnUpdate(state, column, min_value, max_value);
      }
    }
    state->samples_processed++;
  }
}

void ECG_Display_UpdateMetrics(ECG_DisplayState *state,
                              uint32_t heart_rate_bpm,
                              float32_t peak_to_peak_mv)
{
  if (state == NULL)
  {
    return;
  }

  state->latest_bpm = (float32_t)heart_rate_bpm;
  state->latest_peak_to_peak_mv = peak_to_peak_mv;

  LCD_Fill(150U, 20U, 240U, 60U, BLACK);
  LCD_ShowIntNum(150U, 20U, heart_rate_bpm, 3U, RED, BLACK, 2U);
  LCD_Fill(150U, 48U, 240U, 92U, BLACK);
  LCD_ShowFloatNum1(150U, 48U, peak_to_peak_mv, 3U, CYAN, BLACK, 2U);
}
