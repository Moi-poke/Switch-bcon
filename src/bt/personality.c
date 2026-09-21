// personality.c -- emulated-controller identity table + role helpers.
// Host-testable: stdint/stdbool/string + protocol.h/pack.h only.
// Transport bit layout (both roles, spec/protocol_v3.md section 11):
//   B0 = Y/X/B/A/SR/SL/R/ZR (bits 0-7), B1 = Minus/Plus/RStick/LStick/Home/Capture,
//   B2 = Down/Up/Right/Left/SR/SL/L/ZL (bits 0-7).
#include <string.h>

#include "personality.h"
#include "pack.h" // ctrl_pack_btn3 (stdint-only, host-safe)

const personality_t PERSONALITY_TABLE[3] = {
    {
        // source: src/bt/switch_hid.h:59-76 (VID/PID/CoD/GAP) +
        //         src/bt/hid.c:207-221 (fw 03 8B, dev_type 03) +
        //         src/usb/usb_descriptors.c:21-23,173-178 (USB ids/strings).
        .id = EMUL_ROLE_PROCON,
        .usb_vid = 0x057E,
        .usb_pid = 0x2009,
        .usb_mfr = "Nintendo Co., Ltd.",
        .usb_product = "Pro Controller",
        .usb_serial = "000000000001",
        .gap_name = "Pro Controller",
        .cod = 0x2508u,
        .dev_type = 0x03u,
        .fw_major = 0x03u,
        .fw_minor = 0x8Bu,
    },
    {
        // source: switchnotes console_pairing_session device-info `82 02 03 48 01`,
        //         hid-nintendo.c JOYCON_CTLR_TYPE_JCL=0x01.
        // TBD: USB VID:PID/strings for Joy-Con (L) are unverified placeholders
        // (PID 0x2006 is the documented Joy-Con (L) value; strings mirror ProCon).
        .id = EMUL_ROLE_JOY_L,
        .usb_vid = 0x057E,
        .usb_pid = 0x2006,
        .usb_mfr = "Nintendo Co., Ltd.",
        .usb_product = "Joy-Con (L)",
        .usb_serial = "000000000001",
        .gap_name = "Joy-Con (L)",
        .cod = 0x2508u,
        .dev_type = 0x01u,
        .fw_major = 0x03u,
        .fw_minor = 0x48u,
    },
    {
        // source: switchnotes console_pairing_session device-info `82 02 03 48 02`,
        //         hid-nintendo.c JOYCON_CTLR_TYPE_JCR=0x02.
        // TBD: USB VID:PID/strings for Joy-Con (R) are unverified placeholders
        // (PID 0x2007 is the documented Joy-Con (R) value; strings mirror ProCon).
        .id = EMUL_ROLE_JOY_R,
        .usb_vid = 0x057E,
        .usb_pid = 0x2007,
        .usb_mfr = "Nintendo Co., Ltd.",
        .usb_product = "Joy-Con (R)",
        .usb_serial = "000000000001",
        .gap_name = "Joy-Con (R)",
        .cod = 0x2508u,
        .dev_type = 0x02u,
        .fw_major = 0x03u,
        .fw_minor = 0x48u,
    },
};

const personality_t *personality_get(uint8_t id) {
    if (id > (uint8_t)EMUL_ROLE_JOY_R) {
        return NULL;
    }
    return &PERSONALITY_TABLE[id];
}

void personality_mac(const uint8_t base[6], uint8_t role, uint8_t out[6]) {
    memcpy(out, base, 6);
    out[5] ^= (uint8_t)(role & 0x03u);
}

void joy_pack_btn3(uint32_t procon, uint8_t role, uint8_t out3[3]) {
    if (role == (uint8_t)EMUL_ROLE_JOY_L) {
        uint8_t b0 = 0u, b1 = 0u, b2 = 0u;
        if (procon & BTN_MINUS) {
            b1 |= 0x01u;
        }
        if (procon & BTN_LSTICK) {
            b1 |= 0x08u;
        }
        if (procon & BTN_CAPTURE) {
            b1 |= 0x20u;
        }
        if (procon & BTN_DOWN) {
            b2 |= 0x01u;
        }
        if (procon & BTN_UP) {
            b2 |= 0x02u;
        }
        if (procon & BTN_RIGHT) {
            b2 |= 0x04u;
        }
        if (procon & BTN_LEFT) {
            b2 |= 0x08u;
        }
        if (procon & BTN_ZR) {
            b2 |= 0x10u; // left-SR (consumed: not at ProCon B0.b7)
        }
        if (procon & BTN_R) {
            b2 |= 0x20u; // left-SL (consumed: not at ProCon B0.b6)
        }
        if (procon & BTN_L) {
            b2 |= 0x40u;
        }
        if (procon & BTN_ZL) {
            b2 |= 0x80u;
        }
        // Dropped: A/B/X/Y/Plus/Home/RSTICK (no left-half position).
        out3[0] = b0;
        out3[1] = b1;
        out3[2] = b2;
        return;
    }
    if (role == (uint8_t)EMUL_ROLE_JOY_R) {
        uint8_t b0 = 0u, b1 = 0u;
        if (procon & BTN_Y) {
            b0 |= 0x01u;
        }
        if (procon & BTN_X) {
            b0 |= 0x02u;
        }
        if (procon & BTN_B) {
            b0 |= 0x04u;
        }
        if (procon & BTN_A) {
            b0 |= 0x08u;
        }
        if (procon & BTN_ZL) {
            b0 |= 0x10u; // right-SR (consumed: not at ProCon B2.b7)
        }
        if (procon & BTN_L) {
            b0 |= 0x20u; // right-SL (consumed: not at ProCon B2.b6)
        }
        if (procon & BTN_R) {
            b0 |= 0x40u;
        }
        if (procon & BTN_ZR) {
            b0 |= 0x80u;
        }
        if (procon & BTN_PLUS) {
            b1 |= 0x02u;
        }
        if (procon & BTN_RSTICK) {
            b1 |= 0x04u;
        }
        if (procon & BTN_HOME) {
            b1 |= 0x10u;
        }
        // Dropped: D-pad/Minus/Capture/LSTICK (no right-half position).
        out3[0] = b0;
        out3[1] = b1;
        out3[2] = 0u;
        return;
    }
    {
        ctrl_state_t st;
        st.buttons = procon;
        st.lx = 0u;
        st.ly = 0u;
        st.rx = 0u;
        st.ry = 0u;
        ctrl_pack_btn3(&st, out3);
    }
}

bool joy_use_left_stick(uint8_t role) {
    if (role == (uint8_t)EMUL_ROLE_JOY_L) {
        return true;
    }
    if (role == (uint8_t)EMUL_ROLE_PROCON) {
        return true;
    }
    return false;
}
