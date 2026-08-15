# Keyboard Key and LED Example

This example detects keyboard events from the TCA8418 device registered as
`tca8418` and controls the six keyboard LEDs through the AW21009 component.

Every detected key is printed with its name and numeric code. Press `ENTER` to
toggle all keyboard LEDs between off and 50 percent brightness.

The application uses blocking `rt_device_read()` calls, so it does not poll the
keyboard while idle. Pending key events are flushed once during startup.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

Expected output resembles:

```text
[I/keyboard.app] keyboard 'tca8418' ready; press ENTER to toggle LEDs
[I/keyboard.app] key=1, code=32, long=0
[I/keyboard.app] key=ENTER, code=31, long=0
[I/keyboard.app] keyboard LEDs ON
```
