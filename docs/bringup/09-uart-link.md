# 9. K230 <-> BL616 UART link

- **Test method** -> K230 -> BL616: BL616 `pt_uart on` prints what arrives, the agent's `READY` frames at boot;
  BL616 -> K230: `pt_uart on` sends a line per second, K230 `cat /dev/uart1`. Both pass, no loss.
- **Each READY 5 times** -> by design: the agent resends while no ACK comes (test firmware sends none).
- **`cat /dev/uart1` hangs the shell (no Ctrl-C)** -> reset the K230 from the BL616 (`pt_hold`, `pt_run`).
- **`killall leakcam_agent` did not stop it** -> the agent was still listed afterwards; it did not get in the way.
