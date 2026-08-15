# RT-Thread Device 测试例程

[English](readme.md)

本目录用于验证 T-Display-SF32 外设注册到 RT-Thread Device、Sensor、ADC
框架后的初始化和访问流程。例程通常通过 `rt_device_find()`、
`rt_device_open()`、`rt_device_read()`、`rt_device_write()` 和
`rt_device_control()` 操作设备，适合驱动联调，不是完整产品应用。

## 例程说明

| 目录 | 功能 |
| --- | --- |
| `bhi260ap` | 通过 RT-Sensor 读取加速度、陀螺仪和计步数据。 |
| `joypad` | 通过注册的 ADS1115 ADC Device 读取摇杆 X/Y 电压，支持轮询和可选 ALERT/RDY 中断模式。 |
| `joypad_key` | 当前用于 XL9555 输入及中断测试；目录名保留，但不是 ADS1115 摇杆读取例程。 |
| `keyboard_bme280` | 通过 RT-Sensor 读取温度、湿度和气压。 |
| `keyboard_key` | 通过注册的 TCA8418 Device 检测按键；按 `ENTER` 开关 AW21009 键盘 LED。 |
| `keyboard_l76k` | 通过 RT-Sensor 读取 L76K 定位数据，并通过 `uart_mux` 使用共享 UART2。 |
| `keyboard_motor` | 通过注册的 AW86224 Device 设置增益、播放振动波形并读取状态。 |
| `sgm41562b` | 通过注册的充电管理 Device 配置充电参数并读取状态、故障信息。 |
| `xl9555` | 通过注册的 XL9555 Device 测试 IO 方向、读写和输入中断。 |

ESP32C6 的测试对象是 RT-Thread UART Device；AW21009 LED 使用现有组件接口，
其余表中标明的外设均通过对应 RT-Thread 设备适配器访问。

## 编译

进入目标例程的 `project` 目录执行：

```text
scons --board=t-display-sf32_hcpu -j8
```

## 注意事项

- 先在 `proj.conf` 或 `menuconfig` 中启用对应设备适配器；启动后可用
  `list_device` 检查注册名称。配置或 SDK 驱动变更后应重新编译固件。
- `keyboard_*` 例程仅适用于 T-SF32 keyboard board，并依赖 XL9555 控制的
  电源、使能或复位信号。
- L76K 与 ESP32C6 复用 UART2，必须通过 `uart_mux` 切换，并保证一次完整
  命令与响应期间不切换到另一设备。
- XL9555 的部分 IO 用于 GPS、Wi-Fi、键盘 LED 和马达控制。运行 `xl9555`
  或 `joypad_key` 前必须确认测试掩码不会把正在使用的控制脚改为输入。
- 多个外设共用 I2C1；TCA8418 和 ADS1115 中断模式还可能共用 PIN11，相关
  例程不要同时运行。
- `sgm41562b` 示例参数仅适用于单节 4.2 V 锂电池，修改充电电压、电流前
  必须核对电芯规格和散热条件。
- SF32LB52X 芯片的启动电压要大于 3.5 V，所以请确保电池电压在 3.5 V
  以上，否则无法正常启动。
