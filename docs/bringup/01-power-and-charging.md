# 1. Power and charging

- **No charging** -> U1 is BQ24072**T** (no internal TS bias), circuit drawn for the plain BQ24072 -> rev 1: 47 k
  VUSB-TS; rev 2: BQ24072RGTR. [bug-001](../../bugs/bug-001/README.md)
- **Wrong NTC window** -> assumed B3950, part is B3435 -> take constants from EXTERNAL_PARTS.md.
