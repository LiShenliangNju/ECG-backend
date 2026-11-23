#ifndef HEART_RATE_H
#define HEART_RATE_H

/**
 * 接收 ecg_rpeak 的峰时间戳，计算 RR、瞬时 BPM、最近 5 个有效 RR 的
 * 显示平均 BPM，以及启动稳定标志。
 *
 * 调用顺序：ECG_HeartRate_Init()，然后按时间顺序为每个确认峰调用
 * ECG_HeartRate_OnPeak()。首峰建立基准并不产生区间；后续 RR 只有落在
 * 410–1375 ms 间才计入窗口和稳定计数。
 *
 * peak_timestamp 必须使用 ecg_rpeak 相同的连续 1 kHz 无符号索引。
 * 所有状态只允许主循环/任务串行访问，不提供锁或 ISR 临界区保护。
 */

#include <stdint.h>

#define ECG_HEART_RATE_RR_MIN_MS 410U
#define ECG_HEART_RATE_RR_MAX_MS 1375U
#define ECG_HEART_RATE_RR_WINDOW 5U
#define ECG_HEART_RATE_STABLE_RR_COUNT 5U

typedef struct
{
  /* 最近有效 RR 环形窗口及本模块全部运行状态；只由主循环串行读写。 */
  uint32_t rr_window_ms[ECG_HEART_RATE_RR_WINDOW];
  uint32_t previous_timestamp;
  uint32_t last_rr_ms;
  uint32_t last_instant_bpm;
  uint32_t valid_interval_count;
  uint32_t rejected_interval_count;
  uint8_t rr_window_count;
  uint8_t rr_window_head;
  uint8_t consecutive_valid_rr;
  uint8_t has_previous;
  uint8_t stable;
} ECG_HeartRate;

typedef struct
{
  /* 一次峰调用的完整输出快照；首峰的 has_interval 为 0。 */
  uint32_t rr_ms;
  uint32_t instant_bpm;
  uint32_t average_bpm;
  uint8_t has_interval;
  uint8_t rr_in_range;
  uint8_t stable;
} ECG_HeartRateResult;

/** 清零所有心率状态及最近 5 个 RR 的窗口。重复调用丢弃既有基准。 */
void ECG_HeartRate_Init(ECG_HeartRate *state);

/* peak_timestamp is the 1 kHz sample index reported by ecg_rpeak. */
uint8_t ECG_HeartRate_OnPeak(ECG_HeartRate *state,
                             uint32_t peak_timestamp,
                             ECG_HeartRateResult *result);

#endif
