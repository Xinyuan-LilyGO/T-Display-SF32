# AW86224 Joypad Motor Example

This RT-Thread example initializes the AW86224 haptic driver on the joypad:

- Initialize XL9555 on `i2c1`.
- Leave the optional AW86224 enable pin disconnected.
- Reset AW86224 through XL9555 P13 (linear pin index 11).
- Verify the AW86224 chip ID and load the built-in RAM waveform library.

## Build

Run from the `project` directory after loading the SiFli SDK environment:

```text
scons --board=t-display-sf32_hcpu -j8
```

Expected log output includes:

```text
AW86224 initialized successfully
motor device 'aw86224' opened
```
