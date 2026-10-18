int spinand_isbusy(struct aic_spinand *flash, uint8_t *status)
{
    /* LEAKCAM: wait on real time and accept "ready" only when two status reads in a row agree.
     * Canaan's version polled 1000 times, then re-read the status once and failed when that read
     * still showed BUSY. On the W25N02KV (rev 1, 2026-10-18) a status read sometimes shows "ready"
     * right after the page-read command, the confirm read then sees BUSY (0x01), and the page read
     * was abandoned without data: UFFS took the -1 as "ECC corrected", used stale buffers, refreshed
     * blocks, failed its write verifies and marked good blocks bad. 20 ms covers a block erase
     * (10 ms max). */
    const uint64_t t0 = cpu_ticks();
    const uint64_t limit = (uint64_t)TIMER_CLK_FREQ / 50;
    uint8_t SR = 0xFF;
    int result;

    while (1) {
        result = spinand_read_status(flash, &SR);
        if (result != SPINAND_SUCCESS)
            return result;
        if (!(SR & 0x1)) {
            result = spinand_read_status(flash, &SR);
            if (result != SPINAND_SUCCESS)
                return result;
            if (!(SR & 0x1))
                break;
        }
        if (cpu_ticks() - t0 > limit) {
            LOG_W("spinand timeout.\n");
            break;
        }
        cpu_ticks_delay_us(1);
    }

    if (status)
        *status = SR;

    return SR & 0x1 ? -SPINAND_ERR_TIMEOUT : 0;
}
