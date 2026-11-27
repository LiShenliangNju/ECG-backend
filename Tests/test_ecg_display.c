#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ecg_display.h"
#include "spi.h"

#define CHECK_TRUE(condition)                                              \
  do                                                                       \
  {                                                                        \
    if (!(condition))                                                      \
    {                                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
      return 1;                                                            \
    }                                                                      \
  } while (0)

typedef struct
{
  uint16_t x;
  uint16_t top_color;
  uint16_t grid_color;
  uint16_t axis_color;
  uint16_t bottom_color;
  uint16_t threshold_y;
  uint16_t trace_pixel_count;
} ColumnTransfer;

static ColumnTransfer g_column_transfers[512];
static uint32_t g_column_transfer_count;
static uint32_t g_dma_call_count;
static uint32_t g_dma_bytes;
static uint32_t g_tick;
static uint8_t g_dma_fail;
static uint8_t g_dma_never_completes;
static uint8_t g_dma_data_is_black;
static uint8_t g_last_string_size;
static uint8_t g_last_int_size;
static uint8_t g_last_float_size;
static uint16_t g_scale_label_x;
static uint16_t g_scale_label_y;
static uint8_t g_scale_label_mode;
static char g_scale_label[32];
static uint16_t g_red_line_y;
static uint32_t g_red_line_count;
static uint16_t g_address_x;
SPI_HandleTypeDef hspi5;

static void ResetMocks(void)
{
  memset(g_column_transfers, 0, sizeof(g_column_transfers));
  g_column_transfer_count = 0U;
  g_dma_call_count = 0U;
  g_dma_bytes = 0U;
  g_tick = 0U;
  g_dma_fail = 0U;
  g_dma_never_completes = 0U;
  g_dma_data_is_black = 1U;
  g_last_string_size = 0U;
  g_last_int_size = 0U;
  g_last_float_size = 0U;
  g_scale_label_x = UINT16_MAX;
  g_scale_label_y = UINT16_MAX;
  g_scale_label_mode = 0U;
  g_scale_label[0] = '\0';
  g_red_line_y = UINT16_MAX;
  g_red_line_count = 0U;
  g_address_x = 0U;
  hspi5.state = HAL_SPI_STATE_READY;
  hspi5.error = HAL_SPI_ERROR_NONE;
}

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *spi,
                                       const uint8_t *data,
                                       uint16_t size)
{
  if (g_dma_fail != 0U)
  {
    return HAL_ERROR;
  }

  g_dma_call_count++;
  g_dma_bytes += size;
  for (uint16_t index = 0U; index < size; ++index)
  {
    if (data[index] != 0U)
    {
      g_dma_data_is_black = 0U;
    }
  }

  if ((size == ECG_DISPLAY_WAVEFORM_H * 2U) &&
      (g_column_transfer_count <
       (sizeof(g_column_transfers) / sizeof(g_column_transfers[0]))))
  {
    ColumnTransfer *transfer =
        &g_column_transfers[g_column_transfer_count++];
    transfer->x = g_address_x;
    transfer->top_color = (uint16_t)((uint16_t)data[0] << 8U) | data[1];
    transfer->grid_color =
        (uint16_t)((uint16_t)data[20U * 2U] << 8U) | data[20U * 2U + 1U];
    transfer->axis_color =
        (uint16_t)((uint16_t)data[(ECG_DISPLAY_WAVEFORM_H / 2U) * 2U] << 8U) |
        data[(ECG_DISPLAY_WAVEFORM_H / 2U) * 2U + 1U];
    transfer->bottom_color =
        (uint16_t)((uint16_t)data[(ECG_DISPLAY_WAVEFORM_H - 1U) * 2U] << 8U) |
        data[(ECG_DISPLAY_WAVEFORM_H - 1U) * 2U + 1U];
    transfer->threshold_y = UINT16_MAX;
    for (uint16_t offset = 0U; offset < ECG_DISPLAY_WAVEFORM_H; ++offset)
    {
      const uint16_t color =
          (uint16_t)((uint16_t)data[offset * 2U] << 8U) |
          data[offset * 2U + 1U];
      if (color == ECG_DISPLAY_THRESHOLD_COLOR)
      {
        transfer->threshold_y = ECG_DISPLAY_WAVEFORM_Y + offset;
      }
      if (color == ECG_DISPLAY_TRACE_COLOR)
      {
        transfer->trace_pixel_count++;
      }
    }
  }

  spi->state = (g_dma_never_completes != 0U)
                   ? HAL_SPI_STATE_BUSY
                   : HAL_SPI_STATE_READY;
  return HAL_OK;
}

