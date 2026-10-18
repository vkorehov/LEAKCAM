# 8. LED chains (white U14/J1, IR U15/J2)

- **White never lit; J1 + at 37 V** -> open chain; LEDs came on while an ohmmeter bridged J1 pin 2 to GND ->
  J1 pin 2 (white -) not joined to its pad / the 2 mm bottom track to R66 -> resoldered. Sheets:
  `tools/power-bringup/led_drivers.png`, `r66_white_return.png`.
- **The "wrong" chain lit** -> PWM0..2 share one counter; a channel's output is high while the counter is at or above
  its compare register, 0 after reset: enabling one channel ran the never-configured other one at full current ->
  shell: `pwm_disable pwm <other>` first; firmware `led_set()` disables the off channels before enabling.
- **Safe test currents:** white 10/50 % = ~7/33 mA (rated 350 mA), IR 10/50 % = ~13/67 mA (rated 1 A).
