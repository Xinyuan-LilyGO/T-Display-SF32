# BHI260AP RT-Thread Sensor Example

This example uses the RT-Thread Sensor framework to initialize the BHI260AP,
read IMU data, and report the hardware step counter.

The BHI260AP adapter registers these read-only polling devices:

- `acce_bhi260`: 3-axis acceleration in mg
- `gyro_bhi260`: 3-axis angular velocity in mdps
- `step_bhi260`: hardware step count

Opening the first device uploads the BHI260AP firmware, configures the virtual
sensors, and starts the interrupt-driven FIFO worker. The application opens all
three devices at 100 Hz and prints a sample every 500 ms.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

Expected output resembles:

```text
[I/bhi260.app] BHI260AP sensor devices opened at 100 Hz
Accel[mg] x=12 y=-18 z=1003 | Gyro[mdps] x=125 y=-62 z=31 | Steps=42
```
