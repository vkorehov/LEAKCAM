/* entry: the boot ROM jumps here (0x80360000) on one hart; stack at the top of the SRAM the loader may use */
    .section .text.start
    .globl _start
_start:
    csrw    mie, zero
    li      sp, 0x803fff00
    call    main
1:  wfi
    j       1b
