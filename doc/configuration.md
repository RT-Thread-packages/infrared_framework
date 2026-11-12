# 配置、构建与设备接入

[返回 README](../README.md) · [架构](architecture.md) · [维护](maintenance.md)

## 1. 配置入口

在目标 RT-Thread 工程的 `menuconfig` 中使用以下常见菜单路径：

```text
RT-Thread online packages
  peripheral libraries and drivers  --->
    [*] infrared : infrared is base on rt-thread pin,hwtimer and pwm.  --->
        Infrared implementation  --->
            ( ) LEGACY
            (X) FRAMEWORK
```

勾选软件包即配置 `PKG_USING_INFRARED`；模式选择是 Kconfig 的 `choice`，默认 LEGACY。菜单具体层级以已安装的软件包索引为准。软件包配置载入根目录 `Kconfig`。`SConscript` 用 `GetDepend()` 选择 LEGACY 或 FRAMEWORK 文件与头文件路径；不存在在构建前必须运行的 `gen_infrared_callbacks.py`。

| 配置 | 作用与约束 |
|---|---|
| `PKG_USING_INFRARED` | 软件包总开关 |
| `INFRARED_LEGACY` / `INFRARED_FRAMEWORK` | `choice` 互斥，默认 LEGACY；FRAMEWORK 不依赖 `RT_USING_ULOG` |
| `INFRARED_MAX_DEVICES` | 注册表容量，范围 1～16、默认 3；不等于实际注册个数 |
| `INFRARED_DEVICE_PIN` | PIN + HWTIMER 后端；需要 `RT_USING_PIN` 和 `RT_USING_HWTIMER` |
| `INFRARED_USING_INPUT_CAPTURE` | Input Capture；需要 `RT_USING_INPUT_CAPTURE`、`RT_USING_HEAP` |
| `INFRARED_DEVICE_PWM` | PWM + HWTIMER；需要 `RT_USING_PWM` 和 `RT_USING_HWTIMER` |
| `INFRARED_FRAMEWORK_NEC_CODEC` | 选编 NEC Codec，默认 `y`（仅 FRAMEWORK） |
| `INFRARED_NEC_CONTEXT_COUNT` | NEC 同时占用的解析状态槽数，范围 `1..INFRARED_MAX_DEVICES` |
| `INFRARED_USING_EXAMPLE` | 选编最多 3 个 Kconfig 配置槽位，须启用 NEC、组件初始化 |
| `INFRARED_USING_THREAD_EXAMPLE` | 创建示例 Event 接收线程；另需 `RT_USING_EVENT`、`RT_USING_HEAP` |
| `INFRARED_USING_DIAGNOSTICS` | 编译设备信息快照 API |
| `INFRARED_USING_MSH` | MSH 诊断；依赖 FINSH MSH 和诊断开关 |
| `INFRARED_CHECK_ALL`、`INFRARED_CHECK_PARAMETERS/CONFIG/RESOURCE/DEVICE` | 可选预检、配置检查与设备诊断编译开关；不代替必要的硬件清理 |

`INFRARED_KCONFIG_SLOT_COUNT` 是**示例声明槽位数**（1～3），`INFRARED_KCONFIG_DEVICE0/1/2` 决定各槽位是否真的参与注册；Kconfig 隐藏整数选择器 `INFRARED_DEVn_RX_BACKEND`（0=无、1=PIN、2=Capture）和 `INFRARED_DEVn_TX_BACKEND`（0=无、1=PWM）被 `IR_DEFINE_DEVICE(n, codec_ops)` 使用。不能将未定义的设备配置当成有效实例。

## 2. 手动注册：PIN 接收 + PWM 发送示例

以下名称、引脚是已有 Kconfig 示例的**默认值**，并非目标 MCU/BSP 保证；按板级实际注册对象替换。此示例使用固定逻辑索引 0，需要预留额外手动后端容量。

```c
#include "infrared_device.h"
#include "infrared_pin.h"
#include "infrared_pwm.h"
#include "nec_codec.h"

static const struct infrared_pin_config rx_cfg = {
    .timer_name = "timer16", .pin = 17, .active_level = 0, .timer_hz = 0
};
static const struct infrared_pwm_config tx_cfg = {
    .pwm_name = "pwm4", .pwm_channel = 3, .timer_name = "timer15"
};

static rt_err_t ir0_tx_callback(rt_device_t physical, rt_size_t size)
{
    return infrared_device_dispatch_tx(0, physical, size);
}

static const struct infrared_device_config ir_cfg = {
    .index = 0, .name = "ir0",
    .input = { .ops = &infrared_pin_ops, .settings = &rx_cfg },
    .output = { .ops = &infrared_pwm_ops, .settings = &tx_cfg,
                .timeout_ticks = 0, .indicate = ir0_tx_callback },
    .codec = { .ops = &infrared_nec_codec, .context = RT_NULL }
};

static int ir_app_init(void)
{
    rt_err_t ret = infrared_device_register(&ir_cfg);
    struct infrared_device *ir;
    if (ret != RT_EOK)
        return ret;
    ir = infrared_device_get(0);
    return rt_device_open(infrared_device_rt_device(ir), RT_DEVICE_OFLAG_RDWR);
}
INIT_APP_EXPORT(ir_app_init);
```

