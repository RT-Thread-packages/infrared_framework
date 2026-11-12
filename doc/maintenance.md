# 诊断与维护

[返回 README](../README.md) · [配置](configuration.md) · [接收](reception.md) · [发送](transmission-codec.md)

## 1. 可选 MSH

只有同时启用 `INFRARED_USING_DIAGNOSTICS`、`INFRARED_USING_MSH` 及其 FINSH 依赖后，`SConscript` 才编译 `framework/examples/infrared_msh.c`。NEC 命令另依赖 `INFRARED_FRAMEWORK_NEC_CODEC`，由 `framework/codecs/nec/nec_msh.c` 实现，通用命令仅分发。

| 命令 | 用途 |
|---|---|
| `ir list`、`ir info N` | 查看注册数量及固定索引、逻辑设备状态与 RX 帧队列占用 |
| `ir open N`、`ir close N` | 通过标准 `rt_device_open/close` 操作一次；直接报告结果，不存在引用归零后的专用清理或重试入口 |
| `ir poll N` | 由当前 MSH 线程处理一个原始脉冲，不保证得到完整协议帧 |
| `ir read N` | 按帧读取已解码结果 |
| `ir flush N now` | 立即清空原始脉冲、Codec 状态和已解码帧队列，成功返回代表后端已确认清空 |
| `ir flush N defer` | 仅登记待清空请求，下一次 `infrared_device_poll()` 开始时执行，控制命令成功不等于已完成清空 |
| `ir tx N HEX` | 按十六进制字节提交一条已注册 Codec 对应的协议帧 |
| `ir codec list` | 列出已选编且接入分发的 Codec 命令 |
| `ir codec nec read N` | 按 NEC 地址、命令和重复数展示已解码帧 |
| `ir codec nec tx N AA CC [repeats]` | 发送 NEC 地址 AA、命令 CC，重复数限制 0～8 |

`N` 是固定索引（不是已注册的第 N 个实例）；`ir codec nec ...` 只面向实际绑定 NEC 的设备，不进行运行时协议切换。`ir open/close` 与 RX/TX、ISR 的并发控制属于应用责任。OPEN/CLOSE 失败时只进行本次调用的一次尽力回滚/关闭，记录错误码及日志，**不再调用 `infrared_device_cleanup()`**。当物理停止失败后，不能仅因逻辑 `ref_count` 已归零就认定资源回收成功；业务层根据 BSP 情况处理硬件故障，不应盲目重复 `open/close`。

`INFRARED_CTRL_RX_FLUSH` 要求 `args` 指向一个 `rt_bool_t`（`RT_TRUE` 即时，`RT_FALSE` 延迟），不能再传 `RT_NULL`。清空要求与应用 `poll()`、读帧及设备关闭串行化；Capture 驱动若未提供 `INPUTCAPTURE_CMD_CLEAR_BUF` 或返回失败，`now` 返回错误而不是假报成功。自动 Overflow 仍遵循独立的异常恢复逻辑，不会删除已经解码的合法帧。

## 2. 日志与诊断信息

FRAMEWORK 源码使用 `DBG_TAG`、`DBG_LVL DBG_INFO` 和 `<rtdbg.h>` 输出 `LOG_E/W/I/D`。开启 `RT_USING_ULOG` 时经 ULOG 处理，否则是否在 `rt_kprintf` 输出取决于工程配置。框架坚持**错误即上报**：没有自动恢复线程、重试状态机或 refcount 修正；不把错误日志等同于清理成功，操作结果仍以返回值及实际硬件状态为准。

`infrared_device_get_info()` 只有在 `INFRARED_USING_DIAGNOSTICS` 下声明并实现；它报告逻辑 `opened`、RX/TX 后端标识、配置与已完成帧队列数量，不包含 PIN 原始脉冲深度或 PWM ISR 游标。启用 ULOG 时，其输出级别、后端及时间戳由目标工程配置；不启用时需检查目标 RT-Thread 的调试开关是否允许 `rtdbg.h` 向控制台输出。

