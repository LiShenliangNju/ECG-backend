/**
 * @brief ECG 连续采集模块内部实现。
 *
 * 数据流：TIM2 更新事件 → ADC1 转换 → circular DMA 的 512 点缓冲区
 * → DMA 半满/全满回调发布对应半区 → 主循环/任务取得、处理并归还。
 *
 * 两个 ADC DMA 回调运行于 ISR 上下文，只发布就绪事件或锁存覆盖故障；
 * 启动、取块、归还和查询由主循环/任务调用。就绪/活动位与块元数据交接
 * 通过保存-恢复式 PRIMASK 临界区协调；故障标志和计数器由 ISR 写、主循环读。
 * 故障锁存后不再发布数据，只能由 Start() 清除。
 *
 * 当前 TIM2 配置为 PSC=9、ARR=499、输入时钟 50 MHz，触发率 10 kHz；
 * 每个 256 点半区对应 25.6 ms。消费者处理和 DMA 中断响应必须满足半区重用时限，
 * 其最坏耗时仍需在目标板测量。
 */
#include "ecg_acquisition.h"

#include "adc.h"
#include "tim.h"

typedef union
{
  /* 使联合体至少按 uint32_t 对齐；DMA 的实际传输宽度由 ADC DMA 配置决定。 */
  uint32_t alignment;
  uint16_t samples[ECG_DMA_BUFFER_LENGTH];
} ECG_DmaBuffer;

/* DMA 硬件写入；主循环仅在取得对应块后读取，样本区所有权通过 active_mask 约束。 */
static ECG_DmaBuffer dma_buffer;

/* ISR 发布就绪位；主循环在临界区内取走就绪位，ISR 同时检查即将写入半区是否仍就绪。 */
static volatile uint8_t ready_mask;

/* 主循环在临界区内置位/清零；ISR 检查目标 DMA 半区是否仍被处理。 */
static volatile uint8_t active_mask;

/* ISR 锁存覆盖故障，主循环读取，Start() 在临界区内清除；故障后回调不再发布数据。 */
static volatile uint8_t acquisition_fault;

/* ISR 在首次检测到覆盖时递增，主循环读取，Start() 清零；自然对齐的 32 位访问在 Cortex-M4 上原子完成。 */
static volatile uint32_t overrun_count;

/* ISR 在成功发布半区时递增，主循环读取，Start() 清零；对齐 32 位访问原子完成，溢出按模 2^32 回绕。 */
static volatile uint32_t completed_sample_count;

/* ISR 在发布对应 ready 位前写入，主循环仅在临界区内取得该块时读取。 */
static uint32_t block_start_sample[2];

/* 主循环维护的交替消费位置；仅在成功取块后翻转，Start() 复位为前半区。 */
static uint8_t next_consume_half;

/**
 * @brief 保存当前中断屏蔽状态并进入短临界区。
 * @context 主循环/任务；模块的公开状态操作不应从 ISR 调用。
 * @return 调用前的 PRIMASK，供 ExitCriticalSection() 恢复。
 */
static uint32_t EnterCriticalSection(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

/**
 * @brief 恢复进入临界区前的中断屏蔽状态。
 * @context 主循环/任务。
 * @param primask EnterCriticalSection() 返回的原始 PRIMASK。
 */
static void ExitCriticalSection(uint32_t primask)
{
  if (primask == 0U)
  {
    /* 保留调用者原有的中断屏蔽状态，避免无条件开中断。 */
    __enable_irq();
  }
}

/**
 * @brief 发布一个已完成半区，并检查 DMA 下一步要写入的半区是否仍被占用。
 * @context DMA ISR。
 * @param completed_half 刚完成传输的半区。
 * @param next_write_half 循环 DMA 即将写入的半区。
 * @note 若目标半区仍就绪或正在处理，则锁存故障并计数；故障后不再发布。
 *       正常发布时先写块首序号和累计计数，再置就绪位。
 */
static void PublishCompletedHalf(ECG_DmaHalf completed_half,
                                 ECG_DmaHalf next_write_half)
{
  uint8_t completed_mask = (uint8_t)(1U << completed_half);
  uint8_t next_write_mask = (uint8_t)(1U << next_write_half);

  if ((acquisition_fault != 0U) ||
      (((ready_mask | active_mask) & next_write_mask) != 0U))
  {
    if (acquisition_fault == 0U)
    {
      acquisition_fault = 1U;
      overrun_count++;
    }
    return;
  }

  block_start_sample[completed_half] = completed_sample_count;
  completed_sample_count += ECG_DMA_HALF_LENGTH;
  /* 仅置当前半区的位，保留其他半区状态；就绪位在块元数据写完后发布。 */
  ready_mask |= completed_mask;
}

/**
 * @brief 清理软件状态并启动 ADC DMA 与 TIM2 触发。
 * @context 主循环/任务；外设须已初始化且尚未运行。
 * @note 先启动 DMA，再启动定时器，使首次触发前 DMA 已就绪。
 *       TIM2 启动失败时尝试停止 DMA；回滚失败时返回回滚错误状态。
 *       未检测重复启动，运行中再次调用会先清除软件状态，再由 HAL 返回错误。
 */
HAL_StatusTypeDef ECG_Acquisition_Start(void)
{
  HAL_StatusTypeDef status;
  uint32_t primask = EnterCriticalSection();
  ready_mask = 0U;
  active_mask = 0U;
  acquisition_fault = 0U;
  overrun_count = 0U;
  completed_sample_count = 0U;
  block_start_sample[ECG_DMA_HALF_FIRST] = 0U;
  block_start_sample[ECG_DMA_HALF_SECOND] = 0U;
  next_consume_half = ECG_DMA_HALF_FIRST;
  ExitCriticalSection(primask);

  /* HAL 接口接收 uint32_t *；实际搬运宽度由已配置的 half-word DMA 决定。 */
  status = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)dma_buffer.samples,
                             ECG_DMA_BUFFER_LENGTH);
  if (status != HAL_OK)
  {
    return status;
  }

  status = HAL_TIM_Base_Start(&htim2);
  if (status != HAL_OK)
  {
    HAL_StatusTypeDef stop_status = HAL_ADC_Stop_DMA(&hadc1);
    if (stop_status != HAL_OK)
    {
      return stop_status;
    }
    return status;
  }

  return HAL_OK;
}

