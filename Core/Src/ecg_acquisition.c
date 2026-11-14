#include "ecg_acquisition.h"

#include "adc.h"
#include "tim.h"

typedef union
{
  uint32_t alignment;
  uint16_t samples[ECG_DMA_BUFFER_LENGTH];
} ECG_DmaBuffer;

static ECG_DmaBuffer dma_buffer;
static volatile uint8_t ready_mask;
static volatile uint8_t active_mask;
static volatile uint8_t acquisition_fault;
static volatile uint32_t overrun_count;
static volatile uint32_t completed_sample_count;
static uint32_t block_start_sample[2];
static uint8_t next_consume_half;

static uint32_t EnterCriticalSection(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void ExitCriticalSection(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void PublishCompletedHalf(ECG_DmaHalf completed_half,
                                 ECG_DmaHalf next_write_half)
{
  uint8_t completed_mask = (uint8_t)(1U << completed_half);
  uint8_t next_write_mask = (uint8_t)(1U << next_write_half);

  if ((acquisition_fault != 0U) ||
      (((ready_mask | active_mask) & next_write_mask) != 0U))
  {
    if (acquisition_fault == 0U)
    {
      acquisition_fault = 1U;
      overrun_count++;
    }
    return;
  }

  block_start_sample[completed_half] = completed_sample_count;
  completed_sample_count += ECG_DMA_HALF_LENGTH;
  ready_mask |= completed_mask;
}

HAL_StatusTypeDef ECG_Acquisition_Start(void)
{
  HAL_StatusTypeDef status;
  uint32_t primask = EnterCriticalSection();
  ready_mask = 0U;
  active_mask = 0U;
  acquisition_fault = 0U;
  overrun_count = 0U;
  completed_sample_count = 0U;
  block_start_sample[ECG_DMA_HALF_FIRST] = 0U;
  block_start_sample[ECG_DMA_HALF_SECOND] = 0U;
  next_consume_half = ECG_DMA_HALF_FIRST;
  ExitCriticalSection(primask);

  status = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)dma_buffer.samples,
                             ECG_DMA_BUFFER_LENGTH);
  if (status != HAL_OK)
  {
    return status;
  }

  status = HAL_TIM_Base_Start(&htim2);
  if (status != HAL_OK)
  {
    HAL_StatusTypeDef stop_status = HAL_ADC_Stop_DMA(&hadc1);
    if (stop_status != HAL_OK)
    {
      return stop_status;
    }
    return status;
  }

  return HAL_OK;
}

uint8_t ECG_Acquisition_TryGetBlock(ECG_SampleBlock *block)
{
  if (block == NULL)
  {
    return 0U;
  }

  uint32_t primask = EnterCriticalSection();
  uint8_t half_mask = (uint8_t)(1U << next_consume_half);
  if ((acquisition_fault != 0U) || (active_mask != 0U) ||
      ((ready_mask & half_mask) == 0U))
  {
    ExitCriticalSection(primask);
    return 0U;
  }

  ready_mask &= (uint8_t)~half_mask;
  active_mask = half_mask;
  block->half = (ECG_DmaHalf)next_consume_half;
  block->samples = &dma_buffer.samples[next_consume_half * ECG_DMA_HALF_LENGTH];
  block->first_sample_index = block_start_sample[next_consume_half];
  block->sample_count = ECG_DMA_HALF_LENGTH;
  next_consume_half ^= 1U;
  ExitCriticalSection(primask);
  return 1U;
}

uint8_t ECG_Acquisition_ReleaseBlock(ECG_DmaHalf half)
{
  if ((half != ECG_DMA_HALF_FIRST) && (half != ECG_DMA_HALF_SECOND))
  {
    return 0U;
  }

  uint32_t primask = EnterCriticalSection();
  uint8_t half_mask = (uint8_t)(1U << half);
  if (active_mask != half_mask)
  {
    ExitCriticalSection(primask);
    return 0U;
  }

  active_mask = 0U;
  ExitCriticalSection(primask);
  return 1U;
}

uint8_t ECG_Acquisition_HasFault(void)
{
  return acquisition_fault;
}

uint32_t ECG_Acquisition_GetOverrunCount(void)
{
  return overrun_count;
}

uint32_t ECG_Acquisition_GetSampleCount(void)
{
  return completed_sample_count;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    PublishCompletedHalf(ECG_DMA_HALF_FIRST, ECG_DMA_HALF_SECOND);
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    PublishCompletedHalf(ECG_DMA_HALF_SECOND, ECG_DMA_HALF_FIRST);
  }
}
