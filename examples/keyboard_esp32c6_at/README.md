# ESP32C6 AT Firmware Test

This example tests the ESP32C6 AT firmware directly through UART2 and the
shared `uart_mux` component.

The application opens the UART receive path before resetting ESP32C6 so boot
messages are visible. It then repeatedly sends:

- `AT` to verify that the firmware responds with `OK`
- `AT+GMR` to print the AT firmware version

Received lines are printed with the `[ESP32C6 RX]` prefix. The test does not
use the asynchronous AT command queue, so a missing response does not prevent
the next test cycle from running.

L76K and ESP32C6 share UART2 on the keyboard board. This example always selects
`UART_MUX_DEVICE_ESP32C6` before transmitting.

## Build

Run from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```
