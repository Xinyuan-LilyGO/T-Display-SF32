# BME280 RT-Thread Sensor Example

This example powers the BME280 on the T-Display-SF32 keyboard board and uses
the RT-Thread Sensor framework instead of calling the BME280 driver directly.

The BME280 adapter registers these read-only polling devices:

- `temp_bme280`: temperature in 0.1 degrees Celsius
- `humi_bme280`: relative humidity in 0.1 percent RH
- `baro_bme280`: pressure in pascals

The application opens all three devices with `rt_device_open()`, reads them
with `rt_device_read()`, and prints one sample every three seconds.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

Expected output resembles:

```text
[I/bme280.app] BME280 sensor devices opened, chip ID: 0x60
Temperature: 25.3 C, Humidity: 48.7 %RH, Pressure: 100825 Pa (1008.25 hPa)
```
