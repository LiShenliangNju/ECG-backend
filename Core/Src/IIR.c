#include "IIR.h"

#define NOTCH_NUM_STAGES 2U

/*
 * Coefficients implement the 49-51 Hz band-stop filter at a 1 kHz
 * sample rate. CMSIS-DSP uses +a1*y[n-1] + a2*y[n-2], so feedback
 * signs are already converted for the CMSIS convention.
 */
static const float32_t notch_coefficients[5U * NOTCH_NUM_STAGES] =
{
  1.0f,
  -1.9021505794f,
  1.0f,
  1.8908514635f,
  -0.9910331479f,

  1.0f,
  -1.9021505794f,
  1.0f,
  1.8965480696f,
  -0.9912740585f
};

static const float32_t notch_scale_values[NOTCH_NUM_STAGES] =
{
  0.9955669717f,
  0.9955669717f
};

static arm_biquad_casd_df1_inst_f32 notch_filter;
static float32_t notch_state[4U * NOTCH_NUM_STAGES];
static uint8_t notch_initialized;

arm_status remove_50Hz_init(void)
{
  arm_biquad_cascade_df1_init_f32(
      &notch_filter,
      NOTCH_NUM_STAGES,
      notch_coefficients,
      notch_state);
  notch_initialized = 1U;
  return ARM_MATH_SUCCESS;
}

arm_status remove_50Hz(float32_t *data, uint32_t length)
{
  uint32_t i;
  const float32_t scale = notch_scale_values[0] * notch_scale_values[1];

  if ((data == 0) || (length == 0U) || (notch_initialized == 0U))
  {
    return ARM_MATH_ARGUMENT_ERROR;
  }

  /* Exact in-place operation is supported by the CMSIS-DSP DF1 routine. */
  arm_biquad_cascade_df1_f32(&notch_filter, data, data, length);

  for (i = 0U; i < length; ++i)
  {
    data[i] *= scale;
  }

  return ARM_MATH_SUCCESS;
}
