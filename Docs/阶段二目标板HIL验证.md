# 阶段二目标板硬件在环验证

## 目标

验证 STM32F429 上的 ADC/TIM2/DMA、ADC 码到 ECG mV 换算、连续 10:1 均值降采样、1 kHz 50 Hz 陷波器及 DMA 块边界状态衔接。主机 CTest 不能替代本项目标板验证。

## 专用固件与钩子

- `HILValidation`：记录样本和逐块指标；累积 4096 个 1 kHz 陷波输出后停止 TIM2/ADC DMA 并冻结数据，便于通过 SWD 读取。
- `HILFilterFault`：第一块数据调用陷波器时故意请求超长块，验证滤波状态码经统一故障路径停机并显示 LCD 故障页。此固件不能用于信号频响测试。
- 捕获全局变量：`g_ecg_stage2_hil_capture`。`magic` 应为 `0xEC620001`，`dwt_enabled` 应为 1。
- 原始和校准数组各最多记录 4096 点；降采样前、后的 1 kHz 数组各记录 4096 点。`capture_complete` 和 `acquisition_stopped` 均为 1 时，采集数据已冻结。
- 逐块记录包含原始块序号、半区、降采样前后相位、输出点数、滤波状态、处理周期和 ready-to-release 周期。钩子不在 DMA ISR 中执行。
- HIL 固件额外占用约 68 KB RAM；生产固件默认关闭钩子。

## 构建

```powershell
cmake --preset HILValidation
cmake --build --preset HILValidation

# 单独验证滤波故障显示路径
cmake --preset HILFilterFault
cmake --build --preset HILFilterFault
```

使用 CMSIS-DAP/SWD 和 `build/HILValidation/ECG-backend.elf` 读取全局捕获结构。此专用固件完成一次捕获后会停止采集并等待调试器；每次更换输入频率后复位并重新运行。

## 信号源和接线前置条件

需要频率、幅值准确的信号发生器，以及已确认的注入点。若从 ADC1_IN12/PC2 注入，必须先确认/隔离板上前端输出，不能让信号发生器与前端输出相互驱动；ADC 输入信号需以 1.5 V 为中心，并始终处于 ADC 允许范围内。若从前端输入端注入，则应按前端电路的输入范围和增益配置幅值。不得将人体作为测试信号源。

只有确认板卡型号、模拟注入点及信号源接线后，才可进行模拟输入测试。已由用户确认连接的是本项目 STM32F429 目标板；目前没有信号发生器，因此尚未施加模拟测试信号，任何板端频率响应指标均未验收。

## 验收指标

### 采集与连续性

- 每个原始块为 256 点；以连续样本总数为准，降采样累计输出必须始终等于 `floor(total_raw_samples / 10)`。单块可产生 25 或 26 点，取决于跨块保留的分组相位。
- `sequence_error_count == 0`、`decimation_error_count == 0`、`filter_error_count == 0`、`dma_overrun_count == 0`。
- 一次完整捕获应有 `total_raw_samples == 40960`、`total_decimated_samples == 4096`、`block_record_count == 160`；原始捕获数组会在 4096 点后停止增长。
- `max_ready_to_release_cycles < 2560000`，即 100 MHz 下小于 25.6 ms；同时记录 `max_process_cycles`。HIL 捕获复制本身计入处理耗时。
- 所有滤波状态为 `IIR_NOTCH_STATUS_OK`，捕获的滤波输出应为有限值。

### 频率响应

对每次单频输入，使用同一份捕获中的降采样陷波前/后数据，忽略启动瞬态后计算幅值比：

- 50 Hz 衰减至少 60 dB；
- 49 Hz 和 51 Hz 响应均为 -3.01±0.1 dB；
- 1、10、40、60、100 Hz 通带偏差不超过 0.1 dB；
- 与主机测试相同的 0.1 mV 等效输入幅度下，通带输出应有限且无超过 1% 的幅度误差。

## 滤波错误屏幕测试

使用 `HILFilterFault` 固件启动后，应在第一块处理时返回 `IIR_NOTCH_STATUS_BLOCK_TOO_LARGE`，停止 TIM2 和 ADC DMA，并显示 `FILTER ERROR` 与状态码 `3`。复位后必须刷回普通固件或 `HILValidation` 固件。

LCD 字符绘制接口仅支持 16 或 32 像素字高。此前故障页传入 `2`，清屏后字符绘制因此被跳过；现已改为 16，需用重新构建的 `HILFilterFault` 固件复测显示。

## 实测记录

- 2026-11-21：通过 CMSIS-DAP/SWD 临时刷写并运行 `HILFilterFault`。SWD 读回 `g_ecg_fault_reason == ECG_FAULT_SIGNAL_FILTER`、`g_ecg_fault_detail == 3`、首块 `filter_status == 3`、`filter_error_count == 1`，首块包含 256 个原始样本。
- 故障后 `g_ecg_shutdown_status == 0`；TIM2 CR1 的 CEN 位和 DMA2 Stream 0 CR 的 EN 位均为 0，确认定时器及 ADC DMA 已停止。`g_lcd_initialized == 1`，但未能目视确认 LCD 上的文字与状态码。
- 测试后已刷回普通 `Debug` 固件。SWD 确认 `g_ecg_fault_reason == ECG_FAULT_SYSTEM_INIT`、LCD 已初始化、采集状态为运行中，TIM2 CEN 和 DMA Stream EN 位均为 1。
- 本次没有信号发生器；未运行 `HILValidation` 模拟频响捕获，也未验收滤波前后幅值、连续性和处理时序指标。
- 2026-11-22：用户通过 Watch 确认故障原因、状态码、停机状态、滤波错误计数及首块记录均符合预期。首次 LCD 故障页仍为黑屏，已定位为字体高度参数错误并修正为 16；修正后的固件已构建，等待手动烧录后目视复测。
- 2026-11-22：修正字体后的 HILFilterFault 固件由用户手动烧录，LCD 已正常显示滤波故障信息，目视复测通过。

## 当前状态

- [x] HIL 捕获钩子、DWT 计时、逐块序号/分组检查和滤波故障注入固件已实现并通过交叉编译。
- [x] 确认连接的是本项目 STM32F429 目标板。
- [ ] 准备信号发生器并确认安全的模拟注入点。
- [ ] 刷写 `HILValidation` 并完成各频点的模拟输入、捕获读取和指标核验。
- [x] 运行 `HILFilterFault` 并通过 SWD 确认滤波错误码及采集停机。
- [x] 目视确认 LCD 故障信息。
- [x] 刷回普通固件并通过 SWD 确认采集恢复运行。
