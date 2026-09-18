// rumble.h -- Switch HID-output 0x10 rumble decoder (T3, pure/host-testable).
// SSOT vectors: log/COM3_2026_09_18.rumble_vib.txt (full-tail distinct set).
// Layout (spike plan-a-task-2-report S3/S4, dekuNukem + hid-nintendo):
//   BTstack delivers the report WITHOUT the report ID, so for 0x10:
//   report[0] = packet counter, rumble8 = report[1..8] (report_size >= 9).
// Per-motor block M[0..3]: M[0]=HF-freq-lo, M[1]:bit0=HF-freq-hi,
//   bits7..1=HF amplitude, M[2]=LF-freq, M[3]=LF amplitude.
// HF amp (spike S5, neutral-verified): idx=(M[1]&0xFE)>>1;
//   amp=(idx*255+50)/100, clamp 255. M[1]=0x01 (neutral) -> 0.
// LF amp (neutral-relative, normalized to the loudest OBSERVED byte):
//   M[3]<=0x40 (logged neutral) -> 0;
//   else ((M[3]-0x40)*255+41)/82, clamp 255, where 0x92 is the loudest
//   logged LF byte (both-strong vector, 3 hits) so span=0x92-0x40=82.
// Per-motor amp = max(HF, LF): the logged peak (M[1]=0x00, M[3]=0x92)
// is LF-driven, HF-only decode would report it as silence.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint8_t rumble_motor_amp(const uint8_t m[4]) {
    uint8_t idx, hf, lf;
    if (m == NULL) {
        return 0u;
    }
    idx = (uint8_t)((m[1] & 0xFEu) >> 1);
    hf = (uint8_t)(((uint16_t)idx * 255u + 50u) / 100u);
    if (hf > 255u) {
        hf = 255u;
    }
    if (m[3] <= 0x40u) {
        lf = 0u;
    } else {
        uint16_t v = (uint16_t)(((uint16_t)(m[3] - 0x40u) * 255u + 41u) / 82u);
        lf = v > 255u ? 255u : (uint8_t)v;
    }
    return hf > lf ? hf : lf;
}

// Decodes 8 rumble bytes (report[1..8]) to L/R amps 0-255. NULL -> (0,0).
static inline void rumble_decode8(const uint8_t raw8[8], uint8_t *amp_l,
                                  uint8_t *amp_r) {
    uint8_t l = 0u, r = 0u;
    if (raw8 != NULL) {
        l = rumble_motor_amp(&raw8[0]);
        r = rumble_motor_amp(&raw8[4]);
    }
    if (amp_l != NULL) {
        *amp_l = l;
    }
    if (amp_r != NULL) {
        *amp_r = r;
    }
}

// 0x10-entry (spike S4 correction): report[0] is the packet counter, so the
// payload starts at &report[1] and report_size >= 9 is required. Returns
// false (amps forced to 0) on NULL/short input.
static inline bool rumble_decode_010(const uint8_t *report, int report_size,
                                     uint8_t *amp_l, uint8_t *amp_r) {
    if (report == NULL || report_size < 9) {
        if (amp_l != NULL) {
            *amp_l = 0u;
        }
        if (amp_r != NULL) {
            *amp_r = 0u;
        }
        return false;
    }
    rumble_decode8(&report[1], amp_l, amp_r);
    return true;
}
