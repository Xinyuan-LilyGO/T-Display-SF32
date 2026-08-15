# XL9555 RT-Thread Device 示例

该示例通过 XL9555 RT-Thread Device Adapter 演示：

- 配置并翻转一个输出 IO。
- 读取单个 IO 和全部 16 位 IO 状态。
- 把指定 IO 配置为输入并接收 XL9555 INT 中断。
- 使用 `xl9555_dump` FinSH 命令读取当前 IO 状态。

## 默认引脚

- 输出：P1_7，即 Adapter pin 15。
- 中断输入：P0_0 到 P0_7，即 Adapter pin 0-7，输入掩码 `0x00FF`。
- XL9555 INT 接到 MCU RT-Thread PIN 10，低电平有效。

示例将 P0_0 到 P0_7 全部改为输入，因此运行时这些引脚不再作为 GPS、
Wi-Fi、键盘 LED、马达复位等板级控制输出使用。实际测试前请确认这些功能
没有同时启用；也可以在 `menuconfig -> XL9555 example configuration` 中修改
输入掩码。

XL9555 没有可编程内部上拉。输入测试需要硬件外接上拉或下拉，输入电平变化
才会触发 INT。

## RT-Thread Device 接口

```c
rt_device_t dev = rt_device_find("xl9555");
rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);

/* position 参数就是 XL9555 pin 0-15。 */
rt_device_write(dev, 15, &output_value, 1);
rt_device_read(dev, 1, &input_value, 1); /* 读取 P0_1 */
```

方向配置、中断绑定和读取全部 IO 使用 `xl9555_rt_device.h` 中的 control
命令。

## 编译

```powershell
cd examples\rt_device\xl9555\project
scons --board=t-display-sf32_hcpu -j8
```
