# infrared_framework

面向 RT-Thread 的红外软件包，提供两套**互斥**的编译实现：

- **LEGACY**：保留原有单设备红外驱动和 NEC 解码接口。
- **FRAMEWORK**：多实例设备框架，按 Kconfig 选择 PIN/HWTIMER 接收、Input Capture 接收、PWM/HWTIMER 发送、NEC Codec、诊断命令和事件线程示例。

选择实现模式以及准备 BSP 时，请从 [配置与移植](configuration.md) 开始。

## 文档索引

- [架构与实现边界](architecture.md)：设备注册、资源所有权、收发状态
- [红外接收](reception.md)：原始脉冲、后端和帧缓冲
- [发送与 Codec](transmission-codec.md)：PWM、NEC 和扩展协议
- [配置与移植](configuration.md)：Kconfig、RAM 容量和 BSP 依赖
- [诊断与维护](maintenance.md)：MSH、错误传播及诊断
- [CI 与 GitHub Pages](ci.md)：编译矩阵、静态检查和部署方法
- [Doxygen API 参考](https://wdfk-prog.github.io/infrared_framework/api/)：公共接口、数据结构与源码浏览

代码和完整快速入门位于 [GitHub 仓库](https://github.com/wdfk-prog/infrared_framework)。

## 维护与感谢

- 当前维护者：[wdfk-prog](https://github.com/wdfk-prog)
- 个人主页：[wdfk-prog.space](https://wdfk-prog.space/)
- 联系邮箱：[1425075683@qq.com](mailto:1425075683@qq.com)
- 原维护团队：RT-Thread 开发团队
- 原项目：[RT-Thread-packages/infrared_framework](https://github.com/RT-Thread-packages/infrared_framework)
- 开源许可：[Apache-2.0](https://github.com/wdfk-prog/infrared_framework/blob/master/LICENSE)

感谢 RT-Thread 开发团队和原项目贡献者。
