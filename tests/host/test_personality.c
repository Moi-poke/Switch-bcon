// test_personality.c -- personality table / MAC / joy_pack host tests.
// No Pico SDK needed. CTest name: personality.
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "pack.h"
#include "personality.h"
#include "spi.h"
#include "usb_hid.h" // usb_role_pack_btn3 (T9 cross-check export)
/* Link spi.c directly (no CMakeLists change): this test target does not
 * otherwise compile src/proto/spi.c. */
#include "spi.c"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    printf("[0] ProCon row verbatim (switch_hid.h + hid.c + usb_descriptors.c)\n");
    {
        const personality_t *p = personality_get(EMUL_ROLE_PROCON);
        CHECK(p != NULL && p->id == 0, "get(0) valid, id==0");
        CHECK(p->usb_vid == 0x057E && p->usb_pid == 0x2009, "VID 057E PID 2009");
        CHECK(strcmp(p->usb_mfr, "Nintendo Co., Ltd.") == 0, "mfr exact");
        CHECK(strcmp(p->usb_product, "Pro Controller") == 0, "product exact");
        CHECK(strcmp(p->usb_serial, "000000000001") == 0, "serial exact");
        CHECK(strcmp(p->gap_name, "Pro Controller") == 0, "gap exact");
        CHECK(p->cod == 0x2508u, "CoD 0x2508");
        CHECK(p->dev_type == 0x03u, "dev_type 0x03");
        CHECK(p->fw_major == 0x03u && p->fw_minor == 0x8Bu, "fw 03/8B");
    }

    printf("[1] Joy rows (PID/gap/dev_type/fw) + get bounds\n");
    {
        const personality_t *l = personality_get(EMUL_ROLE_JOY_L);
        const personality_t *r = personality_get(EMUL_ROLE_JOY_R);
        CHECK(l != NULL && l->id == 1, "get(1) valid, id==1");
        CHECK(r != NULL && r->id == 2, "get(2) valid, id==2");
        CHECK(l->usb_vid == 0x057E && l->usb_pid == 0x2006, "JoyL PID 2006");
        CHECK(r->usb_vid == 0x057E && r->usb_pid == 0x2007, "JoyR PID 2007");
        CHECK(strcmp(l->gap_name, "Joy-Con (L)") == 0, "JoyL gap");
        CHECK(strcmp(r->gap_name, "Joy-Con (R)") == 0, "JoyR gap");
        CHECK(l->dev_type == 0x01u && r->dev_type == 0x02u, "dev_type 01/02");
        CHECK(l->fw_major == 0x03u && l->fw_minor == 0x48u, "JoyL fw 03/48");
        CHECK(r->fw_major == 0x03u && r->fw_minor == 0x48u, "JoyR fw 03/48");
        CHECK(l->cod == 0x2508u && r->cod == 0x2508u, "CoD 0x2508 both");
        CHECK(personality_get(3) == NULL, "get(3)==NULL");
        CHECK(personality_get(0xFF) == NULL, "get(0xFF)==NULL");
    }

    printf("[2] MAC: role0 identity, role1/2 differ only in byte5 by ^1/^2\n");
    {
        const uint8_t base[6] = { 0x7C, 0xBB, 0x8A, 0x11, 0x22, 0x33 };
        uint8_t m0[6], m1[6], m2[6];
        personality_mac(base, 0, m0);
        personality_mac(base, 1, m1);
        personality_mac(base, 2, m2);
        CHECK(memcmp(m0, base, 6) == 0, "role0 identity");
        CHECK(memcmp(m1, base, 5) == 0 && m1[5] == (uint8_t)(0x33 ^ 0x01),
              "role1 byte5 ^1 only");
        CHECK(memcmp(m2, base, 5) == 0 && m2[5] == (uint8_t)(0x33 ^ 0x02),
              "role2 byte5 ^2 only");
    }

    printf("[3] pack vectors (i)-(iii)\n");
    {
        uint8_t o[3];
        joy_pack_btn3(BTN_R | BTN_ZR, EMUL_ROLE_JOY_L, o);
        CHECK(o[0] == 0x00 && o[1] == 0x00 && o[2] == 0x30,
              "(i) R|ZR + JoyL -> {00,00,30}");
        joy_pack_btn3(BTN_A, EMUL_ROLE_JOY_L, o);
        CHECK(o[0] == 0x00 && o[1] == 0x00 && o[2] == 0x00,
              "(ii) A + JoyL -> {0,0,0}");
        joy_pack_btn3(BTN_L | BTN_ZL, EMUL_ROLE_JOY_R, o);
        CHECK(o[0] == 0x30 && o[1] == 0x00 && o[2] == 0x00,
              "(iii) L|ZL + JoyR -> {30,00,00}");
    }

    printf("[4] consumed SL/SR never echo at ProCon positions + drops\n");
    {
        uint8_t o[3];
        joy_pack_btn3(BTN_R | BTN_ZR, EMUL_ROLE_JOY_L, o);
        CHECK((o[0] & 0xC0u) == 0u, "JoyL: R/ZR absent at B0.b6/b7");
        joy_pack_btn3(BTN_L | BTN_ZL, EMUL_ROLE_JOY_R, o);
        CHECK((o[2] & 0xC0u) == 0u, "JoyR: L/ZL absent at B2.b6/b7");
        joy_pack_btn3(BTN_A | BTN_B | BTN_X | BTN_Y | BTN_PLUS | BTN_HOME |
                      BTN_RSTICK, EMUL_ROLE_JOY_L, o);
        CHECK(o[0] == 0x00 && o[1] == 0x00 && o[2] == 0x00,
              "JoyL drops A/B/X/Y/Plus/Home/RSTICK");
        joy_pack_btn3(BTN_DOWN | BTN_UP | BTN_RIGHT | BTN_LEFT | BTN_MINUS |
                      BTN_CAPTURE | BTN_LSTICK, EMUL_ROLE_JOY_R, o);
        CHECK(o[0] == 0x00 && o[1] == 0x00 && o[2] == 0x00,
              "JoyR drops D-pad/Minus/Capture/LSTICK");
        joy_pack_btn3(BTN_DOWN | BTN_L | BTN_ZL | BTN_MINUS | BTN_CAPTURE |
                      BTN_LSTICK, EMUL_ROLE_JOY_L, o);
        CHECK(o[0] == 0x00 && o[1] == 0x29 && o[2] == 0xC1,
              "JoyL keeps Down/L/ZL/Minus/Capture/LSTICK");
        joy_pack_btn3(BTN_A | BTN_R | BTN_ZR | BTN_PLUS | BTN_HOME |
                      BTN_RSTICK, EMUL_ROLE_JOY_R, o);
        CHECK(o[0] == 0xC8 && o[1] == 0x16 && o[2] == 0x00,
              "JoyR keeps A/R/ZR/Plus/Home/RSTICK");
    }

    printf("[5] role0 joy_pack == ctrl_pack_btn3 sweep (16 values)\n");
    {
        static const uint32_t sweep[16] = {
            0x00u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x33u,
            0xFFu, 0x100u, 0x1000u, 0x3000u, 0x30000u, 0xFFFFu,
            0x3FFFFFu, 0xFFFFFFFFu,
        };
        bool all_eq = true;
        for (int k = 0; k < 16; k++) {
            ctrl_state_t st;
            uint8_t a[3], b[3];
            st.buttons = sweep[k];
            st.lx = 0u;
            st.ly = 0u;
            st.rx = 0u;
            st.ry = 0u;
            ctrl_pack_btn3(&st, a);
            joy_pack_btn3(sweep[k], EMUL_ROLE_PROCON, b);
            if (memcmp(a, b, 3) != 0) {
                all_eq = false;
            }
        }
        CHECK(all_eq, "sweep: role0 identical to ctrl_pack_btn3");
    }

    printf("[6] joy_use_left_stick: L=true, R=false, PROCON=true\n");
    {
        CHECK(joy_use_left_stick(EMUL_ROLE_JOY_L), "JoyL -> left");
        CHECK(!joy_use_left_stick(EMUL_ROLE_JOY_R), "JoyR -> right");
        CHECK(joy_use_left_stick(EMUL_ROLE_PROCON), "ProCon -> true");
    }

    printf("[7] spi_joy_blank zero-fill + 0x6000 regression\n");
    {
        uint8_t n = 0xFFu;
        const uint8_t *p = spi_joy_blank(16, &n);
        CHECK(p != NULL, "blank(16) non-NULL");
        CHECK(n == 16, "blank(16) out_len==16");
        {
            static const uint8_t zeros[16] = {0};
            CHECK(p != NULL && memcmp(p, zeros, 16) == 0,
                  "blank(16) all zero");
        }
        CHECK(spi_joy_blank(0, &n) == NULL, "blank(0)->NULL");
        {
            const spi_entry_t *e = spi_find(0x6000);
            CHECK(e != NULL, "find(0x6000) non-NULL");
            CHECK(e != NULL && e->addr == 0x6000 && e->size == 16,
                  "0x6000 size 16");
            CHECK(e != NULL && e->data[0] == 0x00 && e->data[1] == 0x00,
                  "0x6000 first two bytes 00,00");
            CHECK(e != NULL && e->data[2] == 0x58, "0x6000 byte[2]==0x58");
        }
        CHECK(SPI_TABLE_N == 10, "SPI_TABLE_N unchanged (10)");
    }

    printf("[8] T9 cross-check: joy_pack_btn3(u,role) == "
           "usb_role_pack_btn3(ctrl_pack_btn3(u),role) (roles 1,2)\n");
    {
        static const uint32_t sweep[] = {
            0x00u, // no buttons
            1u << 0, 1u << 1, 1u << 2, 1u << 3, // B/A/Y/X single bits
            1u << 4, 1u << 5, // R/ZR (JoyL SL/SR sources)
            1u << 6, 1u << 7, // Plus/RSTICK
            1u << 8, 1u << 9, 1u << 10, 1u << 11, // D-pad single bits
            1u << 12, 1u << 13, // L/ZL (JoyR SL/SR sources)
            1u << 14, 1u << 15, // Minus/LSTICK
            1u << 16, 1u << 17, // Home/Capture
            1u << 18, 1u << 19, 1u << 20, 1u << 21, // GR/GL/C/Headset (dropped both)
            BTN_R | BTN_ZR, // JoyL SL/SR pair
            BTN_L | BTN_ZL, // JoyR SL/SR pair
            BTN_A | BTN_B | BTN_X | BTN_Y, // face cluster
            BTN_DOWN | BTN_UP | BTN_RIGHT | BTN_LEFT, // full D-pad
            0x003FFFFFu, // all 22 BTN bits
            0xFFFFFFFFu, // masked to 22 bits (reserved dropped both sides)
        };
        const int n = (int)(sizeof(sweep) / sizeof(sweep[0]));
        CHECK(n >= 24, "sweep covers >=24 BTN values");
        for (int role = 1; role <= 2; role++) {
            bool role_eq = true;
            for (int k = 0; k < n; k++) {
                ctrl_state_t st;
                uint8_t p3[3], u3[3], j3[3];
                st.buttons = sweep[k];
                st.lx = 0u;
                st.ly = 0u;
                st.rx = 0u;
                st.ry = 0u;
                ctrl_pack_btn3(&st, p3);
                usb_role_pack_btn3(p3, (uint8_t)role, u3);
                joy_pack_btn3(sweep[k], (uint8_t)role, j3);
                if (memcmp(u3, j3, 3) != 0) {
                    printf("  MISMATCH role=%d u=0x%08X "
                           "usb={%02X,%02X,%02X} joy={%02X,%02X,%02X}\n",
                           role, sweep[k], u3[0], u3[1], u3[2],
                           j3[0], j3[1], j3[2]);
                    role_eq = false;
                }
            }
            CHECK(role_eq, role == 1 ?
                  "role1 (JoyL): BT/USB mapper identical over sweep" :
                  "role2 (JoyR): BT/USB mapper identical over sweep");
        }
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
