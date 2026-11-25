#ifndef ECG_STAGE2_HIL_H
#define ECG_STAGE2_HIL_H

#include "ecg_acquisition.h"
#include "ecg_signal.h"
#include "IIR.h"

#define ECG_STAGE2_HIL_RAW_CAPTURE_CAPACITY 4096U
#define ECG_STAGE2_HIL_SIGNAL_CAPTURE_CAPACITY 4096U
#define ECG_STAGE2_HIL_BLOCK_RECORD_CAPACITY 512U

typedef struct
{
  uint32_t first_raw_sample_index;
  uint32_t first_1khz_sample_index;
  uint32_t process_cycles;
  uint32_t ready_to_release_cycles;
  uint16_t raw_sample_count;
  uint16_t decimated_sample_count;
  uint8_t dma_half;
  uint8_t downsampler_phase_before;
  uint8_t downsampler_phase_after;
  uint8_t filter_status;
} ECG_Stage2HILBlockRecord;

typedef struct
{
  volatile uint32_t magic;
  volatile uint32_t dwt_enabled;
  volatile uint32_t capture_complete;
  volatile uint32_t acquisition_stopped;
  volatile uint32_t tim_stop_status;
  volatile uint32_t adc_dma_stop_status;
  volatile uint32_t total_raw_samples;
  volatile uint32_t total_decimated_samples;
  volatile uint32_t dma_overrun_count;
  volatile uint32_t raw_capture_count;
  volatile uint32_t decimated_input_capture_count;
  volatile uint32_t filtered_output_capture_count;
  volatile uint32_t block_record_count;
  volatile uint32_t max_process_cycles;
  volatile uint32_t max_ready_to_release_cycles;
  volatile uint32_t lcd_service_call_count;
  volatile uint32_t max_lcd_service_cycles;
  volatile uint32_t sequence_error_count;
  volatile uint32_t decimation_error_count;
  volatile uint32_t filter_error_count;
  volatile uint32_t block_record_overflow_count;
  uint16_t raw_adc_codes[ECG_STAGE2_HIL_RAW_CAPTURE_CAPACITY];
  float32_t calibrated_10khz_mv[ECG_STAGE2_HIL_RAW_CAPTURE_CAPACITY];
  float32_t decimated_1khz_before_notch[ECG_STAGE2_HIL_SIGNAL_CAPTURE_CAPACITY];
  float32_t filtered_1khz_after_notch[ECG_STAGE2_HIL_SIGNAL_CAPTURE_CAPACITY];
  ECG_Stage2HILBlockRecord blocks[ECG_STAGE2_HIL_BLOCK_RECORD_CAPACITY];
} ECG_Stage2HILCapture;

extern ECG_Stage2HILCapture g_ecg_stage2_hil_capture;

void ECG_Stage2HIL_Init(void);
uint32_t ECG_Stage2HIL_GetCycleCount(void);
void ECG_Stage2HIL_CaptureRawBlock(const ECG_SampleBlock *block,
                                   const float32_t *calibrated_mv);
void ECG_Stage2HIL_CaptureDecimatedInput(const float32_t *samples,
                                         uint32_t sample_count);
void ECG_Stage2HIL_RecordBlock(const ECG_SampleBlock *block,
                               uint32_t phase_before,
                               uint32_t phase_after,
                               uint32_t decimated_sample_count,
                               IIR_NotchStatus filter_status,
                               const float32_t *filtered_samples,
                               uint32_t process_start_cycles);
void ECG_Stage2HIL_RecordReadyToRelease(uint32_t cycles);
void ECG_Stage2HIL_RecordLCDService(uint32_t cycles);
uint8_t ECG_Stage2HIL_IsCaptureComplete(void);
void ECG_Stage2HIL_RecordStop(HAL_StatusTypeDef tim_status,
                              HAL_StatusTypeDef adc_dma_status,
                              uint32_t dma_overrun_count);

#endif
