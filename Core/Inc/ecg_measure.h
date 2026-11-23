#ifndef ECG_MEASURE_H
#define ECG_MEASURE_H

/**
 * 使用连续 1 kHz 降采样波形维护相邻 R 峰之间的每搏峰峰值。
 *
 * 调用 ECG_Measure_Init() 后，每个 1 kHz 滤波输出先调用
 * ECG_Measure_ProcessSample()，收到确认峰后调用 ECG_Measure_OnRPeak()。
 * 区间端点为相邻两个确认峰：上一峰之后到当前峰（含）。
 *
 * 全部状态由主循环串行维护；索引必须与 ecg_rpeak 使用的连续 1 kHz
 * 无符号计数域相同。本模块无 ISR 回调、锁或临界区保护。
 */

#include "arm_math.h"

#define ECG_MEASURE_HISTORY_LENGTH 64U
#define ECG_MEASURE_HISTORY_MASK (ECG_MEASURE_HISTORY_LENGTH - 1U)

typedef struct
{
  /* min/max 为一次已完成搏动区间内的 1 kHz 滤波波形极值。 */
  float32_t min_mv;
  float32_t max_mv;
  float32_t peak_to_peak_mv;
  uint8_t valid;
} ECG_BeatMeasurement;

typedef struct
{
  /* 环形历史、当前搏动累计值及最近有效搏动；只允许串行主循环访问。 */
  float32_t history_mv[ECG_MEASURE_HISTORY_LENGTH];
  float32_t beat_min_mv;
  float32_t beat_max_mv;
  uint32_t last_index;
  uint32_t beat_start_index;
  uint32_t committed_index;
  ECG_BeatMeasurement latest;
  uint8_t has_sample;
  uint8_t beat_active;
  uint8_t beat_has_samples;
} ECG_MeasureState;

/** 清零历史、当前搏动和最近有效测量。 */
void ECG_Measure_Init(ECG_MeasureState *state);

/* 在确认峰到达前逐点提交降采样后的 1 kHz 波形和相同连续时间戳。 */
void ECG_Measure_ProcessSample(ECG_MeasureState *state,
                               uint32_t sample_index,
                               float32_t sample_mv);

/* peak_index 为 ecg_rpeak 时间戳；首峰建立基准，后续峰完成一搏并返回 1。 */
uint8_t ECG_Measure_OnRPeak(ECG_MeasureState *state,
                            uint32_t peak_index,
                            ECG_BeatMeasurement *measurement);
const ECG_BeatMeasurement *ECG_Measure_GetLatest(const ECG_MeasureState *state);

#endif