## 3. 故障定位索引

| 现象 / 返回 | 查找位置及直接核对项 |
|---|---|
| `infrared_device_register()` 失败 | `infrared_device.c`：索引容量、重名、Codec 解析上下文；Kconfig 资源池容量独立于注册槽位 |
| 打开 PIN 失败 | `framework/src/backends/input/infrared_pin.c::infrared_pin_open()`：检查 HWTIMER、GPIO IRQ、FIFO 容量；打开失败尝试一次回滚，物理解绑失败不释放仍可能被 ISR 访问的缓存 |
| 打开 Capture 失败 | `framework/src/backends/input/infrared_capture.c::infrared_capture_start()`：检查 BSP 设备、`INPUTCAPTURE_CMD_SET_WATERMARK` 和回调绑定；错误通过业务调用层上报，必要的本次关闭只执行一次 |
| TX 打开或启动失败 | `framework/src/backends/output/infrared_pwm.c::infrared_pwm_open()`、`infrared_pwm_arm()`：检查 PWM 通道、HWTIMER 与 `rt_device_write()`；载波禁用失败不交出通道声明，TX 返回帧数并记录底层错误日志 |
| `poll()` 返回 `-RT_EEMPTY` | 当前后端没有可读取的完整原始脉冲，或 Capture 驱动尚无样本 |
| `poll()` 返回 `-RT_EFULL` | 完整帧队列无一条记录空间；先读取已完成帧，再继续解析 |
| 收到 Overflow / NEC 无效 | `infrared_report_overflow()`、`nec_decode()`：丢样、MARK/SPACE 电平映射、时间容差、起始间隔 |
| PIN 时间戳跨周期后异常 | `framework/src/backends/input/infrared_pin.c::infrared_pin_irq()`：周期内读值、累计读值和采样间隔是否超过一个 1 秒周期 |
| 关闭 PWM 返回错误 | `framework/src/backends/output/infrared_pwm.c::infrared_pwm_close()`：载波停止、定时器回调解绑与物理关闭失败点；仅单次尽力关闭，日志上报，业务/BSP 决定后续恢复 |
| 发送阻塞或超时 | `infrared_send_batch()`：Completion、ISR 回调是否安装、包络定时重装、`timeout_ticks` |
| 注册成功却没有 Event 线程 | `INFRARED_USING_THREAD_EXAMPLE` 关闭时只注册示例设备；框架核心不创建线程 |

## 4. 变更影响位置

- 新增硬件输入：`framework/inc/infrared_backend.h`（后端接口）+ `framework/src/backends/input/`（适配器实现）+ `Kconfig` / `SConscript`（选编与容量），并核对物理资源的独占声明。
- 改变 PIN 时间戳行为：`framework/src/backends/input/infrared_pin.c::infrared_pin_irq()`，同时核对 `infrared_pin_start()` 的 1 秒周期配置与 BSP 的 HWTIMER 读值语义。
- 改变 PWM 时序或关闭行为：`framework/src/backends/output/infrared_pwm.c::infrared_pwm_timeout()`、`infrared_pwm_close()`，核对 HWTIMER ISR、`rt_completion_done()` 与物理关闭失败时的资源保留顺序。
- 新增 Codec：`framework/inc/infrared_codec.h` + `framework/codecs/<name>/`（协议目录占位写法；现有实现位于 `framework/codecs/nec/`）；每实例上下文由 Codec 管理，MSH 命令是可选扩展。
- 改变逻辑注册和帧 IO：`framework/src/infrared_device.c`、`framework/src/infrared_private.h`，同时核对 `framework/inc/infrared_device.h` 的公开接口消费者。
- 修改编译配置：`Kconfig`、`SConscript`、`framework/inc/infrared_framework_config.h`；检查关闭模块后的选源关系与链接引用。