/**
 * @brief 在短临界区内按序取得一个已就绪半区。
 * @context 主循环/任务；不得从 ISR 调用。
 * @param block 输出块信息；NULL 时不修改状态并返回参数无效结果。
 * @return 区分块就绪、无数据、参数无效、已有活动块和故障锁存的结果。
 * @note 成功后 ready 位清除、active 位设置；消费顺序严格为前、后半区交替。
 */
ECG_AcquisitionResult ECG_Acquisition_TryGetBlock(ECG_SampleBlock *block)
{
  if (block == NULL)
  {
    return ECG_ACQUISITION_RESULT_INVALID_ARGUMENT;
  }

  uint32_t primask = EnterCriticalSection();
  uint8_t half_mask = (uint8_t)(1U << next_consume_half);
  if (acquisition_fault != 0U)
  {
    ExitCriticalSection(primask);
    return ECG_ACQUISITION_RESULT_FAULT_LATCHED;
  }
  if (active_mask != 0U)
  {
    ExitCriticalSection(primask);
    return ECG_ACQUISITION_RESULT_BLOCK_ACTIVE;
  }
  if ((ready_mask & half_mask) == 0U)
  {
    ExitCriticalSection(primask);
    return ECG_ACQUISITION_RESULT_NO_BLOCK;
  }

  /* 在同一临界区完成所有权交接，避免 ISR 在交接间隙误判半区可复用。 */
  ready_mask &= (uint8_t)~half_mask;
  active_mask = half_mask;
  block->half = (ECG_DmaHalf)next_consume_half;
  block->samples = &dma_buffer.samples[next_consume_half * ECG_DMA_HALF_LENGTH];
  block->first_sample_index = block_start_sample[next_consume_half];
  block->sample_count = ECG_DMA_HALF_LENGTH;
  /* 仅在成功取得后推进，确保空闲轮询不会跳过半区。 */
  next_consume_half ^= 1U;
  ExitCriticalSection(primask);
  return ECG_ACQUISITION_RESULT_BLOCK_READY;
}

/**
 * @brief 结束调用方对当前半区的持有。
 * @context 主循环/任务；应在处理完样本后调用。
 * @param half 要归还的半区。
 * @return 1 表示当前活动半区已释放；非法半区或状态不匹配时返回 0。
 * @note active_mask 必须与传入半区完全匹配，避免错误归还另一半区。
 */
uint8_t ECG_Acquisition_ReleaseBlock(ECG_DmaHalf half)
{
  if ((half != ECG_DMA_HALF_FIRST) && (half != ECG_DMA_HALF_SECOND))
  {
    return 0U;
  }

  uint32_t primask = EnterCriticalSection();
  uint8_t half_mask = (uint8_t)(1U << half);
  if (active_mask != half_mask)
  {
    ExitCriticalSection(primask);
    return 0U;
  }

  active_mask = 0U;
  ExitCriticalSection(primask);
  return 1U;
}

/** @brief 主循环读取 ISR 锁存的覆盖状态；该标志只能由 Start() 清除。 */
uint8_t ECG_Acquisition_HasFault(void)
{
  return acquisition_fault;
}

/** @brief 主循环读取 ISR 维护的覆盖计数；当前锁存策略下每次启动最多为 1。 */
uint32_t ECG_Acquisition_GetOverrunCount(void)
{
  return overrun_count;
}

/** @brief 主循环读取 ISR 维护的成功发布样本数；32 位无符号计数自然回绕。 */
uint32_t ECG_Acquisition_GetSampleCount(void)
{
  return completed_sample_count;
}

/**
 * @brief ADC1 DMA 半传输回调，将前半区交给采集状态机。
 * @context DMA ISR。
 * @param hadc 触发此回调的 ADC 句柄。
 * @note 只发布事件，不处理样本。
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    PublishCompletedHalf(ECG_DMA_HALF_FIRST, ECG_DMA_HALF_SECOND);
  }
}

/**
 * @brief ADC1 DMA 全传输回调，将后半区交给采集状态机。
 * @context DMA ISR。
 * @param hadc 触发此回调的 ADC 句柄。
 * @note 只发布事件，不处理样本。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    PublishCompletedHalf(ECG_DMA_HALF_SECOND, ECG_DMA_HALF_FIRST);
  }
}
