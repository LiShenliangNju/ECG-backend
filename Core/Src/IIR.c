/*
 * Notch-filter data flow:
 *   caller's float32_t block
 *     -> bounded stack work buffer
 *     -> CMSIS-DSP two-stage DF1 cascade using module-global history
 *     -> caller's buffer, scaled by the product of the two section gains.
 *
 * The filter handle, history, coefficients, and initialization flag form one
 * singleton stream; Init resets that stream and Process advances it. The
 * processing path is intended for the main loop/task, not an ISR, and must
 * not run concurrently or reentrantly. A 256-sample block uses a 1 KiB local
 * float buffer, so callers must also account for task stack capacity.
 *
 * The coefficients are designed for 1 kHz samples; main applies this filter
 * to the decimated stream before R-peak detection.
 */
#include "IIR.h"

#include <string.h>

/* Persistent singleton filter state: only Init and Process access these
 * objects, and callers must serialize both functions.
 */
static arm_biquad_casd_df1_inst_f32 g_notch_filter;
static float32_t g_filter_state[4U * IIR_NUM_STAGES];
static uint8_t g_filter_initialized = 0U;

/*
 * 50 Hz notch coefficients from band-stop.fcf. The source SOS order is
 * b0,b1,b2,a0,a1,a2; CMSIS DF1 expects b0,b1,b2,-a1,-a2 after normalizing a0.
 * The denominator sign conversion is essential to preserve the designed poles.
 */
static const float32_t IIRCoeffs32BS[5U * IIR_NUM_STAGES] = {
    1.0f, -1.902150579414243258824512849969323724508f, 1.0f,
    1.890851463462374759672002255683764815331f, -0.991033147905611921935076225054217502475f,
    1.0f, -1.902150579414243258824512849969323724508f, 1.0f,
    1.896548069620134224777530107530765235424f, -0.991274058481530051523122892831452190876f
};

/* Per-section gains from band-stop.fcf; Process applies their product. */
static const float32_t IIRScaleValues[IIR_NUM_STAGES] = {
    0.995566971680792001109239208744838833809f,
    0.995566971680792001109239208744838833809f
};

/*
 * Initialize the CMSIS cascade and mark it usable. Reinitialization discards
 * prior input history, so it restarts the filter transient. CMSIS init has no
 * status result in this interface; this function is main-loop/task context
 * and must not overlap Process.
 */
void IIR_Notch_Init(void)
{
    memset(g_filter_state, 0, sizeof(g_filter_state));
    arm_biquad_cascade_df1_init_f32(&g_notch_filter,
                                    IIR_NUM_STAGES,
                                    (float32_t *)IIRCoeffs32BS,
                                    g_filter_state);
    g_filter_initialized = 1U;
}

/*
 * Filter one block while retaining CMSIS state for the next block. Invalid
 * pointers or zero length, calls before Init, and blocks over 256 samples
 * return distinct statuses without changing data. The copy to a local buffer
 * separates input from output for the CMSIS call; the fixed bound also limits
 * stack use. Main-loop/task context only; no synchronization is used.
 */
IIR_NotchStatus IIR_Notch_Process(float32_t *data, uint32_t sample_count)
{
    if ((data == NULL) || (sample_count == 0U))
    {
        return IIR_NOTCH_STATUS_INVALID_ARGUMENT;
    }

    if (g_filter_initialized == 0U)
    {
        return IIR_NOTCH_STATUS_NOT_INITIALIZED;
    }

    if (sample_count > IIR_NOTCH_MAX_BLOCK_SIZE)
    {
        return IIR_NOTCH_STATUS_BLOCK_TOO_LARGE;
    }

    const float32_t scale = IIRScaleValues[0] * IIRScaleValues[1];
    float32_t work_buffer[IIR_NOTCH_MAX_BLOCK_SIZE];

    /* Preserve input samples while CMSIS writes filtered values to data. */
    memcpy(work_buffer, data, sample_count * sizeof(float32_t));
    arm_biquad_cascade_df1_f32(&g_notch_filter, work_buffer, data, sample_count);

    for (uint32_t index = 0U; index < sample_count; ++index)
    {
        data[index] *= scale;
    }

    return IIR_NOTCH_STATUS_OK;
}

/*
 * Compatibility entry point; all initialization, capacity, state, and
 * concurrency semantics are those of IIR_Notch_Process.
 */
IIR_NotchStatus remove_50Hz(float32_t *data, uint32_t sample_count)
{
    return IIR_Notch_Process(data, sample_count);
}
