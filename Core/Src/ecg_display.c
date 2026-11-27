#include "ecg_display.h"

#include <math.h>
#include <string.h>

#include "spi.h"

/*
 * Data flow: foreground ECG samples (filtered mV) -> 2400-sample ring history
 * -> ten-sample min/max envelope -> pending-column queue -> asynchronous
 * foreground SPI-DMA renderer. Metrics use a separate deferred refresh. No
 * display state is shared with an ISR; only DMA status is polled by the main loop.
 *
 * Plot invariants: write_column and erase_column wrap over 240 columns, with
 * erase_column initialized 30 columns ahead of write_column. Each complete group
 * of ten input samples advances both cursors once. samples_in_column preserves
 * the grouping phase independently of the wrapping sample counter.
 * The LCD plot occupies y=14..193; its vertical mapping assumes a +/-1.5 mV
 * display range, with out-of-range finite samples saturated at the limits.
 */

#define ECG_DISPLAY_DMA_BUFFER_BYTES 1024U
#define ECG_DISPLAY_DMA_TIMEOUT_MS 20U
#define ECG_DISPLAY_COLUMN_DMA_BYTES (ECG_DISPLAY_WAVEFORM_H * 2U)
#define ECG_DISPLAY_DMA_STAGE_ERASE 0U
#define ECG_DISPLAY_DMA_STAGE_DRAW 1U

static uint8_t g_display_dma_buffer[ECG_DISPLAY_DMA_BUFFER_BYTES];
static uint8_t g_display_column_dma_buffer[ECG_DISPLAY_COLUMN_DMA_BYTES];

/* Keeps the full-panel DMA source alive until every chunk has completed. */
static ECG_DisplayStatus ECG_Display_FillRectDMA(uint16_t x_start,
                                                 uint16_t y_start,
                                                 uint16_t x_end,
                                                 uint16_t y_end,
                                                 uint16_t color)
{
  if ((x_start >= x_end) || (y_start >= y_end) ||
      (x_end > ECG_DISPLAY_WIDTH) || (y_end > ECG_DISPLAY_HEIGHT))
  {
    return ECG_DISPLAY_STATUS_INVALID_ARGUMENT;
  }

  for (uint32_t index = 0U; index < ECG_DISPLAY_DMA_BUFFER_BYTES; index += 2U)
  {
    g_display_dma_buffer[index] = (uint8_t)(color >> 8U);
    g_display_dma_buffer[index + 1U] = (uint8_t)color;
  }

  LCD_Address_Set(x_start, y_start, x_end - 1U, y_end - 1U);
  LCD_DC_Set();

  uint32_t bytes_remaining =
      (uint32_t)(x_end - x_start) * (uint32_t)(y_end - y_start) * 2U;
  while (bytes_remaining > 0U)
  {
    const uint16_t transfer_size =
        (bytes_remaining > ECG_DISPLAY_DMA_BUFFER_BYTES)
            ? (uint16_t)ECG_DISPLAY_DMA_BUFFER_BYTES
            : (uint16_t)bytes_remaining;
    if (HAL_SPI_Transmit_DMA(&hspi5, g_display_dma_buffer, transfer_size) != HAL_OK)
    {
      return ECG_DISPLAY_STATUS_DMA_ERROR;
    }

    const uint32_t transfer_start = HAL_GetTick();
    while (HAL_SPI_GetState(&hspi5) != HAL_SPI_STATE_READY)
    {
      if (HAL_SPI_GetState(&hspi5) == HAL_SPI_STATE_ERROR)
      {
        return ECG_DISPLAY_STATUS_DMA_ERROR;
      }
      if ((uint32_t)(HAL_GetTick() - transfer_start) >=
          ECG_DISPLAY_DMA_TIMEOUT_MS)
      {
        if (HAL_SPI_Abort(&hspi5) != HAL_OK)
        {
          return ECG_DISPLAY_STATUS_DMA_ERROR;
        }
        return ECG_DISPLAY_STATUS_DMA_TIMEOUT;
      }
    }

    if (HAL_SPI_GetError(&hspi5) != HAL_SPI_ERROR_NONE)
    {
      return ECG_DISPLAY_STATUS_DMA_ERROR;
    }
    bytes_remaining -= transfer_size;
  }

  return ECG_DISPLAY_STATUS_OK;
}