HAL_SPI_StateTypeDef HAL_SPI_GetState(const SPI_HandleTypeDef *spi)
{
  return spi->state;
}

uint32_t HAL_SPI_GetError(const SPI_HandleTypeDef *spi)
{
  return spi->error;
}

HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi)
{
  spi->state = HAL_SPI_STATE_READY;
  return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
  return g_tick++;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  (void)port;
  (void)pin;
  (void)state;
}

void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  g_address_x = x1;
  (void)y1;
  (void)x2;
  (void)y2;
}

void LCD_Fill(uint16_t x_start, uint16_t y_start,
              uint16_t x_end, uint16_t y_end, uint16_t color)
{
  (void)x_start;
  (void)y_start;
  (void)x_end;
  (void)y_end;
  (void)color;
}

void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
  (void)x;
  (void)y;
  (void)color;
}

void LCD_DrawLine(uint16_t x1, uint16_t y1,
                  uint16_t x2, uint16_t y2, uint16_t color)
{
  if ((color == ECG_DISPLAY_THRESHOLD_COLOR) && (y1 == y2))
  {
    g_red_line_y = y1;
    g_red_line_count++;
  }
  (void)x1;
  (void)x2;
}

void LCD_DrawRectangle(uint16_t x1, uint16_t y1,
                       uint16_t x2, uint16_t y2, uint16_t color)
{
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
  (void)color;
}

void LCD_ShowString(uint16_t x, uint16_t y, const uint8_t *text,
                    uint16_t foreground, uint16_t background,
                    uint8_t size, uint8_t mode)
{
  (void)foreground;
  (void)background;
  g_last_string_size = size;
  if ((x == 0U) && (y == 0U))
  {
    g_scale_label_x = x;
    g_scale_label_y = y;
    g_scale_label_mode = mode;
    strncpy(g_scale_label, (const char *)text, sizeof(g_scale_label) - 1U);
    g_scale_label[sizeof(g_scale_label) - 1U] = '\0';
  }
}

void LCD_ShowIntNum(uint16_t x, uint16_t y, uint16_t value, uint8_t length,
                    uint16_t foreground, uint16_t background, uint8_t size)
{
  (void)x;
  (void)y;
  (void)value;
  (void)length;
  (void)foreground;
  (void)background;
  g_last_int_size = size;
}

void LCD_ShowFloatNum1(uint16_t x, uint16_t y, float value, uint8_t length,
                       uint16_t foreground, uint16_t background, uint8_t size)
{
  (void)x;
  (void)y;
  (void)value;
  (void)length;
  (void)foreground;
  (void)background;
  g_last_float_size = size;
}

static int DrainRenderer(ECG_DisplayState *state)
{
  for (uint32_t iteration = 0U; iteration < 256U; ++iteration)
  {
    if (ECG_Display_Service(state) != ECG_DISPLAY_STATUS_OK)
    {
      return 1;
    }
    if ((state->pending_count == 0U) &&
        (state->dma_active == 0U) &&
        (state->threshold_redraw_pending == 0U) &&
        (state->metrics_dirty == 0U))
    {
      return 0;
    }
  }
  return 1;
}

