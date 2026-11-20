/**
 * @brief ADC1/TIM2 ECG 连续采集模块。
 *
 * 外设初始化后调用 ECG_Acquisition_Start()；主循环/任务取得就绪块、处理后，
 * 必须调用 ECG_Acquisition_ReleaseBlock() 归还半区。
 * DMA 缓冲区分为两个半区，每块包含 ECG_DMA_HALF_LENGTH 个 uint16_t 样本。
 * 样本指针指向模块内部缓冲区，只能在归还前使用；覆盖故障锁存后不再交付数据。
 * 当前实现没有运行中重启保护，Start() 仅应在外设已初始化且采集未运行时调用。
 */
#ifndef ECG_ACQUISITION_H
#define ECG_ACQUISITION_H

#include "main.h"

/* DMA 循环缓冲区总样本数；须与启动长度及半满/全满回调保持一致。 */
#define ECG_DMA_BUFFER_LENGTH 512U
/* 每次 DMA 半满或全满事件对应的连续样本数。 */
#define ECG_DMA_HALF_LENGTH (ECG_DMA_BUFFER_LENGTH / 2U)

/* 标识循环 DMA 缓冲区的前半区或后半区，也用于归还对应的数据块。 */
typedef enum
{
  ECG_DMA_HALF_FIRST = 0U,
  ECG_DMA_HALF_SECOND = 1U
} ECG_DmaHalf;

/** @brief Result of requesting the next ready DMA half-buffer. */
typedef enum
{
  ECG_ACQUISITION_RESULT_NO_BLOCK = 0U,
  ECG_ACQUISITION_RESULT_BLOCK_READY,
  ECG_ACQUISITION_RESULT_INVALID_ARGUMENT,
  ECG_ACQUISITION_RESULT_BLOCK_ACTIVE,
  ECG_ACQUISITION_RESULT_FAULT_LATCHED
} ECG_AcquisitionResult;

/**
 * @brief 一次成功取得的 DMA 半区视图。
 *
 * samples 指向内部 DMA 缓冲区，不归调用方所有；取得后应处理并归还，
 * 归还后不得继续读取。first_sample_index 是连续采样计数中的块首序号，
 * 以 uint32_t 自然回绕，并非环形缓冲区内的 0/256 下标。
 */
typedef struct
{
  const uint16_t *samples;        /* 模块内部 DMA 缓冲区中该块的起始地址。 */
  uint32_t first_sample_index;    /* 该块首样本的连续 uint32_t 序号。 */
  uint16_t sample_count;          /* 本块有效样本数，当前固定为 ECG_DMA_HALF_LENGTH。 */
  ECG_DmaHalf half;               /* 传给 ReleaseBlock() 以归还本块。 */
} ECG_SampleBlock;

/**
 * @brief 清理软件状态并启动 ADC DMA 与 TIM2 触发。
 * @return 两步均成功时为 HAL_OK；失败时返回对应 HAL 状态。
 *         TIM2 启动失败时会尝试停止 ADC DMA；若回滚失败，返回停止操作的状态。
 * @note 由主循环/任务调用；ADC1 和 TIM2 必须已初始化且尚未运行。
 *       当前实现不支持运行中重复启动。
 */
HAL_StatusTypeDef ECG_Acquisition_Start(void);

/**
 * @brief 按前、后半区交替顺序取得一个已就绪块。
 * @param block 输出块信息，不得为 NULL。
 * @return 明确的请求结果，正常无数据时返回 NO_BLOCK；调用方应与 BLOCK_READY 比较。
 * @note 由主循环/任务调用。成功取得后，调用方必须在 DMA 重写该半区前处理并归还。
 */
ECG_AcquisitionResult ECG_Acquisition_TryGetBlock(ECG_SampleBlock *block);

/**
 * @brief 归还当前由调用方持有的 DMA 半区。
 * @param half 最近一次成功取得且尚未归还的半区。
 * @return 1 表示归还成功；0 表示半区值非法或与当前活动块不匹配。
 * @note 由主循环/任务调用；归还后不得继续访问该块的 samples 指针。
 */
uint8_t ECG_Acquisition_ReleaseBlock(ECG_DmaHalf half);

/** @brief 查询 DMA 覆盖故障是否已锁存；非零表示故障，只有 Start() 会清除。 */
uint8_t ECG_Acquisition_HasFault(void);

/** @brief 读取本次启动以来的 DMA 覆盖次数；故障锁存后每次启动最多计数一次。 */
uint32_t ECG_Acquisition_GetOverrunCount(void);

/** @brief 读取本次启动以来成功发布的累计样本数；uint32_t 自然回绕。 */
uint32_t ECG_Acquisition_GetSampleCount(void);

#endif