/* Foreground helper: paints the background and fixed frame before plotting. */
static ECG_DisplayStatus ECG_Display_DrawGrid(void)
{
  ECG_DisplayStatus status =
      ECG_Display_FillRectDMA(0U, 0U, ECG_DISPLAY_WIDTH, ECG_DISPLAY_HEIGHT,
                              ECG_DISPLAY_BG_COLOR);
  if (status != ECG_DISPLAY_STATUS_OK)
  {
    return status;
  }

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

  return ECG_DISPLAY_STATUS_OK;
}

/* The labels occupy the 16-pixel margin above the plot. */
static void ECG_Display_DrawScaleLabels(void)
{
  LCD_ShowString(0U, 0U,
                 (const uint8_t *)"X:200ms/div Y:0.339mV/div",
                 WHITE, BLACK, 16U, 1U);
}

/* Maps zero to the center axis and +/-1.5 mV to the plot's vertical limits. */
static uint16_t ECG_Display_MapY(float32_t sample_mv)
{
  const float32_t y_range = 1.5f;
  float32_t clamped = sample_mv;
  if (clamped < -y_range)
  {
    clamped = -y_range;
  }
  else if (clamped > y_range)
  {
    clamped = y_range;
  }

  const float32_t normalized = (clamped + y_range) / (2.0f * y_range);
  return (uint16_t)(ECG_DISPLAY_WAVEFORM_Y +
                    (1.0f - normalized) *
                        (float32_t)(ECG_DISPLAY_WAVEFORM_H - 1U));
}

/* Builds one complete vertical strip so plot updates require a single DMA transfer. */
static void ECG_Display_BuildColumn(uint16_t column,
                                    uint16_t min_y,
                                    uint16_t max_y,
                                    uint8_t draw_trace)
{
  for (uint16_t offset = 0U; offset < ECG_DISPLAY_WAVEFORM_H; ++offset)
  {
    const uint16_t y = ECG_DISPLAY_WAVEFORM_Y + offset;
    uint16_t color = ECG_DISPLAY_BG_COLOR;

    if (((column % 20U) == 0U) || (column == ECG_DISPLAY_WIDTH - 1U) ||
        (offset == 0U) || (offset == ECG_DISPLAY_WAVEFORM_H - 1U) ||
        ((offset % 20U) == 0U))
    {
      color = ECG_DISPLAY_GRID_COLOR;
    }
    if (y == ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H / 2U)
    {
      color = ECG_DISPLAY_AXIS_COLOR;
    }

    if ((draw_trace != 0U) &&
        (y >= ((min_y < max_y) ? min_y : max_y)) &&
        (y <= ((min_y > max_y) ? min_y : max_y)))
    {
      color = ECG_DISPLAY_TRACE_COLOR;
    }

    g_display_column_dma_buffer[offset * 2U] = (uint8_t)(color >> 8U);
    g_display_column_dma_buffer[offset * 2U + 1U] = (uint8_t)color;
  }
}

static void ECG_Display_DrawMetricsNow(const ECG_DisplayState *state)
{
  LCD_Fill(64U, 220U, 112U, 240U, BLACK);
  LCD_ShowIntNum(64U, 220U, (uint16_t)state->latest_bpm, 3U, RED, BLACK, 16U);
  LCD_Fill(144U, 220U, 204U, 240U, BLACK);
  LCD_ShowFloatNum1(144U, 220U, state->latest_peak_to_peak_mv, 3U,
                    CYAN, BLACK, 16U);
}

/* Foreground: prepares the caller-owned state; LCD hardware is untouched. */
void ECG_Display_Init(ECG_DisplayState *state)
{
  if (state == NULL)
  {
    return;
  }

  memset(state, 0, sizeof(*state));
  state->write_column = 0U;
  state->erase_column = ECG_DISPLAY_ERASE_LEAD_COLUMNS;
  state->initialized = 0U;
  state->latest_bpm = 0.0f;
  state->latest_peak_to_peak_mv = 0.0f;
}

