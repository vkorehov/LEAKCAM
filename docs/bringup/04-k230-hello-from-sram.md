# 4. K230 hello from SRAM (k230_hello)

- **Loader format** -> `--custom-loader` takes a raw binary at 0x80360000, no header.
- **No output** -> ROM leaves UART/GPIO clocks gated -> enable them (CMU 0x91100024).
- **Slow, garbled lines** -> UART register reads return 0, THRE never seen -> pace characters by time.
- **Flasher "hang"** -> `timeout` around `docker run` leaves the container holding USB -> `timeout` inside the container.
