# CI、STM32F407 编译矩阵与 GitHub Pages

本仓库属于 RT-Thread 在线软件包，本身只有 `SConscript`，**没有独立 BSP `SConstruct`**。
编译工作流将仓库当前版本临时放入 RT-Thread 的 `bsp/stm32/stm32f407-atk-explorer`
工程，再使用 SCons 实际编译和链接 ARM ELF。这与仅运行 GCC `-fsyntax-only` 不同。

## 自动化验证

| Job | 检查内容 | 失败门禁 |
| --- | --- | --- |
| `config-matrix` | Kconfiglib 解析真实 `Kconfig` 的依赖和互斥项，并核对 `SConscript` 实际选源 | 配置不合法、选源不一致、源码缺失 |
| `stm32f407-build` | RT-Thread v4.1.1 的 10 个组合与 master 的 4 个兼容组合，STM32F407 + SCons、ARM GCC 编译和 ELF 链接 | 编译/链接失败、ELF 缺失、目标架构错误、对象文件遗漏或重复 |
| `cppcheck` | 按 10 个 Kconfig profile 的有效宏和 SConscript 选源分别分析 | Cppcheck 报告所启用类别的错误或告警 |
| `documentation` | Doxygen 生成 C API、告警门禁和 `mkdocs build --strict` | 文档警告、HTML 缺失、站点构建失败 |

所有任务均由 `.github/workflows/ci.yml` 定义。`push`、针对 `master` 的 PR
和手动执行均触发 CI。矩阵任务失败不会通过 `continue-on-error` 被忽略。

### 构建矩阵

| Profile | 编译选项 |
| --- | --- |
| `legacy-minimal` | LEGACY 基础接口、不编译旧驱动 |
| `legacy-nec-rx` | LEGACY 驱动、NEC 解码、PIN/HWTIMER 接收 |
| `legacy-rx-tx` | LEGACY 接收 + PWM/HWTIMER 发送 |
| `framework-core` | FRAMEWORK 核心、无硬件收发后端、NEC 关闭 |
| `framework-pin-nec` | FRAMEWORK PIN 接收、HWTIMER、NEC + pulse-distance |
| `framework-capture` | FRAMEWORK Input Capture、Heap、NEC Codec |
| `framework-pwm` | FRAMEWORK PWM + HWTIMER 发送、NEC Codec、发送超时 |
| `framework-full` | 三槽位、PIN/Capture/PWM、NEC、MSH、示例和 Event 线程 |
| `framework-sparse` | 只启用槽 2、关闭可选参数检查和诊断、验证稀疏实例 |
| `framework-pulse-only` | 不编译 NEC，只编译共享 pulse-distance helper |

`tools/ci/check_matrix.py` 通过 Kconfiglib 解析各组合，产出**有效宏**；
`.github/ci/infrared/build-stm32f407.sh` 将有效宏插入仅用于 CI 的 BSP `rtconfig.h`。
仓库源码暂存于 BSP `ci_infrared/`，通过 BSP `SConscript` 自动发现并加载一次，
不再额外注入 `SConstruct`，避免对象重复链接。
编译后还验证应选中的每一个源码对象在 `build/ci_infrared/` 下
**恰好编译一次**，关闭的源码不应出现。

负向 Kconfig 检查覆盖 Input Capture 缺少 Heap、PIN 缺少 HWTIMER、
PWM 未启用时禁止 PWM 后端等依赖约束。

### 依赖与上游源码

CI 使用 Python 3.11、SCons、`gcc-arm-none-eabi`，分别运行 **RT-Thread `v4.1.1`**
和 **`master`**。旧版 10 个配置覆盖历史设备回调 `rt_size_t`、旧 HWTIMER 与
现有各收发后端。master 使用 `rt_ssize_t` 回调，已移除原 HWTIMER API，
因此构建 `legacy-minimal`、`framework-core`、`framework-capture`、
`framework-pulse-only` 四种不依赖旧 HWTIMER 的配置；它们**不能代表旧 HWTIMER
收发后端在 master 上已获得支持**。
旧版 BSP 的 STM32F4 HAL/CMSIS 位于源码树；master 的依赖须先运行
`scons --pyconfig-silent` 与 RT-Thread Env 的 package upgrade/update 来下载。
每次上传构建日志、有效宏、RT-Thread SHA 与 ARM ELF，以明确验证基线。

