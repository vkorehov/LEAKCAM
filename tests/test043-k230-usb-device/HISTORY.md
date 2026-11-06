# test043: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### Automated run 2026-11-05 (run.py, 24 s): pass

3/3 checks pass.


### Automated run 2026-11-05 (run.py, 24 s): pass

3/3 checks pass.


### Automated run 2026-11-05 (run.py, 159 s): **FAIL**

1/3 checks pass. Failed: 200 round trips of 16 bytes, all back (0 ok, 200 bad); 1 MB echoed, every byte checked (0 B, 0 wrong, 0 KB/s).


### Automated run 2026-11-05 (run.py, 32 s): **FAIL**

not run: the K230 USB device did not enumerate.


## Obstacles

- 2026-11-05: the K230 image from 2026-11-01 was never rebuilt after the USB settings (kdimg 1 min newer than the
  defconfig): test_install.sh run again, the image rebuilt.
- 2026-11-05: the VM saw no USB device: VMware held the new 1209:ABD1 on the Mac until the user allowed it.
- 2026-11-05: `usb_echo`: "ttyGS0 not available": it opened ttyGS0 with INT_RX, which the CDC device (DMA RX/TX only)
  refuses in serial.c; opened with the DMA flags since.
- 2026-11-05: the VM's xHCI controller hung twice after a BL616 reboot from its ROM (hub_ext_port_status -32): reset
  by unbinding/binding 0000:0a:00.0.

- no /dev/leakcam-k230usb: the image without the USB device (test_install.sh), or USB0 not up (2026-11-05, automated run)

- 2026-11-01, first build with CherryUSB: the SDK's glue (bsp/maix3/components/CherryUSB/port/dwc2/usb_glue_kendryte.c)
  calls `rt_ioremap` without `<ioremap.h>`: implicit declaration, the 64-bit mapped address returned as an `int`
  (warning only). test_install.sh adds the include before the build; the image rebuilt without the warning.
