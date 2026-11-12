# infrared

RT-Thread 通用红外软件包。顶层 Kconfig **互斥**选择 `LEGACY`（单设备 API）或 `FRAMEWORK`（多实例设备框架）。两种模式分别编译，互不依赖对方的全局状态。

## 目录结构

```text
infrared/
├── doc/
│   ├── infrared_frame.png
│   ├── architecture.md
│   ├── reception.md
│   ├── transmission-codec.md
│   ├── configuration.md
│   └── maintenance.md
├── inc/                          单设备模式公共接口
│   ├── decoder.h
│   ├── drv_infrared.h
│   └── infrared.h
├── src/                          单设备模式实现
│   ├── drv_infrared.c
│   ├── infrared.c
│   └── nec_decoder.c
├── framework/
│   ├── inc/
│   │   ├── infrared_backend.h       通用 RX/TX 后端操作表
│   │   ├── infrared_codec.h         通用帧格式及流式 Codec
│   │   ├── infrared_device_define.h  可复用固定实例声明宏
│   │   ├── infrared_device.h        设备注册、索引、收发接口
│   │   └── infrared_framework_config.h
│   ├── src/
│   │   ├── infrared_device.c        唯一实例表、帧队列、资源管理
│   │   ├── infrared_callback.c      固定索引的 RX/TX 中断回调
│   │   ├── infrared_private.h       内部实例状态
│   │   └── backends/
│   │       ├── input/
│   │       │   ├── infrared_pin.c
│   │       │   ├── infrared_pin.h
│   │       │   ├── infrared_capture.c
│   │       │   └── infrared_capture.h
│   │       └── output/
│   │           ├── infrared_pwm.c
│   │           └── infrared_pwm.h
│   ├── codecs/
│   │   └── nec/
│   │       ├── nec_codec.c
│   │       ├── nec_codec.h
│   │       ├── nec_msh.c
│   │       └── nec_msh.h
│   └── examples/
│       ├── infrared_example.c       Kconfig 注册与事件驱动示例
│       └── infrared_msh.c           可选 ir 命令行诊断
├── Kconfig
├── SConscript
├── LICENSE
└── README.md
```

## 选择 LEGACY 或 FRAMEWORK

在 RT-Thread ENV 的 `menuconfig` 中，先从软件包列表找到并勾选红外软件包，再进入它的实现模式选择：

```text
RT-Thread online packages
  peripheral libraries and drivers  --->
    [*] infrared : infrared is base on rt-thread pin,hwtimer and pwm.  --->
        Infrared implementation  --->
            ( ) LEGACY: original infrared driver and decoder interface
            (X) FRAMEWORK: independent infrared device instances
```

菜单名称以所用 RT-Thread 软件包索引版本为准；该软件包入口对应 `PKG_USING_INFRARED`，模式由互斥的 `INFRARED_LEGACY` / `INFRARED_FRAMEWORK` 决定。然后在 FRAMEWORK 菜单中选择 PIN/Capture/PWM、Codec、容量以及可选示例。配置保存后由目标 BSP 工程更新 `rtconfig.h`，再用工程自身 SCons 构建。

- **LEGACY**：使用 `infrared_init()`、`infrared_read()`、`infrared_write()`、`ir_decoder_register()`，可通过 `PKG_USING_DRV_INFRARED` 选择 PIN/HWTIMER/PWM 驱动。
- **FRAMEWORK**：独立的 `infrared_device_register()`、`infrared_device_get()`、`infrared_device_poll()` 和标准 RT-Thread `rt_device_read/write` 接口。编译框架核心、按需选用输入/输出后端、Codec 与示例，不需要额外生成源码。`RT_USING_ULOG` **不是必需项**：源码使用 `DBG_TAG`、`DBG_LVL DBG_INFO` 和 `rtdbg.h`；ULOG 开启时由 ULOG 处理，未开启时能否通过 `rt_kprintf` 输出取决于目标 RT-Thread 的调试开关及控制台配置。

`INFRARED_MAX_DEVICES` 只决定最大设备数量（1～16，默认 3），**不会自动注册设备，也不强制配置三个 Kconfig 设备**。应用可以手动注册 `index=1`，同时不注册 `index=0`。索引不会按照注册顺序压缩。

## 手动注册 FRAMEWORK 设备

示例使用 BSP 已注册的 `timer16`、`timer15`、`pwm4`，需开启 `INFRARED_DEVICE_PIN`、`INFRARED_DEVICE_PWM` 和 `INFRARED_FRAMEWORK_NEC_CODEC`，并确认实际板级设备名称与引脚对应：

