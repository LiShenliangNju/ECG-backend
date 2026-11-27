#ifndef ECG_DISPLAY_H
#define ECG_DISPLAY_H

/**
 * @file ecg_display.h
 * @brief Foreground LCD rendering interface for the ECG waveform and metrics.
 *
 * The caller owns ECG_DisplayState and supplies filtered ECG samples in mV.
 * The waveform is accumulated across calls: each ten input samples produce
 * one min/max envelope column, for a 240-column, 2400-sample display history.
 * Call ECG_Display_UpdateMetrics separately when the displayed measurements
 * change; it is not driven by individual waveform samples.
 *
 * Initialize the board delay and LCD/SPI hardware before drawing. Sample
 * updates only enqueue work; call ECG_Display_Service repeatedly from the
 * foreground loop to advance rendering. The state and LCD are not thread-safe.
 */
#include <stdint.h>

#include "arm_math.h"
#include "lcd.h"
#include "lcd_init.h"

#define ECG_DISPLAY_WIDTH 240U
#define ECG_DISPLAY_HEIGHT 240U
#define ECG_DISPLAY_WAVEFORM_X 0U
#define ECG_DISPLAY_WAVEFORM_Y 16U
#define ECG_DISPLAY_WAVEFORM_W 240U
#define ECG_DISPLAY_WAVEFORM_H 178U
#define ECG_DISPLAY_TEXT_Y 200U
#define ECG_DISPLAY_COLUMN_COUNT 240U
#define ECG_DISPLAY_ERASE_LEAD_COLUMNS 30U
#define ECG_DISPLAY_SAMPLES_PER_COLUMN 10U
#define ECG_DISPLAY_HISTORY_SAMPLES 2400U
#define ECG_DISPLAY_PENDING_COLUMN_CAPACITY 64U
#define ECG_DISPLAY_GRID_COLOR LGRAY
#define ECG_DISPLAY_BG_COLOR BLACK
#define ECG_DISPLAY_TRACE_COLOR GREEN
#define ECG_DISPLAY_AXIS_COLOR LIGHTBLUE
#define ECG_DISPLAY_TEXT_COLOR WHITE

typedef enum
{
  ECG_DISPLAY_STATUS_OK = 0U,
  ECG_DISPLAY_STATUS_INVALID_ARGUMENT,
  ECG_DISPLAY_STATUS_INVALID_SAMPLE,
  ECG_DISPLAY_STATUS_QUEUE_FULL,
  ECG_DISPLAY_STATUS_BUSY,
  ECG_DISPLAY_STATUS_DMA_ERROR,
  ECG_DISPLAY_STATUS_DMA_TIMEOUT
} ECG_DisplayStatus;

typedef struct
{
  uint16_t write_column;
  uint16_t erase_column;
  uint16_t min_y;
  uint16_t max_y;
} ECG_DisplayPendingColumn;

typedef struct
{
  /* Foreground-owned rolling plot cursors; both wrap at display width. */
  uint16_t write_column;
  uint16_t erase_column;
  /* Input sample count, independent grouping phase and ring-buffer accounting. */
  uint32_t samples_processed;
  uint8_t samples_in_column;
  uint32_t history_head;
  uint32_t history_count;
  float32_t history[ECG_DISPLAY_HISTORY_SAMPLES];
  /* Main-loop producer/renderer queue; column output is sent asynchronously. */
  ECG_DisplayPendingColumn pending_columns[ECG_DISPLAY_PENDING_COLUMN_CAPACITY];
  uint8_t pending_head;
  uint8_t pending_tail;
  uint8_t pending_count;
  uint8_t dma_active;
  uint8_t dma_stage;
  uint32_t dma_started_tick;
  uint8_t metrics_dirty;
  /* Most recently supplied values; the update API draws the supplied values. */
  float32_t latest_bpm;
  float32_t latest_peak_to_peak_mv;
  /* Set by the first non-empty waveform update after initialization. */
  uint8_t initialized;
} ECG_DisplayState;

/**
 * Reset the caller-owned drawing state; does not initialize or clear the LCD.
 * Call after the LCD hardware is initialized. A NULL state is ignored.
 */
void ECG_Display_Init(ECG_DisplayState *state);

/**
 * Clear the full panel and reset waveform cursors/history accounting.
 * The full-panel pixel transfer uses SPI DMA; this call waits for completion.
 * A successful clear invalidates the static frame so the next update redraws it.
 * Foreground only.
 */
ECG_DisplayStatus ECG_Display_Clear(ECG_DisplayState *state);

/**
 * Draw the fixed grid and center axis, then issue label-drawing calls,
 * and mark the static frame initialized. The full-screen fill uses SPI DMA.
 * LCD hardware must already be initialized. Foreground only.
 */
ECG_DisplayStatus ECG_Display_DrawStaticFrame(ECG_DisplayState *state);

/**
 * Append filtered ECG samples (mV) to the rolling waveform.
 *
 * Calls may contain any non-zero number of samples; groups of ten continue
 * across call boundaries, including samples_processed wrap. Empty input is
 * a successful no-op; invalid pointers or non-finite samples return an error.
 * Completed columns are queued for ECG_Display_Service; queue exhaustion is
 * reported rather than dropping waveform data. This function does not draw.
 */
ECG_DisplayStatus ECG_Display_UpdateWaveform(ECG_DisplayState *state,
                                             const float32_t *samples,
                                             uint32_t sample_count);

/**
 * Store heart rate and peak-to-peak value in mV for deferred refresh.
 * ECG_Display_Service renders them after pending waveform columns.
 */
void ECG_Display_UpdateMetrics(ECG_DisplayState *state,
                              uint32_t heart_rate_bpm,
                              float32_t peak_to_peak_mv);

/**
 * Advance at most one SPI-DMA column transfer or one deferred metric refresh.
 * Call repeatedly from the foreground main loop; the function never waits for
 * column DMA completion. Metric glyph drawing remains synchronous.
 * Returns a transport error or queue/state error.
 */
ECG_DisplayStatus ECG_Display_Service(ECG_DisplayState *state);

#endif
