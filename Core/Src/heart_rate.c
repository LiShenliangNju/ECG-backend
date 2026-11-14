#include "heart_rate.h"

#include <string.h>

static uint32_t ComputeAverageBpm(const ECG_HeartRate *state)
{
  if (state->rr_window_count == 0U)
  {
    return 0U;
  }

  uint32_t sum_rr_ms = 0U;
  for (uint8_t index = 0U; index < state->rr_window_count; ++index)
  {
    sum_rr_ms += state->rr_window_ms[index];
  }

  const uint32_t rounded_sum = (60000U * (uint32_t)state->rr_window_count) +
                               (sum_rr_ms / 2U);
  return rounded_sum / sum_rr_ms;
}

static void AddValidInterval(ECG_HeartRate *state, uint32_t rr_ms)
{
  state->rr_window_ms[state->rr_window_head] = rr_ms;
  state->rr_window_head = (uint8_t)((state->rr_window_head + 1U) %
                                    ECG_HEART_RATE_RR_WINDOW);
  if (state->rr_window_count < ECG_HEART_RATE_RR_WINDOW)
  {
    state->rr_window_count++;
  }

  if (state->consecutive_valid_rr < ECG_HEART_RATE_STABLE_RR_COUNT)
  {
    state->consecutive_valid_rr++;
  }
  if (state->consecutive_valid_rr >= ECG_HEART_RATE_STABLE_RR_COUNT)
  {
    state->stable = 1U;
  }
}

void ECG_HeartRate_Init(ECG_HeartRate *state)
{
  if (state == NULL)
  {
    return;
  }

  memset(state, 0, sizeof(*state));
}

uint8_t ECG_HeartRate_OnPeak(ECG_HeartRate *state,
                             uint32_t peak_timestamp,
                             ECG_HeartRateResult *result)
{
  if ((state == NULL) || (result == NULL))
  {
    return 0U;
  }

  memset(result, 0, sizeof(*result));

  if (state->has_previous == 0U)
  {
    state->has_previous = 1U;
    state->previous_timestamp = peak_timestamp;
    result->stable = state->stable;
    return 0U;
  }

  /* 1 kHz timestamps make the unsigned difference equal to milliseconds. */
  const uint32_t rr_ms = peak_timestamp - state->previous_timestamp;
  state->previous_timestamp = peak_timestamp;

  result->has_interval = 1U;
  result->rr_ms = rr_ms;
  result->instant_bpm = (rr_ms == 0U) ? 0U : (60000U / rr_ms);
  state->last_rr_ms = rr_ms;
  state->last_instant_bpm = result->instant_bpm;

  if ((rr_ms >= ECG_HEART_RATE_RR_MIN_MS) && (rr_ms <= ECG_HEART_RATE_RR_MAX_MS))
  {
    result->rr_in_range = 1U;
    state->valid_interval_count++;
    AddValidInterval(state, rr_ms);
  }
  else
  {
    state->rejected_interval_count++;
    state->consecutive_valid_rr = 0U;
    state->stable = 0U;
  }

  result->stable = state->stable;
  result->average_bpm = ComputeAverageBpm(state);
  return 1U;
}
