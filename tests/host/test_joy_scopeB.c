// test_joy_scopeB.c -- Joy-Con scope B contract tests (FAILING-first, RED).
// No Pico SDK needed. CTest name: joy_scopeB. No src/ changes (contract only).
//
// HW fact (Switch wired JoyR init): Switch ran full JoyR init then one-shot
// 80 05 reject tailing on SPI reads (0x6020/0x603D answered zeros). These RED
// tests encode the fix contract: calibration must NOT be all-zeros.
//
// Provisional-value disclaimer: Joy-Con L/R PID/strings/SPI blanks are
// provisional (real-hardware values pending). The 6020/603D expectations
// below pin a provisional static-1g-consistent cal (gyro-bias zeros, accel
// 1g-consistent with Z=0x4000 rest posture like the 0x30 IMU), NOT measured
// factory bytes. Dev/fw/lamp values are likewise provisional until HW capture.
//
// Scenario table (scope B):
//   S-HAPPY-SPI-CAL   -> T-SPI-CAL-6020 / T-SPI-CAL-603D: Joy role1/role2 SPI
//                        0x10 reads must NOT be all-zeros; ACK 0x90, len 64.
//   S-HAPPY-DEVINFO   -> T-DEVINFO-JOY: role1 dev 0x01 / role2 0x02 /
//                        role0 0x03 + fw bytes (03 48 shared).
//   S-EDGE-LAMP       -> T-LAMP-38: role-conditional Home-LED/lamp; L has no
//                        Home LED. GAP: no lamp/Home-LED API exists yet.
//   S-EDGE-ROLE       -> T-ROLE-CONSIST: PID/product/devtype/conn-nibble/
//                        fixed-03 81-type/02-dev/kick81 vector per role 0/1/2 +
//                        oob-role fallback.
//   S-EDGE-HANDSHAKE  -> T-HANDSHAKE-ORDER: kick81 Joy-only/role0-never,
//                        81 01 type == 02 devtype per role, 80/01 order pins,
//                        reject paths (non-0x80, short buf, want==0||>44).
//   S-EDGE-CENTER     -> T-CENTER: absent stick forced 0x800 center.
//   S-EDGE-SLSR       -> T-SLSR: shoulder remap SL/SR survives wired path.
//   S-EDGE-NEUTRAL    -> T-NEUTRAL-RESUME: T_NEUTRAL wire shape pin.
//   S-REG-PROCON      -> T-PROCON-GATE: role0 passthrough gate.
//
// Anchors (existing public APIs only): usb_pid_for_role,
// usb_product_for_role, usb_devtype_for_role, usb_kick81_due,
// usb_role_pack_btn3, spi_joy_blank, ctrl_pack_btn3, pack_stick_12bit,
// usb_build_81_reply, usb_build_21_reply, usb_pack_controller_data,
// usb_build_30_report. Spec SSOT: spec/protocol_v3.md PROTO_VER=4 v4.1
// EMULATE 0x38 (0=ProCon/1=JoyL/2=JoyR).
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"
#include "pack.h"
#include "personality.h"
#include "usb_hid.h"
/* Link spi.c directly (no extra src): usb_hid.c references spi_find
 * in usb_build_21_reply, so the symbol must resolve. Same hack as
 * test_joy_scopeA.c / test_personality.c. */
#include "spi.c"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static bool all_zeros(const uint8_t *p, int n) {
    int k;
    for (k = 0; k < n; k++) {
        if (p[k] != 0u) {
            return false;
        }
    }
    return true;
}

/* ProCon u32 -> Nintendo 3B via ctrl_pack_btn3 (shared helper). */
static void procon3(uint32_t b, uint8_t o[3]) {
    ctrl_state_t st;
    memset(&st, 0, sizeof(st));
    st.buttons = b;
    st.lx = st.ly = st.rx = st.ry = 0x800u;
    ctrl_pack_btn3(&st, o);
}

