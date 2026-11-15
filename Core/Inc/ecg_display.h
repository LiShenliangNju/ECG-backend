#ifndef ECG_DISPLAY_H
#define ECG_DISPLAY_H

#include <stdint.h>

#include "arm_math.h"
#include "lcd.h"
#include "lcd_init.h"

#define ECG_DISPLAY_WIDTH 240U
#define ECG_DISPLAY_HEIGHT 240U
#define ECG_DISPLAY_WAVEFORM_X 0U
#define ECG_DISPLAY_WAVEFORM_Y 14U
#define ECG_DISPLAY_WAVEFORM_W 240U
#define ECG_DISPLAY_WAVEFORM_H 180U
#define ECG_DISPLAY_TEXT_Y 200U
#define ECG_DISPLAY_COLUMN_COUNT 240U
#define ECG_DISPLAY_ERASE_LAG_COLUMNS 30U
#define ECG_DISPLAY_SAMPLES_PER_COLUMN 10U
#define ECG_DISPLAY_HISTORY_SAMPLES 2400U
#define ECG_DISPLAY_GRID_COLOR LGRAY
#define ECG_DISPLAY_BG_COLOR BLACK
#define ECG_DISPLAY_TRACE_COLOR GREEN
#define ECG_DISPLAY_AXIS_COLOR LIGHTBLUE
#define ECG_DISPLAY_TEXT_COLOR WHITE

typedef struct
{
  uint16_t write_column;
  uint16_t erase_column;
  uint32_t samples_processed;
  uint32_t history_head;
  uint32_t history_count;
  float32_t history[ECG_DISPLAY_HISTORY_SAMPLES];
  float32_t latest_bpm;
  float32_t latest_peak_to_peak_mv;
  uint8_t initialized;
} ECG_DisplayState;

void ECG_Display_Init(ECG_DisplayState *state);
void ECG_Display_Clear(ECG_DisplayState *state);
void ECG_Display_DrawStaticFrame(ECG_DisplayState *state);
void ECG_Display_UpdateWaveform(ECG_DisplayState *state,
                               const float32_t *samples,
                               uint32_t sample_count);
void ECG_Display_UpdateMetrics(ECG_DisplayState *state,
                              uint32_t heart_rate_bpm,
                              float32_t peak_to_peak_mv);

#endif
