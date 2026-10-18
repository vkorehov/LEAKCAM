# 1. Power and charging

- **No charging** -> U1 is BQ24072**T** (no internal TS bias), circuit drawn for the plain BQ24072 -> rev 1: 47 k
  VUSB-TS; rev 2: BQ24072RGTR. [bug-001](../../bugs/bug-001/README.md)
- **Wrong NTC window** -> assumed B3950, part is B3435 -> take constants from EXTERNAL_PARTS.md.
- **Cell drained to 2.9 V after a day of bring-up** -> no charging (bug-001) and hours without USB power (bug-002
  before it was understood) -> K230 rails off; charge the cell off the board (4.2 V, 100 mA below 3.0 V) or fit
  the 47 k. No K230 flashing at 2.9 V (a brown-out during a NAND write). D1 went off: the charger's timer or
  pre-charge state, still bug-001.
