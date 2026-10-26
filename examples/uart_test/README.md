# UART DMA ping-pong test

UART2 uses 921600 baud, 8 data bits, no parity and 1 stop bit. Each DMA frame is
8 bytes: the ASCII marker `PING` followed by a little-endian 32-bit sequence.

Connect the two boards as follows:

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
responder echoes each received frame. Both sides arm RX DMA before transmitting
the next frame, and print transfer statistics every 1000 frames.
