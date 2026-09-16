// baud.c -- UART rate table + hunt sweep/lock (task-16 / B-0).
// See baud.h. Pure logic only; the Core1 sweep mechanics live in main.c.
#include "baud.h"

static const uint32_t BAUD_TABLE[BAUD_N] = {
    115200u, 460800u, 921600u, 1000000u, 2000000u,
};

uint32_t baud_bps(uint8_t idx)
{
    if (idx >= BAUD_N) {
        return 0u;
    }
    return BAUD_TABLE[idx];
}

uint8_t baud_sweep_order(uint8_t last_good, uint32_t max_bps,
                         uint8_t out[BAUD_N])
{
    uint8_t n = 0u;
    int k;
    if (baud_idx_valid(last_good) && BAUD_TABLE[last_good] <= max_bps) {
        out[n++] = last_good;
    }
    for (k = (int)BAUD_N - 1; k >= 0; k--) {
        uint8_t idx = (uint8_t)k;
        if (BAUD_TABLE[idx] > max_bps) {
            continue;
        }
        if (n > 0u && idx == out[0]) {
            continue; /* slot 0 already holds last_good */
        }
        out[n++] = idx;
    }
    return n;
}

void baud_lock_reset(baud_lock_t *l)
{
    l->n = 0u;
}

bool baud_lock_feed(baud_lock_t *l, bool frame_ok)
{
    if (!frame_ok) {
        l->n = 0u;
        return false;
    }
    if (l->n < 2u) {
        l->n++;
    }
    return l->n >= 2u;
}
