# RT-Thread Device Test Examples

[中文](readme_cn.md)

This directory contains tests for peripherals registered with the RT-Thread
Device, Sensor, and ADC frameworks on T-Display-SF32. The examples generally
access devices through `rt_device_find()`, `rt_device_open()`,
`rt_device_read()`, `rt_device_write()`, and `rt_device_control()`. They are
intended for driver verification rather than complete product applications.

## Examples

| Directory | Function |
| --- | --- |
| `bhi260ap` | Reads acceleration, gyroscope, and step-counter data through RT-Sensor. |
| `joypad` | Reads joystick X/Y voltages through the registered ADS1115 ADC Device, with polling and optional ALERT/RDY interrupt modes. |
| `joypad_key` | Currently tests XL9555 inputs and interrupts. Despite the directory name, it does not read the ADS1115 joystick. |
| `keyboard_bme280` | Reads temperature, humidity, and pressure through RT-Sensor. |
| `keyboard_key` | Detects keys through the registered TCA8418 Device. Pressing `ENTER` toggles the AW21009 keyboard LEDs. |
| `keyboard_l76k` | Reads L76K positioning data through RT-Sensor and uses the shared UART2 through `uart_mux`. |
| `keyboard_motor` | Sets gain, plays vibration waveforms, and reads status through the registered AW86224 Device. |
| `sgm41562b` | Configures charging parameters and reads status and fault information through the registered charger Device. |
| `xl9555` | Tests IO direction, read/write operations, and input interrupts through the registered XL9555 Device. |

The ESP32C6 test targets the RT-Thread UART Device, while the AW21009 LED uses
its existing component API. The other peripherals identified above are
accessed through their corresponding RT-Thread device adapters.

## Build

Enter the target example's `project` directory and run:

```text
scons --board=t-display-sf32_hcpu -j8
```

## Notes

- Enable the corresponding device adapter in `proj.conf` or `menuconfig`.
  After startup, use `list_device` to verify the registered device name.
  Rebuild the firmware after changing the configuration or SDK drivers.
- The `keyboard_*` examples require the T-SF32 keyboard board and depend on
  power, enable, or reset signals controlled by XL9555.
- L76K and ESP32C6 share UART2. Switch the route through `uart_mux` and do not
  change devices during a complete command-response transaction.
- Some XL9555 IOs control GPS, Wi-Fi, keyboard LEDs, and the motor. Before
  running `xl9555` or `joypad_key`, ensure the test mask will not change an
  active control pin into an input.
- Several peripherals share I2C1. TCA8418 and ADS1115 interrupt mode may also
  share PIN11, so the related examples must not run at the same time.
- The `sgm41562b` settings are intended only for a single-cell 4.2 V lithium
  battery. Check the cell specification and thermal limits before changing
  charge voltage or current.
- SF32LB52X requires a startup voltage above 3.5 V. Keep the battery voltage
  above 3.5 V or the board may fail to boot.
