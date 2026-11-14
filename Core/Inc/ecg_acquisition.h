#ifndef ECG_ACQUISITION_H
#define ECG_ACQUISITION_H

#include "main.h"

#define ECG_DMA_BUFFER_LENGTH 512U
#define ECG_DMA_HALF_LENGTH (ECG_DMA_BUFFER_LENGTH / 2U)

typedef enum
{
  ECG_DMA_HALF_FIRST = 0U,
  ECG_DMA_HALF_SECOND = 1U
} ECG_DmaHalf;

typedef struct
{
  const uint16_t *samples;
  uint32_t first_sample_index;
  uint16_t sample_count;
  ECG_DmaHalf half;
} ECG_SampleBlock;

HAL_StatusTypeDef ECG_Acquisition_Start(void);
uint8_t ECG_Acquisition_TryGetBlock(ECG_SampleBlock *block);
uint8_t ECG_Acquisition_ReleaseBlock(ECG_DmaHalf half);
uint8_t ECG_Acquisition_HasFault(void);
uint32_t ECG_Acquisition_GetOverrunCount(void);
uint32_t ECG_Acquisition_GetSampleCount(void);

#endif
