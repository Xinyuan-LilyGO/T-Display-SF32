# RT-Thread UART DMA ping-pong test

This example uses the RT-Thread `uart2` device with DMA RX and DMA TX at
921600 baud. The application does not access the SDK UART or DMA handles.

Each frame is 8 bytes: the ASCII marker `PING` followed by a little-endian
32-bit sequence number. The receiver searches for the marker in the DMA byte
stream, so it can recover frame alignment after either board restarts.

Connect the boards as follows:

- Board A PA40 (UART2 TX) to board B PA39 (UART2 RX)
- Board A PA39 (UART2 RX) to board B PA40 (UART2 TX)
- Board A GND to board B GND

Build board A as the initiator with this line in `project/proj.conf`:

```text
CONFIG_UART_PINGPONG_INITIATOR=y
```

Build board B as the responder by replacing it with:

```text
# CONFIG_UART_PINGPONG_INITIATOR is not set
```

Build each role from the `project` directory:

```text
scons --board=t-display-sf32_hcpu -j8
```

The initiator sends the first frame and retries after a one-second timeout. The
responder returns every valid frame. UART1 prints statistics every 1000 frames.
