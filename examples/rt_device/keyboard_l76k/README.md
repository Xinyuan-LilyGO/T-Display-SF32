# L76K RT-Thread Sensor Example

This example uses the L76K adapter registered with the RT-Thread Sensor
framework. The adapter registers the read-only polling device `gps_l76k`.

The application initializes the UART multiplexer on `uart2`, opens the sensor
with `rt_device_open()`, configures polling and normal power modes, and reads
GPS position data with `rt_device_read()`. Time, satellite count, and other
extended fields are read from the L76K driver because the RT-Thread Sensor
framework does not forward vendor-specific control commands.

L76K and ESP32C6 share UART2 on the keyboard board. The application selects
`UART_MUX_DEVICE_GPS` before each read. An application that uses both modules
must serialize complete transactions and keep the mux on the selected module
until its response has been received.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

The log reports that it is waiting for a fix, then prints UTC time, position,
altitude, and satellite count after a valid fix is available.
