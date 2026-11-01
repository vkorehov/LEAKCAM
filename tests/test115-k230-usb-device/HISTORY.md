# test115: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

## Obstacles

- 2026-11-01, first build with CherryUSB: the SDK's glue (bsp/maix3/components/CherryUSB/port/dwc2/usb_glue_kendryte.c)
  calls `rt_ioremap` without `<ioremap.h>`: implicit declaration, the 64-bit mapped address returned as an `int`
  (warning only). test_install.sh adds the include before the build; the image rebuilt without the warning.
