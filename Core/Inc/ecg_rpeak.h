#ifndef ECG_RPEAK_H
#define ECG_RPEAK_H

/**
 * 使用连续 1 kHz ECG 数据检测 R 峰并输出确认后的峰事件。
 *
 * 初始化后逐点调用 ECG_RPeak_ProcessSample()；检测到峰时再将事件依次提交给
 * ECG_HeartRate_OnPeak() 和 ECG_Measure_OnRPeak()。阈值为使用者可调参数，
 * 默认低电平回差为 0.05 mV，候选确认窗口为 30 点，不应期为 200 点。
 *
 * 调用方必须按连续递增的 1 kHz 无符号采样索引提交；所有公开状态均由主循环
 * 或任务串行访问。本模块不提供 ISR 临界区、线程锁或重入保护。
 *
 * 越界阈值会钳位到定义区间；NULL 指针不修改状态。未确认候选返回 0，
 * 已确认峰返回 1。
 */

#include "arm_math.h"
#include "key_input.h"

#define ECG_RPEAK_SAMPLE_RATE_HZ 1000U
#define ECG_RPEAK_DEFAULT_THRESHOLD_MV 0.10f
#define ECG_RPEAK_THRESHOLD_MIN_MV 0.0f
#define ECG_RPEAK_THRESHOLD_MAX_MV 1.5f
#define ECG_RPEAK_HYSTERESIS_MV 0.05f
#define ECG_RPEAK_CONFIRM_WINDOW_SAMPLES 30U
#define ECG_RPEAK_MAX_CONFIRM_DELAY_SAMPLES (ECG_RPEAK_CONFIRM_WINDOW_SAMPLES - 1U)
#define ECG_RPEAK_REFRACTORY_SAMPLES 200U

typedef enum
{
  /** 等待低电平回差和阈值上穿。 */
  ECG_RPEAK_STATE_ARMED = 0U,
  /** 候选已触发，正在累计 30 点窗口最大值。 */
  ECG_RPEAK_STATE_CONFIRM = 1U
} ECG_RPeakState;

typedef struct
{
  /* 所有字段只由主循环/任务串行读写，无跨上下文共享。 */
  float32_t threshold_mv;
  float32_t low_threshold_mv;
  float32_t window_max_mv;
  uint32_t window_max_index;
  uint32_t confirm_samples;
  uint32_t last_peak_index;
  ECG_RPeakState state;
  uint8_t low_seen;
  uint8_t has_last_peak;
} ECG_RPeakDetector;

typedef struct
{
  uint32_t sample_index;
  float32_t value_mv;
} ECG_RPeakEvent;

typedef struct
{
  float32_t current_threshold_mv;
  float32_t min_threshold_mv;
  float32_t max_threshold_mv;
} ECG_RPeakThresholdConfig;

/** 清零检测状态并恢复默认阈值、回差和 ARMED 状态。 */
void ECG_RPeak_Init(ECG_RPeakDetector *detector);
/** 设置阈值；越界值钳位到最小/最大值并返回实际阈值。 */
float32_t ECG_RPeak_SetThreshold(ECG_RPeakDetector *detector, float32_t threshold_mv);
/** 获取当前阈值；detector 为 NULL 时返回 0。 */
float32_t ECG_RPeak_GetThreshold(const ECG_RPeakDetector *detector);
/** 使用配置中的阈值区间更新当前阈值，步进仍由按键事件宏定义。 */
void ECG_RPeak_SetThresholdConfig(ECG_RPeakDetector *detector,
                                 const ECG_RPeakThresholdConfig *config);
/** 每次去抖后的按下事件按固定步进增加或减少阈值。 */
float32_t ECG_RPeak_ApplyKeyStep(ECG_RPeakDetector *detector,
                                 ECG_KeyEvent event,
                                 uint8_t increase);

/* sample_index must be a continuous 1 kHz counter shared with the other ECG
 * modules so that timestamp differences are milliseconds across counter wrap. */
uint8_t ECG_RPeak_ProcessSample(ECG_RPeakDetector *detector,
                                float32_t sample_mv,
                                uint32_t sample_index,
                                ECG_RPeakEvent *event);

#endif