static int TestDmaFrameAndFontSizes(void)
{
  ECG_DisplayState state;
  ECG_Display_Init(&state);
  CHECK_TRUE(state.write_column == 0U);
  CHECK_TRUE(state.erase_column == ECG_DISPLAY_ERASE_LEAD_COLUMNS);
  ResetMocks();

  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, 0.10f) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(ECG_Display_DrawStaticFrame(&state) == ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(state.initialized == 1U);
  CHECK_TRUE(g_dma_call_count > 1U);
  CHECK_TRUE(g_dma_bytes == ECG_DISPLAY_WIDTH * ECG_DISPLAY_HEIGHT * 2U);
  CHECK_TRUE(g_dma_data_is_black == 1U);
  CHECK_TRUE(g_last_string_size == 16U);
  CHECK_TRUE(strcmp(g_scale_label, "X:200ms/div Y:0.339mV/div") == 0);
  CHECK_TRUE(g_scale_label_x == 0U);
  CHECK_TRUE(g_scale_label_y + 16U <= ECG_DISPLAY_WAVEFORM_Y);
  CHECK_TRUE(strlen(g_scale_label) * 8U <= ECG_DISPLAY_WIDTH);
  CHECK_TRUE(g_scale_label_mode == 1U);
  CHECK_TRUE(g_last_int_size == 16U);
  CHECK_TRUE(g_last_float_size == 16U);
  CHECK_TRUE(g_red_line_count == 1U);
  CHECK_TRUE(g_red_line_y == state.threshold_y);
  return 0;
}

static int TestThresholdRedrawPreservesWaveform(void)
{
  ECG_DisplayState state;
  const float32_t samples[ECG_DISPLAY_SAMPLES_PER_COLUMN] = {
    0.3f, 0.3f, 0.3f, 0.3f, 0.3f,
    0.3f, 0.3f, 0.3f, 0.3f, 0.3f
  };

  ECG_Display_Init(&state);
  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, 0.10f) ==
             ECG_DISPLAY_STATUS_OK);
  ResetMocks();
  CHECK_TRUE(ECG_Display_DrawStaticFrame(&state) == ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(ECG_Display_UpdateWaveform(&state, samples,
                                        ECG_DISPLAY_SAMPLES_PER_COLUMN) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(DrainRenderer(&state) == 0);
  CHECK_TRUE(state.rendered_trace[0] == 1U);

  const uint16_t old_threshold_y = state.threshold_y;
  ResetMocks();
  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, 0.2695f) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(state.threshold_redraw_pending == 1U);
  CHECK_TRUE(state.threshold_y != old_threshold_y);
  CHECK_TRUE(DrainRenderer(&state) == 0);
  CHECK_TRUE(g_column_transfer_count == ECG_DISPLAY_COLUMN_COUNT);
  for (uint32_t column = 0U; column < ECG_DISPLAY_COLUMN_COUNT; ++column)
  {
    CHECK_TRUE(g_column_transfers[column].threshold_y == state.threshold_y);
    CHECK_TRUE(g_column_transfers[column].threshold_y != old_threshold_y);
  }
  CHECK_TRUE(g_column_transfers[0].trace_pixel_count > 0U);

  ResetMocks();
  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, 0.439f) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(ECG_Display_Service(&state) == ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(state.dma_active == 1U);
  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, 0.6085f) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(state.threshold_redraw_restart == 1U);
  CHECK_TRUE(DrainRenderer(&state) == 0);
  CHECK_TRUE(g_column_transfer_count == ECG_DISPLAY_COLUMN_COUNT + 1U);
  for (uint32_t column = 1U;
       column < ECG_DISPLAY_COLUMN_COUNT + 1U; ++column)
  {
    CHECK_TRUE(g_column_transfers[column].threshold_y == state.threshold_y);
  }

  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, -0.01f) ==
             ECG_DISPLAY_STATUS_INVALID_ARGUMENT);
  CHECK_TRUE(ECG_Display_UpdateThreshold(&state, NAN) ==
             ECG_DISPLAY_STATUS_INVALID_SAMPLE);
  return 0;
}