int main(void) {
    printf("[T-SPI-CAL-6020] Joy SPI 0x6020 want24 must NOT be all-zeros (RED)\n");
    {
        /* S-HAPPY-SPI-CAL: addr 0x6020 want24 -> provisional
         * static-1g-consistent cal, ACK 0x90, out len 64.
         * RED: current Joy path answers blank zeros, so the
         * NOT-zeros checks FAIL until the fix lands. */
        static const uint8_t zeros24[24] = {0};
        for (int role = 1; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t req[16];
            uint8_t out[64];
            char msg[128];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.role = (uint8_t)role;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x10u;
            req[11] = 0x20u; req[12] = 0x60u;
            req[15] = 24u;
            n = usb_build_21_reply(req, 16, out, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-6020 role%d n==64", role);
            CHECK(n == 64, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-6020 role%d ack 90 10", role);
            CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-6020 role%d header 20 60", role);
            CHECK(n == 64 && out[15] == 0x20u && out[16] == 0x60u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-6020 role%d echo==24", role);
            CHECK(n == 64 && out[19] == 24u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-6020 role%d cal NOT all-zeros (provisional static-1g)", role);
            CHECK(n == 64 && memcmp(&out[20], zeros24, 24) != 0, msg);
        }
    }

    printf("[T-SPI-CAL-603D] Joy SPI 0x603D want25 gyro-bias + accel-1g (RED)\n");
    {
        /* S-HAPPY-SPI-CAL: addr 0x603D want25 -> gyro-bias zeros +
         * accel 1g-consistent with Z=0x4000 rest posture (mirrors the
         * 0x30 IMU static-1g contract). Must NOT be all-zeros.
         * RED: current Joy path answers blank zeros, so FAILs. */
        static const uint8_t zeros25[25] = {0};
        for (int role = 1; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t req[16];
            uint8_t out[64];
            char msg[128];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.role = (uint8_t)role;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x10u;
            req[11] = 0x3Du; req[12] = 0x60u;
            req[15] = 25u;
            n = usb_build_21_reply(req, 16, out, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-603D role%d n==64", role);
            CHECK(n == 64, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-603D role%d ack 90 10", role);
            CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-603D role%d header 3D 60", role);
            CHECK(n == 64 && out[15] == 0x3Du && out[16] == 0x60u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-603D role%d echo==25", role);
            CHECK(n == 64 && out[19] == 25u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-CAL-603D role%d cal NOT all-zeros (gyro-bias0/accZ=0x4000 posture)", role);
            CHECK(n == 64 && memcmp(&out[20], zeros25, 25) != 0, msg);
            (void)all_zeros;
        }
    }

    printf("[T-DEVINFO-JOY] device-info dev + fw bytes per role\n");
    {
        /* S-HAPPY-DEVINFO: 0x02 device-info dev byte + fw (role0 03 48;
         * Joy roles 04 33 per PABot parity, live Joy-L bytes).
         * Closest existing API: usb_build_21_reply sub 0x02 + ctx.role.
         * This group PASSES today (pins the vector so CAL RED cannot
         * regress devinfo). */
        static const uint8_t want_dev[3] = { 0x03u, 0x01u, 0x02u };
        static const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        for (int role = 0; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t req[11];
            uint8_t out[64];
            char msg[128];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            memcpy(ctx.mac, mac, 6);
            ctx.role = (uint8_t)role;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x02u;
            n = usb_build_21_reply(req, 11, out, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-DEVINFO-JOY role%d n==64", role);
            CHECK(n == 64, msg);
            snprintf(msg, sizeof(msg), "T-DEVINFO-JOY role%d dev 0x%02X", role, want_dev[role]);
            CHECK(n == 64 && out[13] == 0x82u && out[14] == 0x02u &&
                  out[17] == want_dev[role], msg);
            snprintf(msg, sizeof(msg), "T-DEVINFO-JOY role%d fw %s", role,
                     (role == 0) ? "03 48" : "04 33 (PABot parity)");
            CHECK(n == 64 && ((role == 0 && out[15] == 0x03u && out[16] == 0x48u) ||
                  (role != 0 && out[15] == 0x04u && out[16] == 0x33u)), msg);
        }
    }

    printf("[T-LAMP-38] role-conditional Home-LED/lamp (NO-CHANGE pin)\n");
    {
        /* S-EDGE-LAMP NO-CHANGE pin (SCOPE-5 c=NONE): transcript decode
         * shows 01/48, 01/30, 01/38 all ACKed (shared 0x80/sub shape),
         * session continued; no s=30/38 lamp lines in the reject burst.
         * Hence no lamp/Home-LED behavior change is required here.
         * This group pins the CURRENT accepted behavior: conn-nibble
         * closest-anchor + shared 01/38 ACK shape (default 0x80/0x38,
         * same as 01/30 and 01/48 via usb_build_21_reply).
         * FUTURE HOOK (kept, not asserted): strict L-no-Home-LED
         * expectation (L has no Home LED; R/ProCon lamp
         * role-conditional). Rationale for deferral: no lamp/Home-LED
         * public API exists yet (no usb_* / joy_* / ctrl_* lamp accessor
         * in usb_hid.h, personality.h, pack.h, spi.h, protocol.h); the
         * conn-nibble in usb_pack_controller_data (0x91 all roles per PABot
         * parity) says nothing about lamps. If a lamp API lands,
         * uncomment/extend the disabled CHECK at the end of this group:
         *   CHECK(lamp_L == 0, "T-LAMP-38 FUTURE L-no-Home-LED"); */
        for (int role = 0; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t out12[12];
            char msg[128];
            /* PABot parity: 0x91 all roles (live capture never shows 0x97). */
            uint8_t want_conn = 0x91u;
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = (uint8_t)role;
            usb_pack_controller_data(out12, &ctx);
            snprintf(msg, sizeof(msg), "T-LAMP-38 role%d conn-nibble closest-anchor 0x%02X", role, want_conn);
            CHECK(out12[1] == want_conn, msg);
        }
        /* Shared 01/38 ACK shape pin (current accepted behavior). */
        for (int role = 0; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t req[11];
            uint8_t out[64];
            char msg[128];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = (uint8_t)role;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x38u;
            n = usb_build_21_reply(req, 11, out, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-LAMP-38 role%d 01/38 shared ACK 80 38", role);
            CHECK(n == 64 && out[13] == 0x80u && out[14] == 0x38u, msg);
        }
        /* Shared-shape witnesses: 01/30 and 01/48 use the same 0x80/sub ACK. */
        {
            usb_sub_ctx_t ctx;
            uint8_t req[11];
            uint8_t out[64];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x30u;
            CHECK(usb_build_21_reply(req, 11, out, 64, &ctx) == 64 &&
                  out[13] == 0x80u && out[14] == 0x30u,
                  "T-LAMP-38 01/30 shared ACK 80 30 witness");
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x48u;
            CHECK(usb_build_21_reply(req, 11, out, 64, &ctx) == 64 &&
                  out[13] == 0x80u && out[14] == 0x48u,
                  "T-LAMP-38 01/48 shared ACK 80 48 witness");
        }
        /* FUTURE HOOK (disabled strict expectation, kept for later):
         * CHECK(false, "T-LAMP-38 FUTURE L-no-Home-LED contract unimplemented");
         * Not asserted while SCOPE-5 c=NONE (see above). */
    }

    printf("[T-ROLE-CONSIST] identifier vector per role + oob fallback\n");
    {
        /* S-EDGE-ROLE: PID/product/devtype/conn-nibble/fixed-03 81-type/
         * 02-dev/kick81 vector per role 0/1/2 + oob-role fallback.
         * Mirrors scopeA T-ROLE-CONSIST (passes today; regression pin). */
        static const uint16_t want_pid[3] = { 0x2009u, 0x2009u, 0x2009u };
        static const char *const want_product[3] = {
            "Pro Controller", "Pro Controller", "Pro Controller",
        };
        static const uint8_t want_dev[3] = { 0x03u, 0x01u, 0x02u };
        /* PABot parity: 0x91 all roles. */
        static const uint8_t want_conn[3] = { 0x91u, 0x91u, 0x91u };
        static const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        for (int role = 0; role <= 2; role++) {
            uint8_t r = (uint8_t)role;
            char msg[128];
            usb_sub_ctx_t ctx;
            uint8_t out12[12];
            uint8_t out64[64];
            uint8_t req81[2] = { 0x80u, 0x01u };
            uint8_t req21[11];
            int n81, n21;
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d pid 0x%04X", role, want_pid[role]);
            CHECK(usb_pid_for_role(r) == want_pid[role], msg);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d product %s", role, want_product[role]);
            CHECK(strcmp(usb_product_for_role(r), want_product[role]) == 0, msg);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d devtype 0x%02X", role, want_dev[role]);
            CHECK(usb_devtype_for_role(r) == want_dev[role], msg);
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = r;
            usb_pack_controller_data(out12, &ctx);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d conn 0x%02X", role, want_conn[role]);
            CHECK(out12[1] == want_conn[role], msg);
            /* PABot parity: 81 01 type always 0x03 (mirrors scopeA). */
            n81 = usb_build_81_reply(req81, 2, out64, 64, mac, usb_devtype_for_role(r));
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d 81 type 0x03", role);
            CHECK(n81 == 64 && out64[0] == 0x81u && out64[1] == 0x01u &&
                  out64[3] == 0x03u, msg);
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            memcpy(ctx.mac, mac, 6);
            ctx.role = r;
            memset(req21, 0, sizeof(req21));
            req21[0] = 0x01u; req21[10] = 0x02u;
            n21 = usb_build_21_reply(req21, 11, out64, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d 02 dev 0x%02X", role, want_dev[role]);
            CHECK(n21 == 64 && out64[13] == 0x82u && out64[14] == 0x02u &&
                  out64[17] == want_dev[role], msg);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d kick81 %s", role, (role == 0) ? "never" : "due");
            CHECK(usb_kick81_due(r, true, true, false, false) == (r != 0u), msg);
        }
        CHECK(usb_pid_for_role(9u) == 0x2009u, "T-ROLE-CONSIST oob pid falls back to 2009");
        CHECK(strcmp(usb_product_for_role(9u), "Pro Controller") == 0, "T-ROLE-CONSIST oob product falls back to ProCon");
        CHECK(usb_devtype_for_role(9u) == 0x03u, "T-ROLE-CONSIST oob devtype falls back to 0x03");
        CHECK(!usb_kick81_due(9u, true, true, false, false), "T-ROLE-CONSIST oob role never kick81");
    }

    printf("[T-HANDSHAKE-ORDER] kick81 gating + 81/01 order + reject paths\n");
    {
        /* S-EDGE-HANDSHAKE: kick81 Joy-only/role0-never, 81 01 type ==
         * 02 devtype per role, 80/01 order pins, reject paths:
         * non-0x80, short buf, want==0||want>44. Mirrors scopeA
         * T-HANDSHAKE-ORDER (passes today; regression pin). */
        uint8_t out[64];
        const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        const uint8_t req01[] = { 0x80, 0x01 };
        const uint8_t bad[] = { 0x00, 0x04 };
        const uint8_t short1[] = { 0x80 };
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        CHECK(usb_kick81_due(1, true, true, false, false), "T-HANDSHAKE-ORDER role1 kick81 due");
        CHECK(usb_kick81_due(2, true, true, false, false), "T-HANDSHAKE-ORDER role2 kick81 due");
        CHECK(!usb_kick81_due(0, true, true, false, false), "T-HANDSHAKE-ORDER role0 never kick81");
        for (int role = 0; role <= 2; role++) {
            uint8_t r = (uint8_t)role;
            char msg[128];
            uint8_t o81[64], o21[64];
            usb_sub_ctx_t c2;
            uint8_t q21[11];
            int n81, n21;
            n81 = usb_build_81_reply(req01, 2, o81, 64, mac, usb_devtype_for_role(r));
            memset(&c2, 0, sizeof(c2));
            c2.lx = c2.ly = c2.rx = c2.ry = 0x800u;
            memcpy(c2.mac, mac, 6);
            c2.role = r;
            memset(q21, 0, sizeof(q21));
            q21[0] = 0x01u; q21[10] = 0x02u;
            n21 = usb_build_21_reply(q21, 11, o21, 64, &c2);
            /* PABot parity: 81 01 type is always 0x03 while 02 dev keeps
             * the per-role devtype (mirrors scopeA). */
            snprintf(msg, sizeof(msg), "T-HANDSHAKE-ORDER role%d 81-type 0x03 + 02-dev 0x%02X", role, usb_devtype_for_role(r));
            CHECK(n81 == 64 && n21 == 64 && o81[3] == 0x03u &&
                  o21[17] == usb_devtype_for_role(r), msg);
        }
        {
            int n = usb_build_81_reply(req01, 2, out, 64, mac, 0x03);
            CHECK(n == 64 && out[0] == 0x81 && out[1] == 0x01, "T-HANDSHAKE-ORDER 80 01 acks (sequence opens)");
        }
        CHECK(usb_build_81_reply(bad, 2, out, 64, mac, 0x03) == 0, "T-HANDSHAKE-ORDER non-0x80 rejected");
        CHECK(usb_build_81_reply(short1, 1, out, 64, mac, 0x03) == 0, "T-HANDSHAKE-ORDER short 80 frame rejected");
        CHECK(usb_build_81_reply(req01, 2, out, 63, mac, 0x03) == 0, "T-HANDSHAKE-ORDER short-buf 81 rejected");
        memset(&ctx, 0, sizeof(ctx));
        ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800;
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10;
        req[11] = 0x20; req[12] = 0x60; req[15] = 0;
        CHECK(usb_build_21_reply(req, 16, out, 64, &ctx) == 0, "T-HANDSHAKE-ORDER 10 want==0 rejected");
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10;
        req[11] = 0x20; req[12] = 0x60; req[15] = 45;
        CHECK(usb_build_21_reply(req, 16, out, 64, &ctx) == 0, "T-HANDSHAKE-ORDER 10 want>44 rejected");
    }

    printf("[T-CENTER] absent stick forced to 0x800 center\n");
    {
        /* S-EDGE-CENTER (mirror scopeA): role1 rx/ry garbage must not leak;
         * role2 lx/ly garbage must not leak; present stick stays live. */
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12], want[3];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = 0x123u; ctx.ly = 0xABC;
            ctx.rx = 0x000u; ctx.ry = 0xFFFu;
            ctx.role = 1u;
            usb_pack_controller_data(out12, &ctx);
            pack_stick_12bit(0x800u, 0x800u, want);
            CHECK(memcmp(&out12[8], want, 3) == 0, "T-CENTER role1 rx/ry packed as 0x800 center");
        }
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12], want[3];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = 0x000u; ctx.ly = 0xFFFu;
            ctx.rx = 0x123u; ctx.ry = 0xABC;
            ctx.role = 2u;
            usb_pack_controller_data(out12, &ctx);
            pack_stick_12bit(0x800u, 0x800u, want);
            CHECK(memcmp(&out12[5], want, 3) == 0, "T-CENTER role2 lx/ly packed as 0x800 center");
        }
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12], live[3];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = 0xABC; ctx.ly = 0x123;
            ctx.rx = 0xABC; ctx.ry = 0x123;
            ctx.role = 1u;
            usb_pack_controller_data(out12, &ctx);
            pack_stick_12bit(0xABCu, 0x123u, live);
            CHECK(memcmp(&out12[5], live, 3) == 0, "T-CENTER role1 lx/ly stays live");
            ctx.role = 2u;
            usb_pack_controller_data(out12, &ctx);
            CHECK(memcmp(&out12[8], live, 3) == 0, "T-CENTER role2 rx/ry stays live");
        }
    }

    printf("[T-SLSR] shoulder remap + wired survival\n");
    {
        /* S-EDGE-SLSR (mirror scopeA): JoyL R|ZR -> {00,00,30},
         * JoyR L|ZL -> {30,00,00}, plus wired-path survival. */
        uint8_t o[3];
        joy_pack_btn3(BTN_R | BTN_ZR, EMUL_ROLE_JOY_L, o);
        CHECK(o[0] == 0x00 && o[1] == 0x00 && o[2] == 0x30, "T-SLSR JoyL R|ZR -> {00,00,30}");
        joy_pack_btn3(BTN_L | BTN_ZL, EMUL_ROLE_JOY_R, o);
        CHECK(o[0] == 0x30 && o[1] == 0x00 && o[2] == 0x00, "T-SLSR JoyR L|ZL -> {30,00,00}");
        {
            uint8_t p3[3];
            usb_sub_ctx_t ctx;
            uint8_t out12[12];
            procon3(BTN_R | BTN_ZR, p3);
            memset(&ctx, 0, sizeof(ctx));
            memcpy(ctx.btn, p3, 3);
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = 1u;
            usb_pack_controller_data(out12, &ctx);
            CHECK(out12[4] == 0x30u, "T-SLSR role1 wired byte keeps SL/SR (want 0x30)");
        }
        {
            uint8_t p3[3];
            usb_sub_ctx_t ctx;
            uint8_t out12[12];
            procon3(BTN_L | BTN_ZL, p3);
            memset(&ctx, 0, sizeof(ctx));
            memcpy(ctx.btn, p3, 3);
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = 2u;
            usb_pack_controller_data(out12, &ctx);
            CHECK(out12[2] == 0x30u, "T-SLSR role2 wired byte keeps SL/SR (want 0x30)");
        }
    }

    printf("[T-NEUTRAL-RESUME] neutral wire shape pin\n");
    {
        /* S-EDGE-NEUTRAL (mirror scopeA intent): T_NEUTRAL 0x02 LEN0
         * stays stable; no new TYPE for Joy neutral/resume. */
        CHECK(T_NEUTRAL == 0x02u, "T-NEUTRAL-RESUME type is 0x02");
        CHECK(proto_expected_len(T_NEUTRAL) == 0, "T-NEUTRAL-RESUME expected len 0");
        CHECK(T_EMULATE_MODE == 0x38u, "T-NEUTRAL-RESUME emulate type still 0x38");
    }

    printf("[T-PROCON-GATE] role0 regression gate (passthrough)\n");
    {
        /* S-REG-PROCON (mirror scopeA): role0 joy_pack == ctrl_pack sweep
         * + role0 usb map identity. Must PASS now (locks ProCon path). */
        static const uint32_t sweep[8] = {
            0x00u, 0x01u, 0x33u, 0xFFu, 0x3000u, 0x30000u, 0x3FFFFFu, 0xFFFFFFFFu,
        };
        bool all_eq = true;
        bool ident = true;
        for (int k = 0; k < 8; k++) {
            ctrl_state_t st;
            uint8_t a[3], b[3], p3[3], u3[3];
            st.buttons = sweep[k];
            st.lx = st.ly = st.rx = st.ry = 0u;
            ctrl_pack_btn3(&st, a);
            joy_pack_btn3(sweep[k], EMUL_ROLE_PROCON, b);
            if (memcmp(a, b, 3) != 0) {
                all_eq = false;
            }
            ctrl_pack_btn3(&st, p3);
            usb_role_pack_btn3(p3, 0u, u3);
            if (memcmp(p3, u3, 3) != 0) {
                ident = false;
            }
        }
        CHECK(all_eq, "T-PROCON-GATE role0 joy_pack == ctrl_pack over sweep");
        CHECK(ident, "T-PROCON-GATE role0 usb map is passthrough");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
