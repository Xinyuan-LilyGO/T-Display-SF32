# Joypad RT-Thread Device 示例

该示例使用注册名为 `ads1115` 的 RT-Thread ADC Device 读取摇杆电压：

- X 轴：ADS1115 AIN0，对应 RT-Thread ADC 通道 0
- Y 轴：ADS1115 AIN1，对应 RT-Thread ADC 通道 1
- 输出单位：mV
- ADS1115 ALERT/RDY：RT-Thread PIN 11

## 轮询模式（默认）

轮询模式通过 `rt_device_find()`、`rt_adc_enable()` 和
`RT_ADC_CMD_READ` 访问 ADS1115。SiFli ADC Device 返回值的单位为
0.1 mV，示例除以 10 后打印 mV。

也可以在 FinSH 中验证设备：

```text
adc probe ads1115
adc enable 0
adc read 0
adc enable 1
adc read 1
```

## 中断模式

在 `menuconfig` 的 `Joypad configuration` 中启用
`Read ADS1115 through ALERT/RDY interrupt`，或在 `proj.conf` 中加入：

```text
CONFIG_JOYPAD_USING_ADS1115_IRQ=y
```

RT-Thread 通用 ADC Device 接口只描述单次通道读取，不能表达 ADS1115
连续转换、ALERT/RDY 和运行时 MUX 切换。因此中断模式使用 RT-Thread PIN
中断与信号量，并调用 ADS1115 类型化接口读取转换结果：

1. 从 AIN0 开始连续转换。
2. ALERT/RDY 下降沿释放信号量，线程读取 X。
3. 切到 AIN1，丢弃切换后的第一次转换，再读取 Y。
4. 打印完整 X/Y 数据后切回 AIN0。

中断模式运行期间不要再调用 `rt_adc_read()` 或 `RT_ADC_CMD_READ`，否则
ADC Device 驱动会把 ADS1115 改回单次转换模式。

## 编译

```powershell
cd examples\joypad_rtdevice\project
scons --board=t-display-sf32_hcpu -j8
```
