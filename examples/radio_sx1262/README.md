# SX1262 LoRa sending example

This example uses the C RadioLib port in the SiFli SDK to drive the SX1262 on
T-Display-SF32. After initialization, it sends a packet every three seconds:

```text
T-Display-SF32 LoRa #0
T-Display-SF32 LoRa #1
...
```

Default radio configuration:

- SPI bus: `spi1`
- Enable/NSS/DIO1/BUSY/RESET: PA26/PA29/PA30/PA31/PA32
- Frequency: 868 MHz
- Bandwidth: 125 kHz
- Spreading factor: 9
- Coding rate: 4/7
- Output power: 22 dBm (SX1262 maximum)
- TCXO: 3.0 V

Build from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

The serial log prints each transmitted payload or the RadioLib error code.
Change `CONFIG_RADIOLIB_DEFAULT_FREQUENCY` in `project/proj.conf` when using a
module intended for another legal regional band.