static int TestCounterWrapSaturationAndGridRestore(void)
{
  ECG_DisplayState state;
  ECG_Display_Init(&state);
  state.initialized = 1U;
  state.samples_processed = UINT32_MAX;
  state.samples_in_column = ECG_DISPLAY_SAMPLES_PER_COLUMN - 1U;
  state.history_head = ECG_DISPLAY_SAMPLES_PER_COLUMN - 1U;
  state.write_column = 0U;
  state.erase_column = ECG_DISPLAY_ERASE_LEAD_COLUMNS;
  for (uint32_t index = 0U; index < ECG_DISPLAY_SAMPLES_PER_COLUMN - 1U; ++index)
  {
    state.history[index] = 100.0f;
  }

  ResetMocks();
  const float32_t positive_outlier = 100.0f;
  CHECK_TRUE(ECG_Display_UpdateWaveform(&state, &positive_outlier, 1U) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(state.samples_processed == 0U);
  CHECK_TRUE(state.samples_in_column == 0U);
  CHECK_TRUE(state.write_column == 1U);
  CHECK_TRUE(state.pending_count == 1U);
  CHECK_TRUE(g_dma_call_count == 0U);
  CHECK_TRUE(DrainRenderer(&state) == 0);
  CHECK_TRUE(g_column_transfer_count == 2U);
  CHECK_TRUE(g_column_transfers[0].x == ECG_DISPLAY_ERASE_LEAD_COLUMNS);
  CHECK_TRUE(g_column_transfers[0].grid_color == ECG_DISPLAY_GRID_COLOR);
  CHECK_TRUE(g_column_transfers[0].axis_color == ECG_DISPLAY_AXIS_COLOR);
  CHECK_TRUE(g_column_transfers[1].x == 0U);
  CHECK_TRUE(g_column_transfers[1].top_color == ECG_DISPLAY_TRACE_COLOR);

  float32_t negative_outliers[ECG_DISPLAY_SAMPLES_PER_COLUMN];
  for (uint32_t index = 0U; index < ECG_DISPLAY_SAMPLES_PER_COLUMN; ++index)
  {
    negative_outliers[index] = -100.0f;
  }
  CHECK_TRUE(ECG_Display_UpdateWaveform(&state, negative_outliers,
                                        ECG_DISPLAY_SAMPLES_PER_COLUMN) ==
             ECG_DISPLAY_STATUS_OK);
  CHECK_TRUE(state.write_column == 2U);
  CHECK_TRUE(DrainRenderer(&state) == 0);
  CHECK_TRUE(g_column_transfer_count == 4U);
  CHECK_TRUE(g_column_transfers[3].x == 1U);
  CHECK_TRUE(g_column_transfers[3].bottom_color == ECG_DISPLAY_TRACE_COLOR);
  return 0;
}

static int TestInvalidSamplesAndDmaErrors(void)
{
  ECG_DisplayState state;
  ECG_Display_Init(&state);
  state.initialized = 1U;
  const float32_t invalid_samples[] = {0.1f, NAN};

  CHECK_TRUE(ECG_Display_UpdateWaveform(&state, invalid_samples, 2U) ==
             ECG_DISPLAY_STATUS_INVALID_SAMPLE);
  CHECK_TRUE(state.samples_processed == 0U);
  CHECK_TRUE(state.write_column == 0U);

  state.initialized = 0U;
  ResetMocks();
  g_dma_fail = 1U;
  CHECK_TRUE(ECG_Display_DrawStaticFrame(&state) ==
             ECG_DISPLAY_STATUS_DMA_ERROR);
  CHECK_TRUE(state.initialized == 0U);

  ResetMocks();
  g_dma_never_completes = 1U;
  CHECK_TRUE(ECG_Display_Clear(&state) == ECG_DISPLAY_STATUS_DMA_TIMEOUT);
  return 0;
}

int main(void)
{
  if (TestDmaFrameAndFontSizes() != 0)
  {
    return 1;
  }
  if (TestThresholdRedrawPreservesWaveform() != 0)
  {
    return 1;
  }
  if (TestCounterWrapSaturationAndGridRestore() != 0)
  {
    return 1;
  }
  if (TestInvalidSamplesAndDmaErrors() != 0)
  {
    return 1;
  }

  printf("all ECG display checks passed\n");
  return 0;
}
