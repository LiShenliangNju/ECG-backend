#ifndef __IIR_H__
#define __IIR_H__

#include "arm_math.h"
#include <stdint.h>

#define IIR_NUM_STAGES 2U

void IIR_Notch_Init(void);
void IIR_Notch_Process(float32_t *data, uint32_t sample_count);
void remove_50Hz(float32_t *data, uint32_t sample_count);

#endif
