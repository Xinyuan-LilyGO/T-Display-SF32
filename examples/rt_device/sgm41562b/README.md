# SGM41562B RT-Thread Device Example

This example uses the registered RT-Thread device `sgm41562b` to initialize
and configure the board charger. It does not call the private charger driver
API directly.

The application applies these settings through `rt_device_control()`:

- input voltage limit: 4600 mV
- input current limit: 500 mA
- battery charge voltage: 4200 mV
- battery charge current: 200 mA
- HIZ mode disabled
- charging enabled

It then reads the charge state, input power state, system status register, and
fault register with `rt_device_read()` every five seconds.

These values assume a single-cell 4.2 V lithium-ion battery. Confirm the cell's
maximum voltage, allowed charge current, NTC circuit, and thermal design before
using different settings.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

Expected output resembles:

```text
[I/sgm41562b.app] charger 0x00 configured: VIN=4600 mV, IIN=500 mA, VBAT=4200 mV, ICHG=200 mA
Charger: fast charge, power=good, status=0x12, fault=0x00
```