RT-Thread 官方软件包索引也包含 `PKG_USING_INFRARED`，为避免源码重复：
CI **不在 BSP `.config` 中启用上游软件包**，只将当前 checkout 下的源码
复制到独立的 `ci_infrared/` 临时目录，由 BSP 原生的 `SConscript` 加载；
不修改真实软件包的 `SConscript`、`Kconfig` 或上游 Git 历史。
如果 BSP 已启用官方同名包，脚本立即失败，而不是混入另一个实现。

### 本地复现

安装 `.github/ci/infrared/requirements-build.txt` 中的 Python 依赖，并配置
可访问 GitHub 的环境及 ARM GCC 工具链。旧版 STM32F4 HAL/CMSIS 由 RT-Thread tag 提供；master 需要网络获取
Env 及 STM32F4 HAL/CMSIS 软件包。
项目根目录执行：

```sh
python3 tools/ci/check_matrix.py --all
sh .github/ci/infrared/build-stm32f407.sh --list-profiles
RTTHREAD_REF=v4.1.1 sh .github/ci/infrared/build-stm32f407.sh framework-full
RTTHREAD_REF=master sh .github/ci/infrared/build-stm32f407.sh framework-core
```

最后一条命令会生成 `_ci/` 中的临时 RT-Thread BSP、日志、有效配置文件与
`rt-thread.elf`，需要完整下载/编译依赖；前三条中的 Kconfig 检查不会编译固件。
运行不同 profile 前请清理本地 `_ci/rt-thread` 和 `_ci/env` 工作目录，
或使用独立工作目录，避免跨配置污染。

### Cppcheck / Doxygen

Cppcheck 对每个 profile 的有效源码和生成宏分别分析，启用 `warning`、`style`、
`performance`、`portability`，将缺少外部 RT-Thread 头文件的报告排除，
结果保存为 `_ci/cppcheck.txt`。这不替代完整编译或目标板验证。

Doxygen 用根目录 `Doxyfile` 为两个模式的 C 源码/API 生成索引和源码浏览，
启用文档警告检查与告警失败门禁（`WARN_AS_ERROR=YES`），并将输出保存于
`_ci/doxygen/html/`。对 Markdown 说明文档使用 MkDocs Material。

## GitHub Pages

同一站点发布两个部分：

```text
https://wdfk-prog.github.io/infrared_framework/
  /            MkDocs 文档入口（手写 Markdown）
  /api/        Doxygen C API 与源码索引
```

`.github/workflows/pages.yml` 在任意分支推送或手动运行时构建并发布网站；
所有分支共享同一个 Pages 站点，后一次成功部署可覆盖之前分支的站点内容。
若 `github-pages` 环境设置了分支部署白名单，还需在仓库 Settings 中同步调整。PR 的文档验证在 CI 内独立进行，**不会发布 Pages**。
站点通过 GitHub 官方 `configure-pages`、`upload-pages-artifact`、
`deploy-pages` 链路发布一个组合 artifact，不建立第二条并发发布流程。

首次发布前，在 GitHub 仓库中设置：

```text
Settings -> Pages -> Build and deployment -> Source -> GitHub Actions
```

随后检查 Pages 工作流的 `build`、`deploy` 均成功及实际访问链接。
`GITHUB_TOKEN` 无需配置额外个人访问令牌。

## 验证边界

ARM ELF 成功链接只能证明所选配置和编译器/BSP 的**构建兼容性**；
不能证明 `timer`/`pwm`/`input capture` 已在实体 STM32F407 板卡上注册，
更不能证明红外收发波形、中断并发、实际资源占用或 HIL 测试通过。
Input Capture 依赖目标 BSP 是否提供对应硬件驱动；CI 能验证框架适配代码，
但硬件采集功能仍需单独上板实测。

相关内容：[配置与移植](configuration.md) · [架构](architecture.md) · [首页](index.md)。
