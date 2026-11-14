#include "IIR.h"

#include <string.h>

static arm_biquad_casd_df1_inst_f32 g_notch_filter;
static float32_t g_filter_state[4U * IIR_NUM_STAGES];
static uint8_t g_filter_initialized = 0U;

/* 50 Hz notch filter coefficients from band-stop.fcf.
 * MATLAB SOS stores: b0, b1, b2, a0, a1, a2
 * CMSIS DF1 uses: b0, b1, b2, -a1, -a2 with a0 normalized to 1.
 */
static const float32_t IIRCoeffs32BS[5U * IIR_NUM_STAGES] = {
    1.0f, -1.902150579414243258824512849969323724508f, 1.0f,
    1.890851463462374759672002255683764815331f, -0.991033147905611921935076225054217502475f,
    1.0f, -1.902150579414243258824512849969323724508f, 1.0f,
    1.896548069620134224777530107530765235424f, -0.991274058481530051523122892831452190876f
};

static const float32_t IIRScaleValues[IIR_NUM_STAGES] = {
    0.995566971680792001109239208744838833809f,
    0.995566971680792001109239208744838833809f
};

void IIR_Notch_Init(void)
{
    memset(g_filter_state, 0, sizeof(g_filter_state));
    arm_biquad_cascade_df1_init_f32(&g_notch_filter,
                                    IIR_NUM_STAGES,
                                    (float32_t *)IIRCoeffs32BS,
                                    g_filter_state);
    g_filter_initialized = 1U;
}

void IIR_Notch_Process(float32_t *data, uint32_t sample_count)
{
    if ((data == NULL) || (sample_count == 0U) || (g_filter_initialized == 0U))
    {
        return;
    }

    const float32_t scale = IIRScaleValues[0] * IIRScaleValues[1];
    float32_t work_buffer[256U];

    if (sample_count > (uint32_t)(sizeof(work_buffer) / sizeof(work_buffer[0])))
    {
        return;
    }

    memcpy(work_buffer, data, sample_count * sizeof(float32_t));
    arm_biquad_cascade_df1_f32(&g_notch_filter, work_buffer, data, sample_count);

    for (uint32_t index = 0U; index < sample_count; ++index)
    {
        data[index] *= scale;
    }
}

void remove_50Hz(float32_t *data, uint32_t sample_count)
{
    IIR_Notch_Process(data, sample_count);
}
