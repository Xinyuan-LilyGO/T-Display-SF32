# Joypad example

本示例通过 ADS1115 采集摇杆 X/Y 轴，通过 XL9555 读取按键，并使用 4 颗
WS2812 RGB LED 显示摇杆方向和幅度。

## RGB 状态

- 摇杆位于中心死区：4 颗 LED 低亮蓝色。
- 向左：LED0 显示青色。
- 向上：LED1 显示绿色。
- 向右：LED2 显示紫色。
- 向下：LED3 显示橙色。
- 斜向推动：同时点亮两个对应方向的 LED。
- LED 亮度随摇杆偏移幅度增加。
- ADS1115 初始化失败：4 颗 LED 显示红色。

WS2812 使用 PA36 的 PWM3 CH1 DMA 输出，默认全局亮度为 96/255。ADC 中心电压
按 1.65 V 处理，死区为 150 mV，可在 `src/main.c` 中调整。

## 编译和烧录

在 `project` 目录执行：

```text
scons --board=t-display-sf32_hcpu -j8
build_t-display-sf32_hcpu\uart_download.bat
```
