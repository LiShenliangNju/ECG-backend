#ifndef __IIR_H__
#define __IIR_H__

/*
 * Stateful 50 Hz ECG notch filter backed by a two-stage CMSIS-DSP DF1 cascade.
 *
 * Typical use:
 *   IIR_Notch_Init();
 *   IIR_Notch_Process(float_samples, sample_count);  // repeat for each block
 *
 * The filter has one module-global history, so initialize once per stream and
 * serialize all calls. Reinitialization clears filter history. Process returns
 * an explicit status for invalid input, calls before initialization, and
 * blocks above the 256-sample limit. The coefficients are designed for a
 * 1 kHz input stream.
 */
#include "arm_math.h"
#include <stdint.h>

/* Number of cascaded second-order sections. */
#define IIR_NUM_STAGES 2U

/* Initialize the filter instance and clear its history. */
void IIR_Notch_Init(void);

typedef enum
{
    IIR_NOTCH_STATUS_OK = 0U,
    IIR_NOTCH_STATUS_INVALID_ARGUMENT = 1U,
    IIR_NOTCH_STATUS_NOT_INITIALIZED = 2U,
    IIR_NOTCH_STATUS_BLOCK_TOO_LARGE = 3U
} IIR_NotchStatus;

#define IIR_NOTCH_MAX_BLOCK_SIZE 256U

/*
 * Filter sample_count float32_t values in place. data must be writable and
 * sample_count must be in [1, IIR_NOTCH_MAX_BLOCK_SIZE]. Calls preserve
 * history across blocks. Returns the specific error status and leaves data
 * unchanged when the request is invalid or the filter has not been initialized.
 * This function is not reentrant or safe for concurrent callers.
 */
IIR_NotchStatus IIR_Notch_Process(float32_t *data, uint32_t sample_count);

/* Compatibility wrapper; returns the same status as IIR_Notch_Process. */
IIR_NotchStatus remove_50Hz(float32_t *data, uint32_t sample_count);

#endif