`infrared_output_config.timeout_ticks` 为 `rt_int32_t`：`0` 或 `RT_WAITING_FOREVER`（`-1`）表示永久等待，正整数为有限 Tick 超时。其他负数及超过该类型表示范围的赋值由应用负责，框架不额外检查。

Kconfig 应启用 PIN、PWM、NEC，并至少为手动实例配置 `INFRARED_MANUAL_PIN_CAPACITY=1`、`INFRARED_MANUAL_PWM_CAPACITY=1`；若没有额外配置式设备，`INFRARED_MANUAL_CAPTURE_CAPACITY` 可为 0。上述示例只负责注册和打开设备；应用还须安排自己的 `poll()` 消费线程或通知机制。应用要在 `RT_USING_COMPONENTS_INIT` 可用、所需物理设备已经由 BSP 注册后初始化，并自行协调与其他 `INIT_APP_EXPORT` 入口的先后顺序。

手动 Input Capture 配置需指定 `infrared_capture_ops` 和 `infrared_capture_config`，同时提供固定索引的 `infrared_device_dispatch_rx(index, ...)` 回调。PIN 的 IRQ callback 不需要额外 trampoline。`config.settings` 与指定的 Codec 外部 `context` 必须在设备生命周期保持有效。

## 3. Kconfig 示例与自定义宏

启用 `INFRARED_USING_EXAMPLE` 后，`framework/examples/infrared_example.c` 对启用的每个槽位调用 `IR_DEFINE_DEVICE(n, &infrared_nec_codec)`，展开 `ir_cfg_device_n` 和需要的固定回调，最后由 `infrared_example_init()` 注册。这个宏集中在 `framework/inc/infrared_device_define.h`，可供应用使用，但要求该槽位的 `INFRARED_DEVn_*` 编译期配置实际存在。

`INFRARED_USING_THREAD_EXAMPLE` 另需满足 Event/Heap 依赖；示例初始化会注册、打开实例并启动一个工作线程。不应同时由应用重复注册同一 `index` 或重复启动相同示例设备。若不启用示例，应用可独立使用注册 API，不依赖示例数组。

## 4. RAM / 静态容量口径

设 `D=INFRARED_MAX_DEVICES`、`R=IR_CONFIGURED_RX_COUNT`、`P=IR_CONFIGURED_PIN_COUNT`、`T=IR_CONFIGURED_TX_COUNT`、`N=INFRARED_NEC_CONTEXT_COUNT`，其中 `R=P+IR_CONFIGURED_CAPTURE_COUNT`，各数量由 Kconfig 选中的后端和 `INFRARED_MANUAL_*_CAPACITY` 推导。静态分配口径如下：

| 分配项 | 来源 | 计算口径 |
|---|---|---|
| 注册设备结构 | `infrared_device.c` | `D × sizeof(struct infrared_device)` |
| 接收完整帧数组 | `infrared_device.c` | 若 R 非零：`R × INFRARED_FRAME_CAPACITY × sizeof(struct infrared_frame)`，另有 R 个 `rt_bool_t` 标记 |
| PIN 原始脉冲 FIFO | `infrared_pin.c` | 若 P 非零：`P × INFRARED_PIN_PULSE_CAPACITY × sizeof(infrared_pulse_t)`，另有 P 个占用标记 |
| PWM 发送状态池 | `infrared_pwm.c` | 若 T 非零：`T × sizeof(struct infrared_pwm_state)`；每个状态已**包含** `INFRARED_TX_PULSE_CAPACITY` 条脉冲数组，不能重复相加 |
| NEC 上下文池 | `nec_codec.c` | NEC 选编时：`N × sizeof(struct infrared_nec_slot)` |
| 可选事件线程 | `infrared_example.c` | 线程栈配置为 `INFRARED_THREAD_EXAMPLE_STACK`；运行时线程控制块及 RT-Thread 堆开销另计 |

以上是源码声明量计算，不是链接 MAP、实机 RAM 峰值或堆使用测量。Capture BSP 内部堆缓冲、系统线程及驱动结构应另外核算。`sizeof()` 随 RT-Thread 配置、类型宽度及对齐变化；禁用相应后端时对应状态池可能不编入。

## 5. 异常返回与责任边界

OPEN/CLOSE 和接收接口返回底层 `rt_err_t`；写帧接口受既有 `rt_size_t` 契约限制，返回已完成帧数并通过日志暴露硬件错误。资源打开失败只回滚一次，关闭只执行一次尽力拆卸；框架不自动重试、不修复物理 `ref_count`，若 IRQ 或资源关闭失败则保持最低限度的独占占用以避免悬空 ISR。后续硬件重置、BSP 修复和业务重启由应用决定。

## 6. 构建与 BSP 核验边界

可从 Kconfig、`SConscript` 确定选编关系；本包没有独立的 `SConstruct` 或目标板配置。集成时在目标 RT-Thread 项目中通过原有 `menuconfig`/软件包流程更新 `rtconfig.h`，再使用该工程自己的 SCons 入口构建。此处不虚构 BSP 名称、交叉工具链路径或已验证的编译命令。建议实际核对所用 RT-Thread 源树的 `rt_device_set_rx_indicate`、Completion/Event、Input Capture 控制命令与 GPIO/HWTIMER 驱动语义。