/* Foreground: clears the panel only when no queued column transfer can race it. */
ECG_DisplayStatus ECG_Display_Clear(ECG_DisplayState *state)
{
  if (state == NULL)
  {
    return ECG_DISPLAY_STATUS_INVALID_ARGUMENT;
  }
  if ((state->pending_count != 0U) || (state->dma_active != 0U))
  {
    return ECG_DISPLAY_STATUS_BUSY;
  }

  const ECG_DisplayStatus status =
      ECG_Display_FillRectDMA(0U, 0U, ECG_DISPLAY_WIDTH, ECG_DISPLAY_HEIGHT,
                              ECG_DISPLAY_BG_COLOR);
  if (status != ECG_DISPLAY_STATUS_OK)
  {
    return status;
  }

  state->write_column = 0U;
  state->erase_column = ECG_DISPLAY_ERASE_LEAD_COLUMNS;
  state->samples_processed = 0U;
  state->samples_in_column = 0U;
  state->history_head = 0U;
  state->history_count = 0U;
  state->pending_head = 0U;
  state->pending_tail = 0U;
  state->dma_stage = ECG_DISPLAY_DMA_STAGE_ERASE;
  state->metrics_dirty = 0U;
  state->latest_bpm = 0.0f;
  state->latest_peak_to_peak_mv = 0.0f;
  state->initialized = 0U;
  return ECG_DISPLAY_STATUS_OK;
}

/* Foreground: draws the frame and labels, then marks it ready for updates. */
ECG_DisplayStatus ECG_Display_DrawStaticFrame(ECG_DisplayState *state)
{
  if (state == NULL)
  {
    return ECG_DISPLAY_STATUS_INVALID_ARGUMENT;
  }
  if ((state->pending_count != 0U) || (state->dma_active != 0U))
  {
    return ECG_DISPLAY_STATUS_BUSY;
  }

  const ECG_DisplayStatus status = ECG_Display_DrawGrid();
  if (status != ECG_DISPLAY_STATUS_OK)
  {
    return status;
  }

  ECG_Display_DrawScaleLabels();
  LCD_ShowString(8U, 204U, (const uint8_t *)"ECG", RED, BLACK, 16U, 0U);
  LCD_ShowString(64U, 204U, (const uint8_t *)"BPM", GREEN, BLACK, 16U, 0U);
  LCD_ShowString(144U, 204U, (const uint8_t *)"mVpp", CYAN, BLACK, 16U, 0U);
  ECG_Display_DrawMetricsNow(state);
  state->metrics_dirty = 0U;
  state->initialized = 1U;
  return ECG_DISPLAY_STATUS_OK;
}

/*
 * Foreground producer. History persists between calls so partial ten-sample
 * groups are completed by later calls; completed min/max columns are queued
 * for ECG_Display_Service instead of being rendered in this sample-block path.
 */
ECG_DisplayStatus ECG_Display_UpdateWaveform(ECG_DisplayState *state,
                                             const float32_t *samples,
                                             uint32_t sample_count)
{
  if ((state == NULL) || ((sample_count > 0U) && (samples == NULL)))
  {
    return ECG_DISPLAY_STATUS_INVALID_ARGUMENT;
  }

  if (sample_count == 0U)
  {
    return ECG_DISPLAY_STATUS_OK;
  }

  for (uint32_t index = 0U; index < sample_count; ++index)
  {
    if (!isfinite(samples[index]))
    {
      return ECG_DISPLAY_STATUS_INVALID_SAMPLE;
    }
  }

  if (state->initialized == 0U)
  {
    const ECG_DisplayStatus status = ECG_Display_DrawStaticFrame(state);
    if (status != ECG_DISPLAY_STATUS_OK)
    {
      return status;
    }
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

    state->samples_in_column++;
    if (state->samples_in_column == ECG_DISPLAY_SAMPLES_PER_COLUMN)
    {
      state->samples_in_column = 0U;
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
      if (state->pending_count >= ECG_DISPLAY_PENDING_COLUMN_CAPACITY)
      {
        return ECG_DISPLAY_STATUS_QUEUE_FULL;
      }

      ECG_DisplayPendingColumn *pending =
          &state->pending_columns[state->pending_tail];
      pending->write_column = column;
      pending->erase_column = state->erase_column;
      const uint16_t first_y = ECG_Display_MapY(min_value);
      const uint16_t second_y = ECG_Display_MapY(max_value);
      pending->min_y = (first_y < second_y) ? first_y : second_y;
      pending->max_y = (first_y > second_y) ? first_y : second_y;
      state->pending_tail =
          (uint8_t)((state->pending_tail + 1U) %
                    ECG_DISPLAY_PENDING_COLUMN_CAPACITY);
      state->pending_count++;
      state->write_column = (state->write_column + 1U) % ECG_DISPLAY_WIDTH;
      state->erase_column = (state->erase_column + 1U) % ECG_DISPLAY_WIDTH;
    }
    state->samples_processed++;
  }

  return ECG_DISPLAY_STATUS_OK;
}

