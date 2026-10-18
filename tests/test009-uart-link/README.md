# test009: K230 <-> BL616 UART link, 2026-10-18

K230 UART1 (IO40 TX, IO41 RX, `/dev/uart1`) to BL616 UART0 (IO22 RX, IO21 TX), 115200 8N1.

| Direction | How | Result | |
|---|---|---|---|
| K230 -> BL616 | BL616 `pt_uart on`, K230 reset: the agent sends `READY` | `$1,READY*FC` .. `$13,READY*0F`, checksums intact; each frame 5x (resent while no ACK comes) | pass |
| BL616 -> K230 | BL616 sends a line per second; K230 `cat /dev/uart1` | `LEAKCAM BL616 uart test 27` .. `40`, consecutive, none lost | pass |

**Pass.**

## Obstacles

- **Each READY 5 times** -> by design: the agent resends while no ACK comes (the test firmware sends none).
- **`cat /dev/uart1` hangs the shell (no Ctrl-C)** -> reset the K230 from the BL616 (`pt_hold`, `pt_run`).
- **`killall leakcam_agent` did not stop it** -> still listed afterwards; it did not get in the way.
