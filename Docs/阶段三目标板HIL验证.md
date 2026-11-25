# 阶段三目标板 HIL 验证

## 目标与捕获长度

`HILValidation` 在真实 STM32F429 上运行 ADC → 校准 → 10:1 降采样 → 陷波 → R 峰 → 心率/每搏测量整条路径，并通过 SWD 读取结果。R 峰算法边界条件仍由主机测试覆盖；此处重点检查实际信号链、DMA 块边界连续性和模块衔接。

捕获长度为 **12800 个 1 kHz 样本（12.8 秒）**：对应 128000 个 10 kHz 原始样本、500 个 256 点 DMA 半区，且同时被 10 点降采样分组整除。因此捕获结束时不会多取或少取一个完整 DMA 块。12.8 秒也为首峰建立基准及连续 5 个最长有效 RR（1375 ms）留有余量，并覆盖最多 29 点的峰确认延迟。

## 测试钩子

- `g_ecg_stage3_hil_capture`：`magic == 0xEC630001`、`sample_target == 12800`；`processed_sample_count` 到达目标后 `capture_complete == 1`。主循环完成该 DMA 块的归还后停止采集。
- `peaks[]`：按确认顺序记录峰时间戳、峰值、RR、瞬时/平均 BPM、RR 范围标志、稳定标志及每搏 min/max/峰峰值。首峰只建立基准，故其 `has_interval` 和 `measurement_valid` 应为 0；后续峰应能完成相邻 R 峰之间的测量。
- `peak_count` 是已检测峰总数；`peak_record_count` 是已保存记录数；`peak_record_overflow_count` 应为 0。记录容量 32，覆盖 12.8 秒内 410 ms 最短有效 RR 的预期事件数。
- `g_ecg_stage2_hil_capture` 继续记录 DMA/处理计时和序号、降采样、滤波错误，并通过 `lcd_service_call_count`、`max_lcd_service_cycles` 记录 LCD 服务调用数和最大单次执行周期。该周期只测量主循环调用 `ECG_Display_Service` 的同步执行时长，不含调用间的 SPI DMA 传输时间。阶段二波形数组仅保留前 4096 点，不随本阶段延长；块记录容量为 512，以容纳 500 个 DMA 半区。
- 阶段三捕获仅在 `ECG_STAGE3_HIL` 下启用；阶段二及 LCD 计时钩子在 `ECG_STAGE2_HIL` 下启用。钩子不在 DMA ISR 中执行，Release/普通 Debug 构建默认不包含 HIL 捕获存储。

## 输入与操作

需要 ECG 模拟器或信号发生器，并先确认注入点及前端隔离方式。输入应产生阈值以上、形态明确的正向 QRS/R 峰，建议先用约 1 秒 RR 的规律波形，便于观察稳定状态；不要将人体作为信号源。模拟量必须满足板上前端及 ADC 输入范围。

```powershell
cmake --preset HILValidation
cmake --build --preset HILValidation
```

连接调试器并刷写 `build/HILValidation/ECG-backend.elf`，接入已确认的模拟信号源后复位运行。等待 `g_ecg_stage3_hil_capture.capture_complete == 1` 且 `g_ecg_stage2_hil_capture.acquisition_stopped == 1`，再通过 SWD 读取捕获结构。每次更改输入后复位重测。

## 验收检查

- 阶段三捕获：`magic == 0xEC630001`、`processed_sample_count == 12800`、`capture_complete == 1`。
- 阶段二采集统计：`total_raw_samples == 128000`、`total_decimated_samples == 12800`、`block_record_count == 500`；`sequence_error_count`、`decimation_error_count`、`filter_error_count`、`dma_overrun_count`、`block_record_overflow_count` 均为 0。
- `acquisition_stopped == 1`，定时器及 ADC DMA 停止状态成功；`max_ready_to_release_cycles < 2560000`（100 MHz 下 25.6 ms）。
- `lcd_service_call_count > 0`；读取 `max_lcd_service_cycles`，以 100 MHz 换算为毫秒并确认单次 `ECG_Display_Service` 执行时间小于 25.6 ms。
- 峰记录应与输入节律对应，峰时间戳单调连续（无符号回绕除外）。有效 RR 应在 410–1375 ms；连续 5 个有效 RR 后 `stable == 1`。每搏测量从第二个峰开始有效，峰峰值应与校准后波形的区间 min/max 相符。
- 若峰数为 0、RR 不符或幅值偏差异常，先检查输入信号、模拟接线和 ADC 校准；不能据此替代已有的主机算法边界测试。

测试完成后，复位并重新刷写普通 `Debug` 固件，避免将 HIL 专用固件留在目标板上。当前工作区尚无本阶段实板捕获记录；没有确认信号源和安全注入点前，不进行模拟输入测试。
