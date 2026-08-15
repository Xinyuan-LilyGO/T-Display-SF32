# AW86224 RT-Thread Device Example

This example uses the AW86224 adapter registered as the RT-Thread character
device `aw86224`.

The application finds and opens the registered device, sets its gain with
`rt_device_control()`, plays RAM effects 1 through 4, and reads playback status
with `rt_device_read()`.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```