```c
#include "infrared_device.h"
#include "infrared_pin.h"
#include "infrared_pwm.h"
#include "nec_codec.h"

static const struct infrared_pin_config my_rx = {
    .timer_name = "timer16", .pin = 17, .active_level = 0, .timer_hz = 0
};
static const struct infrared_pwm_config my_tx = {
    .pwm_name = "pwm4", .pwm_channel = 3, .timer_name = "timer15"
};
/* HWTIMER 物理回调没有用户参数，固定映射到 index=0。 */
static rt_err_t my_ir_tx_indicate(rt_device_t physical, rt_size_t size)
{
    return infrared_device_dispatch_tx(0, physical, size);
}
static const struct infrared_device_config my_ir_config = {
    .index = 0, .name = "ir0",
    .input = { .ops = &infrared_pin_ops, .settings = &my_rx },
    .output = { .ops = &infrared_pwm_ops, .settings = &my_tx,
                .indicate = my_ir_tx_indicate },
    .codec = { .ops = &infrared_nec_codec, .context = RT_NULL }
};

static int my_ir_init(void)
{
    rt_err_t ret = infrared_device_register(&my_ir_config);
    struct infrared_device *ir;
    if (ret != RT_EOK)
        return ret;
    ir = infrared_device_get(0);
    return rt_device_open(infrared_device_rt_device(ir), RT_DEVICE_OFLAG_RDWR);
}
INIT_APP_EXPORT(my_ir_init);
```

配置式设备也可在应用源文件中包含 `infrared_device_define.h`，使用 `IR_DEFINE_DEVICE(n, &infrared_nec_codec)` 展开固定实例、PIN/Capture/PWM 参数和回调；该宏要求相应的 `INFRARED_DEVn_*` 配置符号已经定义，注册时引用展开生成的 `ir_cfg_device_n`。它不提供动态 Codec 切换，也不会生成额外源码文件。

以上代码仅是应用示例，不是强制的注册入口。`infrared_nec_codec` 按 `INFRARED_NEC_CONTEXT_COUNT` 配置紧凑解析状态池，并将稀疏设备索引映射到独立状态；其他协议应自行实现 `infrared_codec_ops` 并提供持久有效的上下文。注册配置中的后端 `settings` 必须保持有效，直至设备停止使用。手动配置的 Input Capture 和 PWM 也必须提供固定索引回调；PIN 使用 IRQ 参数直接定位实例。

没有启用事件工作线程时，应用可以手动调用 `infrared_device_poll(ir)`，每次读取并解码一个原始脉冲，不阻塞也不创建线程。应用也可以在输入通知回调中发送 RT-Thread Event，由工作线程在被唤醒后执行解码。底层 PIN/Capture 的 Ringbuffer 保存原始脉冲，设备帧 Ringbuffer 保存已解码的完整帧；Ringbuffer 不会自行调用 Codec。`infrared_device_read_frames()`、`infrared_device_write_frames()` 和 RT-Thread 设备 `read/write` 的 `size` **均为完整协议帧的数量，不是字节数**。

接收可选 PIN/HWTIMER 或 Input Capture（单个设备只能选其一）；输入后端状态使用框架私有 `union`。PWM 发送端的编码缓冲、ISR 游标与完成结果位于后端自己的状态池，由框架 TX 的 `user_data` 关联；不占用 RT-Thread 设备对象的 `user_data`，以免与 BSP 或应用已有数据冲突。同一个实例可以同时配置接收和发送。当前 PWM 发送后端同时需要 PWM（生成载波）与 HWTIMER（包络计时），两者**不是互斥资源**。PWM 同设备不同通道可共享，但需要相同载波周期；同通道及同物理收发计时器不允许重复占用。

编码输出采用后端持有的稳定线性 32 位脉冲数组，以 `count/index` 驱动 ISR；并非等待其他生产者追加数据的 TX ringbuffer。原始脉冲数据高四位为 `0xA`（MARK）、`0xB`（SPACE），低 28 位为持续时间，单位微秒。

## 可选 Kconfig 示例与 MSH

`INFRARED_USING_EXAMPLE` 可编译 `framework/examples/infrared_example.c`：最多配置三个固定示例槽位（设备名、输入方式、定时器、PWM 通道）。源码使用编译期静态配置表，不在示例中定义设备对象、Codec 状态、PIN FIFO、帧队列或 TX 脉冲数组。固定回调与配置通过公共 `infrared_device_define.h` 中的 `IR_DEFINE_DEVICE(n, codec_ops)` 宏按 Kconfig 选择展开，设备由 `infrared_example_init()` 使用 `INIT_APP_EXPORT` 注册。

开启 `INFRARED_USING_THREAD_EXAMPLE` 后，应用初始化时注册、打开示例设备并创建**一个**阻塞等待 RT-Thread Event 的接收线程。每个接收设备使用独立事件位；工作线程使用普通 `for` 检查事件掩码中置位的设备，只处理收到通知的实例，不使用编译器内建位扫描。每个设备一轮最多处理 `INFRARED_THREAD_POLL_QUOTA` 个脉冲；达到配额时在工作线程的待处理位集中继续处理，不向自身重复发送 Event。框架核心本身不创建线程。

