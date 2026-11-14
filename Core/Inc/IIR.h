#ifndef __IIR_H__
#define __IIR_H__

#include "arm_math.h"

/*
 * Initialize once before starting ADC processing. The filter keeps its
 * internal state across calls, so submit consecutive 1 kHz blocks in order.
 * remove_50Hz operates in-place on floating-point samples (not ADC uint16_t
 * storage) and returns ARM_MATH_SUCCESS on success.
 */
arm_status remove_50Hz_init(void);
arm_status remove_50Hz(float32_t *data, uint32_t length);

#endif
