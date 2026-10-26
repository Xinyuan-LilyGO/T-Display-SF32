# RT-Thread UART DMA ping-pong test

This example uses the RT-Thread `uart2` device with DMA RX and DMA TX at
921600 baud. Each frame is 8 bytes: `PING` followed by a little-endian 32-bit
sequence number.

Connect PA40 (TX) to the other board's PA39 (RX), cross the other TX/RX pair,
and connect the grounds. Build one board with
`CONFIG_UART_PINGPONG_INITIATOR=y` in `project/proj.conf`. Build the other with:

```text
# CONFIG_UART_PINGPONG_INITIATOR is not set
```

Build from `project` with:

```text
scons --board=t-display-sf32_hcpu -j8
```

UART1 prints transfer statistics every 1000 frames.
