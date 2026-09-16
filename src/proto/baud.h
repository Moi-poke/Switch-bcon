// baud.h -- UART rate table + hunt sweep/lock pure logic (task-16 / B-0).
// Pico/BTstack-independent, host-testable (pack.c pattern).
// Rate table is shared with Sub-project B §3 BAUD_SET (index<->bps).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BAUD_N 5u

// index->bps. 0=115200, 1=460800, 2=921600, 3=1000000(default), 4=2000000.
// Returns 0 for an invalid index.
uint32_t baud_bps(uint8_t idx);

static inline bool baud_idx_valid(uint8_t idx) { return idx < BAUD_N; }

// Sweep order for the Core1 hunt machine. Slot 0 = last_good when it is
// valid and within max_bps (Layer-0 fast path); the rest is the table in
// descending order capped at max_bps (build ceiling), deduplicated.
// Writes indexes into out (must hold BAUD_N). Returns slot count (>=1
// whenever at least idx0 fits max_bps; 0 only if max_bps < 115200).
uint8_t baud_sweep_order(uint8_t last_good, uint32_t max_bps,
                         uint8_t out[BAUD_N]);

// Lock detector: 2 consecutive valid frames confirm a slot (CRC8 1/256
// false-positive guard). Feed true per parser-accepted frame, false on
// any gap (bad CRC/len, dwell expiry, slot change). Returns true exactly
// when the feed completes the lock pair.
typedef struct {
    uint8_t n;
} baud_lock_t;

void baud_lock_reset(baud_lock_t *l);
bool baud_lock_feed(baud_lock_t *l, bool frame_ok);