/* Foreground producer: records metrics for the renderer without drawing. */
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
  state->metrics_dirty = 1U;
}

/*
 * Foreground renderer: retires a completed transfer, then starts at most one
 * erase/draw strip. The shared DMA buffer is untouched until HAL reports ready.
 */
ECG_DisplayStatus ECG_Display_Service(ECG_DisplayState *state)
{
  if (state == NULL)
  {
    return ECG_DISPLAY_STATUS_INVALID_ARGUMENT;
  }

  if (state->dma_active != 0U)
  {
    const HAL_SPI_StateTypeDef spi_state = HAL_SPI_GetState(&hspi5);
    if (spi_state != HAL_SPI_STATE_READY)
    {
      if (spi_state == HAL_SPI_STATE_ERROR)
      {
        state->dma_active = 0U;
        (void)HAL_SPI_Abort(&hspi5);
        return ECG_DISPLAY_STATUS_DMA_ERROR;
      }
      if ((uint32_t)(HAL_GetTick() - state->dma_started_tick) >=
          ECG_DISPLAY_DMA_TIMEOUT_MS)
      {
        state->dma_active = 0U;
        if (HAL_SPI_Abort(&hspi5) != HAL_OK)
        {
          return ECG_DISPLAY_STATUS_DMA_ERROR;
        }
        return ECG_DISPLAY_STATUS_DMA_TIMEOUT;
      }
      return ECG_DISPLAY_STATUS_OK;
    }
    if (HAL_SPI_GetError(&hspi5) != HAL_SPI_ERROR_NONE)
    {
      state->dma_active = 0U;
      return ECG_DISPLAY_STATUS_DMA_ERROR;
    }

    state->dma_active = 0U;
    if (state->dma_stage == ECG_DISPLAY_DMA_STAGE_ERASE)
    {
      state->dma_stage = ECG_DISPLAY_DMA_STAGE_DRAW;
    }
    else
    {
      state->pending_head =
          (uint8_t)((state->pending_head + 1U) %
                    ECG_DISPLAY_PENDING_COLUMN_CAPACITY);
      state->pending_count--;
      state->dma_stage = ECG_DISPLAY_DMA_STAGE_ERASE;
    }
  }

  if (state->pending_count > 0U)
  {
    const ECG_DisplayPendingColumn *pending =
        &state->pending_columns[state->pending_head];
    const uint8_t draw_trace =
        (state->dma_stage == ECG_DISPLAY_DMA_STAGE_DRAW) ? 1U : 0U;
    const uint16_t column =
        (draw_trace != 0U) ? pending->write_column : pending->erase_column;
    ECG_Display_BuildColumn(column, pending->min_y, pending->max_y,
                            draw_trace);
    LCD_Address_Set(column, ECG_DISPLAY_WAVEFORM_Y,
                    column,
                    ECG_DISPLAY_WAVEFORM_Y + ECG_DISPLAY_WAVEFORM_H - 1U);
    LCD_DC_Set();

    if (HAL_SPI_Transmit_DMA(&hspi5, g_display_column_dma_buffer,
                             ECG_DISPLAY_COLUMN_DMA_BYTES) != HAL_OK)
    {
      return ECG_DISPLAY_STATUS_DMA_ERROR;
    }
    state->dma_active = 1U;
    state->dma_started_tick = HAL_GetTick();
    return ECG_DISPLAY_STATUS_OK;
  }

  if (state->metrics_dirty != 0U)
  {
    ECG_Display_DrawMetricsNow(state);
    state->metrics_dirty = 0U;
  }
  return ECG_DISPLAY_STATUS_OK;
}
