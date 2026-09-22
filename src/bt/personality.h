// personality.h -- ProCon / Joy-Con (L) / Joy-Con (R) identity table + role helpers.
// Pico/BTstack/TinyUSB-free (stdint/stdbool + protocol.h only): host-testable.
#ifndef SWITCH_BCON_PERSONALITY_H
#define SWITCH_BCON_PERSONALITY_H

#include <stdint.h>
#include <stdbool.h>

#include "protocol.h" // EMUL_ROLE_* (stdint-only, host-safe)

typedef struct {
    uint8_t id; // EMUL_ROLE_* index into PERSONALITY_TABLE
    uint16_t usb_vid;
    uint16_t usb_pid;
    const char *usb_mfr;
    const char *usb_product;
    const char *usb_serial;
    const char *gap_name;
    uint32_t cod;
    uint8_t dev_type; // subcmd 0x02 device-info controller type
    uint8_t fw_major;
    uint8_t fw_minor;
} personality_t;

extern const personality_t PERSONALITY_TABLE[3];

// id > 2 -> NULL.
const personality_t *personality_get(uint8_t id);

// Role-tagged MAC: copy base, out[5] ^= (role & 0x03).
void personality_mac(const uint8_t base[6], uint8_t role, uint8_t out[6]);

// ProCon u32 (VIIPER order) -> 3B transport buttons for the given role.
// role == EMUL_ROLE_PROCON delegates to ctrl_pack_btn3; Joy roles filter
// to their physical buttons and remap the consumed shoulder pair to SL/SR.
void joy_pack_btn3(uint32_t procon, uint8_t role, uint8_t out3[3]);

// Which stick the role reports: JoyL -> left (true), JoyR -> right (false),
// ProCon -> left stick present (true).
bool joy_use_left_stick(uint8_t role);

#endif