开启 `INFRARED_USING_MSH` 后，可使用 `ir list`、`ir info N`、`ir open N`、`ir close N`、`ir poll N`、`ir read N`、`ir flush N now|defer`、`ir tx N HEX`、`ir codec list`、`ir codec nec read N` 与 `ir codec nec tx N AA CC [repeats]`。NEC 命令只接受已绑定 NEC 的设备，`AA/CC` 是两位十六进制地址与命令，`repeats` 为 0～8。编解码类型在设备注册时固定，而非 MSH 动态切换；`N` 是固定设备索引；`list/info` 不依赖后端私有成员。

## 集成限制

1. **Input Capture 依赖 BSP**：RT-Thread 3.1.x 的 Input Capture 驱动在 `open()` 时动态创建其内部 ringbuffer，因此该适配器依赖 `RT_USING_HEAP`。`INPUTCAPTURE_CMD_CLEAR_BUF` 与捕获 ISR 是否同步由目标驱动负责；软件包不额外围绕该命令关闭中断，实际同步性仍需目标 BSP 验证。
2. **资源生命周期与失败责任**：调用方必须串行执行同一实例的 `poll()`、接收启停/关闭和发送操作；框架不维护额外的 RX `enabled`、设备 `closing` 或 TX `busy` 状态。OPEN、CLOSE、RX、TX 出错时，框架记录错误并向能够返回 `rt_err_t` 的调用方传播；打开失败只进行一次逆序尽力回滚，关闭时对各方向各尝试一次，不自动重试、不恢复 RT-Thread `ref_count`、不提供额外 `cleanup()` 恢复接口。失败后是否重置设备、修复 BSP 或重启系统，由业务层决定；不可将错误返回视为硬件已停止。中断或物理关闭未确认成功时，相关独占声明和可能被 ISR 访问的静态存储保留，以免其他实例误用；下一次打开可能返回 `-RT_EBUSY`。`infrared_device_write_frames()` / `rt_device_write()` 的返回类型为**已完成帧数**而非负错误码，TX 失败会记录日志，业务层还应核对实际完成数量。关闭前确保没有并发发送/轮询及未完成 ISR。`timeout_ticks` 使用 `rt_int32_t`：`0` 或 `RT_WAITING_FOREVER`（`-1`）为永久等待，正数为有限 Tick 超时；其他负数与超出类型范围的赋值由应用负责，框架不额外校验。物理设备须先由 BSP 注册。
3. **内存预算**：逻辑设备按 `INFRARED_MAX_DEVICES` 保留槽位；已配置的接收设备、PIN 后端和 PWM 后端分别占用紧凑的静态池。手动注册需设置 `INFRARED_MANUAL_PIN_CAPACITY`、`INFRARED_MANUAL_CAPTURE_CAPACITY` 和 `INFRARED_MANUAL_PWM_CAPACITY`。需按目标 MCU RAM 和实际定时器资源验证。
4. **NEC 状态容量**：启用 `INFRARED_FRAMEWORK_NEC_CODEC` 后，`INFRARED_NEC_CONTEXT_COUNT` 决定最多同时持有解析状态的 NEC 接收实例数量；手动注册的 NEC 接收器也占用该容量，纯发送设备无需解析状态。禁用 NEC 时不编译 NEC Codec/MSH 文件。
5. **日志与诊断**：FRAMEWORK 使用 `DBG_TAG`、`DBG_LVL DBG_INFO` 和 `<rtdbg.h>`，不要求 ULOG，也不自行定义 `DBG_ENABLE`。启用 `RT_USING_ULOG` 时日志交由 ULOG 处理；未启用时是否输出取决于目标 RT-Thread 的调试宏及控制台配置。`INFRARED_USING_DIAGNOSTICS` 独立裁剪诊断 API。
6. **接收清空**：`ir flush N now` 立即清空后端原始脉冲、Codec 状态与已完成帧队列；`ir flush N defer` 返回后只登记请求，下一次 `poll()` 开始时清空。`now` 在后端无法可靠清空原始队列时返回错误（Input Capture 需要 BSP 支持 `INPUTCAPTURE_CMD_CLEAR_BUF`）；同实例的 flush、poll、read 和关闭操作必须由调用方串行化。

详细源码与配置说明：[架构](doc/architecture.md) · [接收](doc/reception.md) · [发送及 NEC](doc/transmission-codec.md) · [配置](doc/configuration.md) · [诊断](doc/maintenance.md)。

## 联系方式 & 感谢

- 原维护：RT-Thread 开发团队
- 原项目主页：https://github.com/RT-Thread-packages/infrared_framework
- 许可证：[Apache-2.0](LICENSE)

