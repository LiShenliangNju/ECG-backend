#include "ecg_stage2_hil.h"

#include <string.h>

ECG_Stage2HILCapture g_ecg_stage2_hil_capture;

void ECG_Stage2HIL_Init(void)
{
  memset(&g_ecg_stage2_hil_capture, 0,
         sizeof(g_ecg_stage2_hil_capture));

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  g_ecg_stage2_hil_capture.magic = 0xEC620001U;
  g_ecg_stage2_hil_capture.dwt_enabled =
      (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U ? 1U : 0U;
}

uint32_t ECG_Stage2HIL_GetCycleCount(void)
{
  return DWT->CYCCNT;
}

void ECG_Stage2HIL_CaptureRawBlock(const ECG_SampleBlock *block,
                                   const float32_t *calibrated_mv)
{
  if ((block == NULL) || (block->samples == NULL) ||
      (calibrated_mv == NULL))
  {
    return;
  }

  uint32_t capture_count = g_ecg_stage2_hil_capture.raw_capture_count;
  const uint32_t available =
      ECG_STAGE2_HIL_RAW_CAPTURE_CAPACITY - capture_count;
  const uint32_t copy_count =
      block->sample_count < available ? block->sample_count : available;

  for (uint32_t index = 0U; index < copy_count; ++index)
  {
    g_ecg_stage2_hil_capture.raw_adc_codes[capture_count + index] =
        block->samples[index];
    g_ecg_stage2_hil_capture.calibrated_10khz_mv[capture_count + index] =
        calibrated_mv[index];
  }

  g_ecg_stage2_hil_capture.raw_capture_count = capture_count + copy_count;
}

void ECG_Stage2HIL_CaptureDecimatedInput(const float32_t *samples,
                                         uint32_t sample_count)
{
  if ((samples == NULL) || (sample_count == 0U))
  {
    return;
  }

  uint32_t capture_count =
      g_ecg_stage2_hil_capture.decimated_input_capture_count;
  const uint32_t available =
      ECG_STAGE2_HIL_SIGNAL_CAPTURE_CAPACITY - capture_count;
  const uint32_t copy_count =
      sample_count < available ? sample_count : available;

  memcpy(&g_ecg_stage2_hil_capture.decimated_1khz_before_notch[capture_count],
         samples, copy_count * sizeof(float32_t));
  g_ecg_stage2_hil_capture.decimated_input_capture_count =
      capture_count + copy_count;
}

void ECG_Stage2HIL_RecordBlock(const ECG_SampleBlock *block,
                               uint32_t phase_before,
                               uint32_t phase_after,
                               uint32_t decimated_sample_count,
                               IIR_NotchStatus filter_status,
                               const float32_t *filtered_samples,
                               uint32_t process_start_cycles)
{
  if ((block == NULL) || (filtered_samples == NULL))
  {
    return;
  }

  const uint32_t expected_decimated_count =
      (phase_before + block->sample_count) / ECG_DOWNSAMPLE_FACTOR;
  const uint32_t expected_phase_after =
      (phase_before + block->sample_count) % ECG_DOWNSAMPLE_FACTOR;

  if ((phase_before >= ECG_DOWNSAMPLE_FACTOR) ||
      (decimated_sample_count != expected_decimated_count) ||
      (phase_after != expected_phase_after))
  {
    g_ecg_stage2_hil_capture.decimation_error_count++;
  }

  if (g_ecg_stage2_hil_capture.total_raw_samples != 0U)
  {
    const ECG_Stage2HILBlockRecord *previous =
        &g_ecg_stage2_hil_capture.blocks[
            g_ecg_stage2_hil_capture.block_record_count - 1U];
    if ((block->first_sample_index !=
         previous->first_raw_sample_index + previous->raw_sample_count) ||
        (block->half == previous->dma_half))
    {
      g_ecg_stage2_hil_capture.sequence_error_count++;
    }
  }

  g_ecg_stage2_hil_capture.total_raw_samples += block->sample_count;
  g_ecg_stage2_hil_capture.total_decimated_samples +=
      decimated_sample_count;
  if (g_ecg_stage2_hil_capture.total_decimated_samples !=
      (g_ecg_stage2_hil_capture.total_raw_samples /
       ECG_DOWNSAMPLE_FACTOR))
  {
    g_ecg_stage2_hil_capture.decimation_error_count++;
  }

  uint32_t output_capture_count =
      g_ecg_stage2_hil_capture.filtered_output_capture_count;
  const uint32_t output_available =
      ECG_STAGE2_HIL_SIGNAL_CAPTURE_CAPACITY - output_capture_count;
  const uint32_t output_copy_count =
      decimated_sample_count < output_available ?
          decimated_sample_count : output_available;
  memcpy(&g_ecg_stage2_hil_capture.filtered_1khz_after_notch[
             output_capture_count],
         filtered_samples, output_copy_count * sizeof(float32_t));
  g_ecg_stage2_hil_capture.filtered_output_capture_count =
      output_capture_count + output_copy_count;

  if (filter_status != IIR_NOTCH_STATUS_OK)
  {
    g_ecg_stage2_hil_capture.filter_error_count++;
  }

  const uint32_t process_cycles =
      ECG_Stage2HIL_GetCycleCount() - process_start_cycles;
  if (process_cycles > g_ecg_stage2_hil_capture.max_process_cycles)
  {
    g_ecg_stage2_hil_capture.max_process_cycles = process_cycles;
  }

  const uint32_t record_index =
      g_ecg_stage2_hil_capture.block_record_count;
  if (record_index < ECG_STAGE2_HIL_BLOCK_RECORD_CAPACITY)
  {
    ECG_Stage2HILBlockRecord *record =
        &g_ecg_stage2_hil_capture.blocks[record_index];
    record->first_raw_sample_index = block->first_sample_index;
    record->first_1khz_sample_index =
        block->first_sample_index / ECG_DOWNSAMPLE_FACTOR;
    record->process_cycles = process_cycles;
    record->raw_sample_count = block->sample_count;
    record->decimated_sample_count =
        (uint16_t)decimated_sample_count;
    record->dma_half = (uint8_t)block->half;
    record->downsampler_phase_before = (uint8_t)phase_before;
    record->downsampler_phase_after = (uint8_t)phase_after;
    record->filter_status = (uint8_t)filter_status;
    g_ecg_stage2_hil_capture.block_record_count = record_index + 1U;
  }
  else
  {
    g_ecg_stage2_hil_capture.block_record_overflow_count++;
  }

  if (g_ecg_stage2_hil_capture.filtered_output_capture_count ==
      ECG_STAGE2_HIL_SIGNAL_CAPTURE_CAPACITY)
  {
    g_ecg_stage2_hil_capture.capture_complete = 1U;
  }
}

void ECG_Stage2HIL_RecordReadyToRelease(uint32_t cycles)
{
  uint32_t record_count = g_ecg_stage2_hil_capture.block_record_count;
  if (record_count == 0U)
  {
    return;
  }

  if (cycles > g_ecg_stage2_hil_capture.max_ready_to_release_cycles)
  {
    g_ecg_stage2_hil_capture.max_ready_to_release_cycles = cycles;
  }

  if (record_count <= ECG_STAGE2_HIL_BLOCK_RECORD_CAPACITY)
  {
    g_ecg_stage2_hil_capture.blocks[record_count - 1U]
        .ready_to_release_cycles = cycles;
  }
}

void ECG_Stage2HIL_RecordLCDService(uint32_t cycles)
{
  g_ecg_stage2_hil_capture.lcd_service_call_count++;
  if (cycles > g_ecg_stage2_hil_capture.max_lcd_service_cycles)
  {
    g_ecg_stage2_hil_capture.max_lcd_service_cycles = cycles;
  }
}

uint8_t ECG_Stage2HIL_IsCaptureComplete(void)
{
  return (uint8_t)(g_ecg_stage2_hil_capture.capture_complete != 0U);
}

void ECG_Stage2HIL_RecordStop(HAL_StatusTypeDef tim_status,
                              HAL_StatusTypeDef adc_dma_status,
                              uint32_t dma_overrun_count)
{
  g_ecg_stage2_hil_capture.tim_stop_status = (uint32_t)tim_status;
  g_ecg_stage2_hil_capture.adc_dma_stop_status =
      (uint32_t)adc_dma_status;
  g_ecg_stage2_hil_capture.dma_overrun_count = dma_overrun_count;
  g_ecg_stage2_hil_capture.acquisition_stopped =
      (tim_status == HAL_OK && adc_dma_status == HAL_OK) ? 1U : 0U;
}
