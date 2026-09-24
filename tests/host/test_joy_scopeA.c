// test_joy_scopeA.c -- Joy-Con scope A + USB wired contract tests (FAILING-first, RED).
// No Pico SDK needed. CTest name: joy_scopeA. No src/ changes (contract only).
// [PABot-ref]: PABotBase2観測応答を参考に同等機能を再現 (互換主張なし)。詳細は src/usb/usb_hid.c 先頭。
//
// Locked IMU decision (scope A + Copilot review A-plan, [PABot-ref]):
// ProCon 0x30 keeps static-1g rest (2wiCC layout: gyro XYZ=0, accel X/Y=0
// Z=0x4000, same all 3 samples); Joy roles serve zero IMU (PABot live
// capture shows 36B zeros). No new frame TYPE. The USB 0x30 report keeps
// ID + 12B ControllerData + 36B IMU + 15B fill. Joy roles MUST NOT add a
// new T_* frame type or dispatch enum for IMU; no sensor drivers/noise.
// The tail fill stays zero via memset for all roles.
//
// Scenario table (scope A):
//   S-HAPPY-JOY-SPI   -> T-SPI-JOY-HAPPY: Joy role1/role2 SPI 0x10 read
//                        addr 0x6000 want12 -> blank zeros + ACK 0x90.
//   S-EDGE-JOY-SPI    -> T-SPI-JOY-EDGE: Joy SPI edges (blank/
//                        transport-default 0xFF/validation/6050+601B exact;
//                        no NO-REPLY: out-of-range is FF+ACK, see OOR-FF).
//   S-HAPPY-JOYL-USB  -> T-HAPPY role1: enum fixed 2009/Pro Controller
//                        (Plan A) / dev 0x01 response,
//                        kick81 Joy-only, SL/SR survives the USB wired path.
//   S-HAPPY-JOYR-USB  -> T-HAPPY role2: enum fixed 2009/Pro Controller
//                        (Plan A) / dev 0x02 response,
//                        kick81 Joy-only, SL/SR survives the USB wired path.
//   S-EDGE-CENTER     -> T-CENTER: absent stick forced to 0x800 center in
//                        usb_pack_controller_data (role1: rx/ry, role2: lx/ly).
//   S-EDGE-SLSR       -> T-SLSR: JoyL R|ZR -> {00,00,30} with B0.b6/b7 absent;
//                        JoyR L|ZL -> {30,00,00} with B2.b6/b7 absent.
//   S-EDGE-REBOOT     -> T-REBOOT: T_EMULATE_MODE (0x38) LEN1 role intent;
//                        FW applies via reboot like WIRED_MODE (FX_EMULATE_MODE).
//   S-REG-PROCON      -> T-PROCON-GATE: role0 passthrough, joy_pack_btn3 ==
//                        ctrl_pack_btn3 over sweep + usb_role_pack_btn3 identity.
//
// Anchors: usb_pid_for_role, usb_product_for_role, usb_devtype_for_role,
// usb_kick81_due, usb_role_pack_btn3, joy_pack_btn3, joy_use_left_stick,
// ctrl_pack_btn3, pack_stick_12bit. Spec SSOT: spec/protocol_v3.md PROTO_VER=4
// v4.1 EMULATE_MODE 0x38 (0=ProCon/1=JoyL/2=JoyR) FW_MINOR=2.
// v4.3 COLOR_GET 0x39 / COLOR_INFO 0x3A FW_MINOR=3.
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "pack.h"
#include "personality.h"
#include "usb_hid.h"
/* Link spi.c directly (no CMakeLists change): usb_hid.c references spi_find
 * in usb_build_21_reply, so the symbol must resolve even though this test
 * never calls the 0x21 path. Same hack as test_personality.c. */
#include "spi.c"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

/* ProCon u32 -> Nintendo 3B via ctrl_pack_btn3 (shared helper). */
static void procon3(uint32_t b, uint8_t o[3]) {
    ctrl_state_t st;
    memset(&st, 0, sizeof(st));
    st.buttons = b;
    st.lx = st.ly = st.rx = st.ry = 0x800u;
    ctrl_pack_btn3(&st, o);
}

int main(void) {
    printf("[T-HAPPY] USB identity + kick81 + Joy SL/SR on the wired path\n");
    {
        /* Plan A: USB enumeration is fixed 057E:2009 Pro Controller for all
         * roles (PABotBase2-Pico2W: USB descriptors are 2009-only). Role
         * expression stays in responses (devtype/btn/stick/battery/SPI). */
        CHECK(usb_pid_for_role(0) == 0x2009u, "T-HAPPY role0 pid 2009");
        CHECK(usb_pid_for_role(1) == 0x2009u, "T-HAPPY role1 pid 2009 fixed");
        CHECK(usb_pid_for_role(2) == 0x2009u, "T-HAPPY role2 pid 2009 fixed");
        CHECK(strcmp(usb_product_for_role(0), "Pro Controller") == 0,
              "T-HAPPY role0 product Pro Controller");
        CHECK(strcmp(usb_product_for_role(1), "Pro Controller") == 0,
              "T-HAPPY role1 product Pro Controller fixed");
        CHECK(strcmp(usb_product_for_role(2), "Pro Controller") == 0,
              "T-HAPPY role2 product Pro Controller fixed");
        CHECK(usb_devtype_for_role(0) == 0x03u, "T-HAPPY role0 devtype 3");
        CHECK(usb_devtype_for_role(1) == 0x01u, "T-HAPPY role1 devtype 1");
        CHECK(usb_devtype_for_role(2) == 0x02u, "T-HAPPY role2 devtype 2");
        CHECK(usb_kick81_due(1, true, true, false, false),
              "T-HAPPY role1 kick81 due when wired+mounted");
        CHECK(usb_kick81_due(2, true, true, false, false),
              "T-HAPPY role2 kick81 due when wired+mounted");
        CHECK(!usb_kick81_due(0, true, true, false, false),
              "T-HAPPY role0 never kick81 (ProCon proven path)");
        /* S-HAPPY-JOYL-USB / S-HAPPY-JOYR-USB: SL/SR must survive the full
         * USB wired path (ctrl pack -> role map -> controller_data). */
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
            CHECK(out12[4] == 0x30u,
                  "T-HAPPY role1 SL/SR survives to USB wired byte (want 0x30)");
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
            CHECK(out12[2] == 0x30u,
                  "T-HAPPY role2 SL/SR survives to USB wired byte (want 0x30)");
        }
    }

    printf("[T-CENTER] absent stick forced to 0x800 center (USB wired)\n");
    {
        /* S-EDGE-CENTER role1: rx/ry garbage must not leak; packed right
         * stick must equal pack_stick_12bit(0x800,0x800). */
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12], want[3];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = 0x123u; ctx.ly = 0xABC;
            ctx.rx = 0x000u; ctx.ry = 0xFFFu; /* garbage on absent side */
            ctx.role = 1u;
            usb_pack_controller_data(out12, &ctx);
            pack_stick_12bit(0x800u, 0x800u, want);
            CHECK(ctx.rx == 0x000u,
                  "T-CENTER role1 input garbage present (guard)");
            CHECK(memcmp(&out12[8], want, 3) == 0,
                  "T-CENTER role1 rx/ry packed as 0x800 center");
        }
        /* S-EDGE-CENTER role2: lx/ly garbage must not leak. */
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12], want[3];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = 0x000u; ctx.ly = 0xFFFu; /* garbage on absent side */
            ctx.rx = 0x123u; ctx.ry = 0xABC;
            ctx.role = 2u;
            usb_pack_controller_data(out12, &ctx);
            pack_stick_12bit(0x800u, 0x800u, want);
            CHECK(memcmp(&out12[5], want, 3) == 0,
                  "T-CENTER role2 lx/ly packed as 0x800 center");
        }
        /* Present stick stays live (not clamped to center). */
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12], live[3], centered[3];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = 0xABC; ctx.ly = 0x123;
            ctx.rx = 0xABC; ctx.ry = 0x123;
            ctx.role = 1u;
            usb_pack_controller_data(out12, &ctx);
            pack_stick_12bit(0xABCu, 0x123u, live);
            pack_stick_12bit(0x800u, 0x800u, centered);
            CHECK(memcmp(&out12[5], live, 3) == 0,
                  "T-CENTER role1 lx/ly stays live");
            ctx.role = 2u;
            usb_pack_controller_data(out12, &ctx);
            CHECK(memcmp(&out12[8], live, 3) == 0,
                  "T-CENTER role2 rx/ry stays live");
        }
        /* 0x30 report: Joy IMU 36B zeros ([PABot-ref]: live capture shows
         * zero IMU, not static-1g; ProCon keeps static-1g, see test_usb
         * [6] + T-PABOT-BATT role0 pin). Tail 15B fill stays zero. */
        {
            usb_sub_ctx_t ctx;
            uint8_t rep[64];
            static const uint8_t zeros15[15] = {0};
            static const uint8_t zeros36[36] = {0};
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = 1u;
            CHECK(usb_build_30_report(&ctx, rep) == 64,
                  "T-CENTER role1 30 report builds 64B");
            CHECK(rep[0] == 0x30u, "T-CENTER role1 30 report ID stays 0x30");
            CHECK(memcmp(&rep[13], zeros36, 36) == 0,
                  "T-CENTER role1 IMU 36B zeros [PABot-ref]");
            CHECK(memcmp(&rep[49], zeros15, 15) == 0,
                  "T-CENTER role1 tail 15B fill stays zero");
        }
    }

    printf("[T-SLSR] shoulder pair remap + consumed bits absent\n");
    {
        uint8_t o[3];
        /* S-EDGE-SLSR vectors. */
        joy_pack_btn3(BTN_R | BTN_ZR, EMUL_ROLE_JOY_L, o);
        CHECK(o[0] == 0x00 && o[1] == 0x00 && o[2] == 0x30,
              "T-SLSR JoyL R|ZR -> {00,00,30}");
        CHECK((o[0] & 0xC0u) == 0u,
              "T-SLSR JoyL R/ZR absent at B0.b6/b7");
        joy_pack_btn3(BTN_L | BTN_ZL, EMUL_ROLE_JOY_R, o);
        CHECK(o[0] == 0x30 && o[1] == 0x00 && o[2] == 0x00,
              "T-SLSR JoyR L|ZL -> {30,00,00}");
        CHECK((o[2] & 0xC0u) == 0u,
              "T-SLSR JoyR L/ZL absent at B2.b6/b7");
        /* USB role mapper agrees with BT mapper over the SL/SR pair. */
        {
            uint8_t p3[3], u3[3], j3[3];
            procon3(BTN_R | BTN_ZR, p3);
            usb_role_pack_btn3(p3, 1u, u3);
            joy_pack_btn3(BTN_R | BTN_ZR, 1u, j3);
            CHECK(memcmp(u3, j3, 3) == 0,
                  "T-SLSR role1 USB/BT mapper identical for R|ZR");
            procon3(BTN_L | BTN_ZL, p3);
            usb_role_pack_btn3(p3, 2u, u3);
            joy_pack_btn3(BTN_L | BTN_ZL, 2u, j3);
            CHECK(memcmp(u3, j3, 3) == 0,
                  "T-SLSR role2 USB/BT mapper identical for L|ZL");
        }
        /* Wired-path survival (same contract as T-HAPPY, edge angle). */
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
            CHECK(out12[4] == 0x30u,
                  "T-SLSR role1 wired byte keeps SL/SR (want 0x30)");
        }
    }

    printf("[T-REBOOT] EMULATE_MODE intent (reboot-apply, no new TYPE)\n");
    {
        /* S-EDGE-REBOOT: T_EMULATE_MODE 0x38 LEN1 role 0/1/2. No new frame
         * TYPE is added for Joy scope A; role switch is an FX_EMULATE_MODE
         * intent applied by reboot (~500ms, Flash-persisted like WIRED_MODE).
         * dispatch.c owns FX_EMULATE_MODE; this contract only pins the wire
         * shape so the intent stays stable. Latch proof (Blocker 4): the FX
         * leg is pinned in test_config.c [22][23], main.c exec_fx persists
         * + arms s_reboot_at, usb_set_role runs once pre-enumeration —
         * mid-session persona switch is impossible by construction. */
        CHECK(T_EMULATE_MODE == 0x38u, "T-REBOOT type is 0x38");
        CHECK(proto_expected_len(T_EMULATE_MODE) == 1,
              "T-REBOOT expected len 1");
        CHECK(EMUL_ROLE_PROCON == 0 && EMUL_ROLE_JOY_L == 1 &&
              EMUL_ROLE_JOY_R == 2,
              "T-REBOOT roles 0/1/2");
        CHECK(T_COLOR_GET == 0x39u && proto_expected_len(T_COLOR_GET) == 0,
              "T-REBOOT next TYPE 0x39=COLOR_GET LEN0 (append-only)");
        CHECK(T_COLOR_INFO == 0x3Au && proto_expected_len(T_COLOR_INFO) == 12,
              "T-REBOOT 0x3A=COLOR_INFO LEN12 (Pico->PC)");
        CHECK(proto_expected_len(0x3B) == -1,
              "T-REBOOT no further TYPE next to 0x3A (0x3B unknown)");
    }

    printf("[T-PROCON-GATE] role0 regression gate (passthrough)\n");
    {
        /* S-REG-PROCON: role0 joy_pack_btn3 == ctrl_pack_btn3 sweep. */
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
            st.lx = st.ly = st.rx = st.ry = 0u;
            ctrl_pack_btn3(&st, a);
            joy_pack_btn3(sweep[k], EMUL_ROLE_PROCON, b);
            if (memcmp(a, b, 3) != 0) {
                all_eq = false;
            }
        }
        CHECK(all_eq,
              "T-PROCON-GATE role0 joy_pack == ctrl_pack over sweep");
        /* role0 usb_role_pack_btn3 is identity. */
        {
            bool ident = true;
            for (int k = 0; k < 16; k++) {
                ctrl_state_t st;
                uint8_t p3[3], u3[3];
                st.buttons = sweep[k];
                st.lx = st.ly = st.rx = st.ry = 0u;
                ctrl_pack_btn3(&st, p3);
                usb_role_pack_btn3(p3, 0u, u3);
                if (memcmp(p3, u3, 3) != 0) {
                    ident = false;
                }
            }
            CHECK(ident, "T-PROCON-GATE role0 usb map is passthrough");
        }
        CHECK(joy_use_left_stick(EMUL_ROLE_PROCON),
              "T-PROCON-GATE ProCon uses left stick");
    }

    printf("[T-ROLE-CONSIST] full role-visible identifier vector per role\n");
    {
        /* External review: every USB-visible identifier must agree on the
         * same role. Sweep roles 0/1/2 and pin the full vector per role:
         *   pid+product FIXED to 2009/Pro Controller for all roles (Plan A:
         *   USB enumerates as ProCon; PABotBase2-Pico2W measured 2009-only) /
         *   devtype / conn nibble (out12[1]) /
         *   0x81 reply type byte (out[3]) / 0x02 device-info dev byte
         *   (out[17]) / kick81 due (only 1/2).
         * Anchors: usb_pid_for_role, usb_product_for_role,
         * usb_devtype_for_role, usb_pack_controller_data, usb_build_81_reply,
         * usb_build_21_reply, usb_kick81_due. */
        static const uint16_t want_pid[3] = { 0x2009u, 0x2009u, 0x2009u };
        static const char *const want_product[3] = {
            "Pro Controller", "Pro Controller", "Pro Controller",
        };
        static const uint8_t want_dev[3] = { 0x03u, 0x01u, 0x02u };
        /* [PABot-ref]: battery/conn byte is 0x91 for ALL roles (live
         * capture never shows 0x97). */
        static const uint8_t want_conn[3] = { 0x91u, 0x91u, 0x91u };
        static const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        for (int role = 0; role <= 2; role++) {
            uint8_t r = (uint8_t)role;
            char msg[96];
            usb_sub_ctx_t ctx;
            uint8_t out12[12];
            uint8_t out64[64];
            uint8_t req81[2] = { 0x80u, 0x01u };
            uint8_t req21[11];
            int n81, n21;
            snprintf(msg, sizeof(msg),
                     "T-ROLE-CONSIST role%d pid 0x%04X", role, want_pid[role]);
            CHECK(usb_pid_for_role(r) == want_pid[role], msg);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d product %s",
                     role, want_product[role]);
            CHECK(strcmp(usb_product_for_role(r), want_product[role]) == 0, msg);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d devtype 0x%02X",
                     role, want_dev[role]);
            CHECK(usb_devtype_for_role(r) == want_dev[role], msg);
            /* conn/battery nibble in controller data. */
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = r;
            usb_pack_controller_data(out12, &ctx);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d conn 0x%02X",
                     role, want_conn[role]);
            CHECK(out12[1] == want_conn[role], msg);
            /* [PABot-ref]: 81 01 controller type is always 0x03 (type is
             * enumeration identity, not role expression). 0x02 dev byte
             * below keeps the per-role devtype. */
            n81 = usb_build_81_reply(req81, 2, out64, 64, mac,
                                     usb_devtype_for_role(r));
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d 81 type 0x03", role);
            CHECK(n81 == 64 && out64[0] == 0x81u && out64[1] == 0x01u &&
                  out64[3] == 0x03u, msg);
            /* 0x02 device-info dev_type byte carries the same devtype. */
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            memcpy(ctx.mac, mac, 6);
            ctx.role = r;
            memset(req21, 0, sizeof(req21));
            req21[0] = 0x01u; req21[10] = 0x02u;
            n21 = usb_build_21_reply(req21, 11, out64, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d 02 dev 0x%02X",
                     role, want_dev[role]);
            CHECK(n21 == 64 && out64[13] == 0x82u && out64[14] == 0x02u &&
                  out64[17] == want_dev[role], msg);
            /* kick81 due only for Joy roles. */
            snprintf(msg, sizeof(msg), "T-ROLE-CONSIST role%d kick81 %s",
                     role, (role == 0) ? "never" : "due");
            CHECK(usb_kick81_due(r, true, true, false, false) == (r != 0u),
                  msg);
        }
        /* Out-of-range role falls back to the ProCon vector, never kicks. */
        CHECK(usb_pid_for_role(9u) == 0x2009u,
              "T-ROLE-CONSIST oob pid falls back to 2009");
        CHECK(strcmp(usb_product_for_role(9u), "Pro Controller") == 0,
              "T-ROLE-CONSIST oob product falls back to ProCon");
        CHECK(usb_devtype_for_role(9u) == 0x03u,
              "T-ROLE-CONSIST oob devtype falls back to 0x03");
        {
            usb_sub_ctx_t ctx;
            uint8_t out12[12];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = 9u;
            usb_pack_controller_data(out12, &ctx);
            CHECK(out12[1] == 0x91u,
                  "T-ROLE-CONSIST oob conn falls back to 0x91");
        }
        CHECK(!usb_kick81_due(9u, true, true, false, false),
              "T-ROLE-CONSIST oob role never kick81");
        /* Role0 byte-identity: the ProCon vector stays verbatim. */
        {
            usb_sub_ctx_t ctx;
            uint8_t out64[64];
            uint8_t req81[2] = { 0x80u, 0x01u };
            uint8_t req21[11];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            memcpy(ctx.mac, mac, 6);
            ctx.role = 0u;
            memset(req21, 0, sizeof(req21));
            req21[0] = 0x01u; req21[10] = 0x02u;
            CHECK(usb_build_81_reply(req81, 2, out64, 64, mac, 0x03u) == 64 &&
                  out64[3] == 0x03u,
                  "T-ROLE-CONSIST role0 81 type stays 0x03");
            CHECK(usb_build_21_reply(req21, 11, out64, 64, &ctx) == 64 &&
                  out64[17] == 0x03u,
                  "T-ROLE-CONSIST role0 02 dev stays 0x03");
        }
    }

    printf("[T-HANDSHAKE-ORDER] wired handshake gating sequence\n");
    {
        /* Replay-style contract for the usb_wired handshake machine.
         * usb_wired.c is NOT unit-callable here (needs tusb.h/pico/time.h),
         * so this group pins the machine at its builder-level gates and
         * documents the order it enforces:
         *   mount -> 80 01..04 (80 04 sets handshake_done=true,
         *     tud_hid_set_report_cb) -> 0x30 input stream (usb_wired_task
         *     gate: !handshake_done returns, no input before 80 04);
         *   80 05 clears handshake_done; unmount/reconnect clears
         *   (tud_umount_cb/usb_wired_reconnect); 01 03 mode 0x30 is the
         *   alternate full-start setter. Garbage must never advance the
         *   machine: every reject path below returns 0 (send nothing). */
        uint8_t out[64];
        const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        const uint8_t req01[] = { 0x80, 0x01 };
        const uint8_t req04[] = { 0x80, 0x04 };
        const uint8_t req05[] = { 0x80, 0x05 };
        const uint8_t bad[] = { 0x00, 0x04 };
        const uint8_t short1[] = { 0x80 };
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        int n;
        /* 1. The opener acks (handshake can start). */
        n = usb_build_81_reply(req01, 2, out, 64, mac, 0x03);
        CHECK(n == 64 && out[0] == 0x81 && out[1] == 0x01,
              "T-HANDSHAKE-ORDER 80 01 acks (sequence opens)");
        /* 2. 80 04 is the setter packet: builder queues nothing (Change B:
         * real-HW silent quirk, no 81 04). handshake_done=true is set by
         * the receive path (tud_hid_set_report_cb), not observable here. */
        n = usb_build_81_reply(req04, 2, out, 64, mac, 0x03);
        CHECK(n == 0,
              "T-HANDSHAKE-ORDER 80 04 silent (setter, hs=true by rx path)");
        /* 3. 80 05 is the clearer packet: must ack 64B (its arrival sets
         * handshake_done=false). */
        n = usb_build_81_reply(req05, 2, out, 64, mac, 0x03);
        CHECK(n == 64 && out[0] == 0x81 && out[1] == 0x05,
              "T-HANDSHAKE-ORDER 80 05 acks (clearer, hs=false)");
        /* 4. Garbage never advances the machine: non-0x80 rejected. */
        CHECK(usb_build_81_reply(bad, 2, out, 64, mac, 0x03) == 0,
              "T-HANDSHAKE-ORDER non-0x80 rejected");
        /* 5. Short 80 frame (<2B) rejected. */
        CHECK(usb_build_81_reply(short1, 1, out, 64, mac, 0x03) == 0,
              "T-HANDSHAKE-ORDER short 80 frame rejected");
        /* 6. Short reply buffer rejected (replies are always 64B). */
        CHECK(usb_build_81_reply(req01, 2, out, 63, mac, 0x03) == 0,
              "T-HANDSHAKE-ORDER short-buf 81 rejected");
        /* 7. 0x21 path: short req (<11B) rejected. */
        memset(&ctx, 0, sizeof(ctx));
        ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800;
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x02;
        CHECK(usb_build_21_reply(req, 10, out, 64, &ctx) == 0,
              "T-HANDSHAKE-ORDER 21 short-req rejected");
        /* 8. 0x21 path: non-0x01 leading byte rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x02; req[10] = 0x02;
        CHECK(usb_build_21_reply(req, 11, out, 64, &ctx) == 0,
              "T-HANDSHAKE-ORDER 21 non-0x01 rejected");
        /* 9. 0x21 path: short reply buffer rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x02;
        CHECK(usb_build_21_reply(req, 11, out, 63, &ctx) == 0,
              "T-HANDSHAKE-ORDER 21 short-buf rejected");
        /* 10. SPI read: want==0 rejected (no zero-length answer). */
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10;
        req[11] = 0x50; req[12] = 0x60; req[15] = 0;
        CHECK(usb_build_21_reply(req, 16, out, 64, &ctx) == 0,
              "T-HANDSHAKE-ORDER 10 want==0 rejected");
        /* 11. SPI read: want>44 rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10;
        req[11] = 0x50; req[12] = 0x60; req[15] = 45;
        CHECK(usb_build_21_reply(req, 16, out, 64, &ctx) == 0,
              "T-HANDSHAKE-ORDER 10 want>44 rejected");
        /* 12. SPI read: short frame (<16B) rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10;
        req[11] = 0x50; req[12] = 0x60; req[15] = 12;
        CHECK(usb_build_21_reply(req, 15, out, 64, &ctx) == 0,
              "T-HANDSHAKE-ORDER 10 short-frame rejected");
        /* 13. Alternate full-start: 01 03 mode 0x30 acks (documented
         * handshake_done setter for hosts that skip 80 04). */
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x03; req[11] = 0x30;
        n = usb_build_21_reply(req, 12, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x80 && out[14] == 0x03,
              "T-HANDSHAKE-ORDER 01 03 mode30 acks (alt full-start)");
    }

    printf("[T-HS-8004-SILENT] 80 04 gets no 81 response (real-HW quirk)\n");
    {
        /* Change B: real HW is silent on 80 04 (known FW quirk); we must not
         * queue 81 04. handshake_done=true is still set by the receive path
         * (usb_wired.c tud_hid_set_report_cb, not observable at builder
         * level — the path queues pend only when this builder returns 64).
         * 80 05 keeps its 81 05 reply + handshake_done=false. kick81/Joy
         * logic untouched. RED: builder currently acks 81 04. */
        uint8_t out[64];
        const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        const uint8_t req04[] = { 0x80, 0x04 };
        const uint8_t req05[] = { 0x80, 0x05 };
        const uint8_t req01[] = { 0x80, 0x01 };
        CHECK(usb_build_81_reply(req04, 2, out, 64, mac, 0x03) == 0,
              "T-HS-8004-SILENT 80 04 queues nothing");
        CHECK(usb_build_81_reply(req05, 2, out, 64, mac, 0x03) == 64 &&
              out[0] == 0x81u && out[1] == 0x05u,
              "T-HS-8004-SILENT 80 05 keeps 81 05 reply");
        CHECK(usb_build_81_reply(req01, 2, out, 64, mac, 0x03) == 64 &&
              out[0] == 0x81u && out[1] == 0x01u,
              "T-HS-8004-SILENT 80 01 still acks (sanity)");
    }

    printf("[T-PABOT-81] 81 01 controller type always 0x03 [PABot-ref]\n");
    {
        /* [PABot-ref]: live Joy-L capture answers 81 01 00 03 even in Joy
         * mode (type is enumeration identity, not role expression).
         * RED: roles 1/2 currently answer 01/02. */
        uint8_t out[64];
        const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        const uint8_t req01[] = { 0x80, 0x01 };
        for (int role = 0; role <= 2; role++) {
            char msg[96];
            int n = usb_build_81_reply(req01, 2, out, 64, mac,
                                       usb_devtype_for_role((uint8_t)role));
            snprintf(msg, sizeof(msg), "T-PABOT-81 role%d type 0x03", role);
            CHECK(n == 64 && out[0] == 0x81u && out[1] == 0x01u &&
                  out[3] == 0x03u, msg);
        }
    }

    printf("[T-PABOT-02] Joy 0x02 fw 04 33 + reversed MAC [PABot-ref]\n");
    {
        /* [PABot-ref] (Joy-L live bytes, capture T07):
         * 82 02 04 33 01 02 <mac6 reversed vs 81 01> 01 01 00.
         * RED: Joy currently answers fw 03 48, straight MAC, byte11 0x02.
         * role0 keeps 03 48 / straight MAC / 0x02. */
        static const uint8_t mac[6] = { 0x81, 0x2F, 0x03, 0x9E, 0xA2, 0x88 };
        usb_sub_ctx_t ctx;
        uint8_t req[11];
        uint8_t o81[64], o02[64];
        int n, m, k;
        bool rev = true;
        memset(&ctx, 0, sizeof(ctx));
        ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
        memcpy(ctx.mac, mac, 6);
        ctx.role = 1u;
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x02u;
        n = usb_build_21_reply(req, 11, o02, 64, &ctx);
        CHECK(n == 64 && o02[13] == 0x82u && o02[14] == 0x02u &&
              o02[15] == 0x04u && o02[16] == 0x33u && o02[17] == 0x01u,
              "T-PABOT-02 Joy fw 04 33 type 01");
        CHECK(n == 64 && o02[26] == 0x01u,
              "T-PABOT-02 Joy byte11 0x01");
        {
            const uint8_t req81[] = { 0x80, 0x01 };
            m = usb_build_81_reply(req81, 2, o81, 64, mac,
                                   usb_devtype_for_role(1u));
            for (k = 0; k < 6; k++) {
                if (o02[19 + k] != o81[4 + (5 - k)]) {
                    rev = false;
                }
            }
            CHECK(m == 64 && n == 64 && rev,
                  "T-PABOT-02 Joy MAC reversed vs 81 01");
        }
    }

    printf("[T-PABOT-BATT] Joy battery 0x91 + 0x30 vib/IMU zeros [PABot-ref]\n");
    {
        /* [PABot-ref]: every 0x30/0x21 header battery byte is 0x91 (never
         * 0x97); Joy 0x30 byte[12]==0x00 and IMU[13..48]==0x00 (live capture
         * shows zero IMU, not static-1g). ProCon keeps 0x91/0x09/static-1g.
         * RED: Joy currently answers 0x97/0x09/static-1g. */
        static const uint8_t zeros36[36] = {0};
        for (int role = 1; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t h12[12];
            uint8_t rep[64];
            char msg[96];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = (uint8_t)role;
            usb_pack_controller_data(h12, &ctx);
            snprintf(msg, sizeof(msg), "T-PABOT-BATT role%d header 0x91", role);
            CHECK(h12[1] == 0x91u, msg);
            n = usb_build_30_report(&ctx, rep);
            snprintf(msg, sizeof(msg), "T-PABOT-BATT role%d 0x30 [12]==0x00", role);
            CHECK(n == 64 && rep[0] == 0x30u && rep[12] == 0x00u, msg);
            snprintf(msg, sizeof(msg), "T-PABOT-BATT role%d 0x30 IMU zeros", role);
            CHECK(n == 64 && memcmp(&rep[13], zeros36, 36) == 0, msg);
        }
        {
            usb_sub_ctx_t ctx;
            uint8_t h12[12];
            uint8_t rep[64];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = 0u;
            usb_pack_controller_data(h12, &ctx);
            CHECK(h12[1] == 0x91u,
                  "T-PABOT-BATT role0 header 0x91 (unchanged)");
            CHECK(usb_build_30_report(&ctx, rep) == 64 && rep[12] == 0x09u &&
                  memcmp(&rep[13], zeros36, 36) != 0,
                  "T-PABOT-BATT role0 0x09 + static-1g (unchanged)");
        }
    }

    printf("[T-PABOT-HDR] Joy header bytes w/o forced bits [PABot-ref]\n");
    {
        /* [PABot-ref] (Joy-L live bytes): button byte carries b3[1] as-is
         * (idle 0x00 — no forced 0x80 bit) in both 0x30 and 0x21 headers,
         * and the 0x21 vib byte is 0x00. ProCon keeps |0x80 and 0x09.
         * RED: Joy currently forces 0x80 and 0x09. */
        for (int role = 1; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t h12[12];
            uint8_t rep30[64], rep21[64];
            uint8_t req[11];
            char msg[96];
            int n30, n21;
            /* Minus survives the JoyL mapper, Plus survives JoyR; both must
             * pass through without a forced 0x80 bit. */
            uint8_t pressed = (role == 1) ? 0x01u : 0x02u;
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = (uint8_t)role;
            usb_pack_controller_data(h12, &ctx);
            snprintf(msg, sizeof(msg), "T-PABOT-HDR role%d idle btn ==0x00", role);
            CHECK(h12[3] == 0x00u, msg);
            ctx.btn[1] = pressed;
            usb_pack_controller_data(h12, &ctx);
            snprintf(msg, sizeof(msg), "T-PABOT-HDR role%d pressed bit kept w/o 0x80", role);
            CHECK(h12[3] == pressed, msg);
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = (uint8_t)role;
            n30 = usb_build_30_report(&ctx, rep30);
            snprintf(msg, sizeof(msg), "T-PABOT-HDR role%d 0x30 [4]==0x00", role);
            CHECK(n30 == 64 && rep30[4] == 0x00u, msg);
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x03u;
            n21 = usb_build_21_reply(req, 11, rep21, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-PABOT-HDR role%d 0x21 [4]==0x00", role);
            CHECK(n21 == 64 && rep21[4] == 0x00u, msg);
            snprintf(msg, sizeof(msg), "T-PABOT-HDR role%d 0x21 [12]==0x00", role);
            CHECK(n21 == 64 && rep21[12] == 0x00u, msg);
        }
        {
            usb_sub_ctx_t ctx;
            uint8_t h12[12];
            uint8_t rep21[64];
            uint8_t req[11];
            memset(&ctx, 0, sizeof(ctx));
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800u;
            ctx.role = 0u;
            usb_pack_controller_data(h12, &ctx);
            CHECK(h12[3] == 0x80u,
                  "T-PABOT-HDR role0 idle btn keeps 0x80 (unchanged)");
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x03u;
            CHECK(usb_build_21_reply(req, 11, rep21, 64, &ctx) == 64 &&
                  rep21[12] == 0x09u,
                  "T-PABOT-HDR role0 0x21 [12]==0x09 (unchanged)");
        }
    }

    printf("[T-PABOT-6086] Joy 0x6086 want18 FF fill [PABot-ref]\n");
    {
        /* [PABot-ref]: 6086+18 answers 18xFF (capture T14). Explicit Joy
         * branch before the blank window; NOT a shared table entry (avoids
         * BT impact — BT Joy keeps blank per its transport).
         * RED: currently blank zeros. */
        static const uint8_t ff18[18] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
        };
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        uint8_t out[64];
        int n;
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 1u;
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x86u; req[12] = 0x60u;
        req[15] = 18u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[19] == 18u && memcmp(&out[20], ff18, 18) == 0,
              "T-PABOT-6086 Joy 6086 want18 FF fill");
    }

    printf("[T-SPI-JOY-HAPPY] Joy SPI 0x6000 0xFF serial-none (Change A)\n");
    {
        /* S-HAPPY-JOY-SPI: Joy role1/role2 SPI 0x10 read addr 0x6000
         * want12 -> 0xFF serial-none + ACK 0x90 (Change A: 0x6000 is
         * always 0xFF for both roles; real serials risk 2162-0002). */
        static const uint8_t ff12[12] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                         0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        for (int role = 1; role <= 2; role++) {
            usb_sub_ctx_t ctx;
            uint8_t req[16];
            uint8_t out[64];
            char msg[96];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.role = (uint8_t)role;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x10u;
            req[11] = 0x00u; req[12] = 0x60u;
            req[15] = 12u;
            n = usb_build_21_reply(req, 16, out, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-HAPPY role%d n==64", role);
            CHECK(n == 64, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-HAPPY role%d ack 90 10", role);
            CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-HAPPY role%d header 00 60", role);
            CHECK(n == 64 && out[15] == 0x00u && out[16] == 0x60u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-HAPPY role%d want==12", role);
            CHECK(n == 64 && out[19] == 12u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-HAPPY role%d 0xFF serial-none12", role);
            CHECK(n == 64 && memcmp(&out[20], ff12, 12) == 0, msg);
        }
    }

    printf("[T-SPI-JOY-EDGE] Joy SPI edges (RED)\n");
    {
        /* S-EDGE-JOY-SPI: blank/transport-default/validation/exact.
         * ORDER: 6050 exact beats blank (exact checked before blank).
         * Below-window 0x5FFF is USB transport-default FF+ACK (Blocker 3;
         * full-byte pin lives in T-SPI-JOY-OOR-FF, no NO-REPLY naming). */
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        uint8_t out[64];
        int n;
        static const uint8_t ff8[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        static const uint8_t ff16[16] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                         0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        static const uint8_t zeros3[3] = {0};
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 1u;
        /* 0x6000 want1 -> 0xFF serial-none + ACK 0x90 (Change A). */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x60u;
        req[15] = 1u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[20] == 0xFFu,
              "T-SPI-JOY-EDGE 6000 want1 serial-none+0x90");
        /* 0x8FFF want1 -> blank zero + ACK 0x90 (window top). */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0xFFu; req[12] = 0x8Fu;
        req[15] = 1u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[20] == 0x00u,
              "T-SPI-JOY-EDGE 8FFF want1 blank+0x90");
        /* 0x9000 -> USB transport-default 0xFF fill, n==64. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x90u;
        req[15] = 8u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[19] == 8u && memcmp(&out[20], ff8, 8) == 0,
              "T-SPI-JOY-EDGE 9000 0xFF fill (transport-default)");
        /* 0x8FF8 want16 crosses 0x9000 -> 0xFF fill. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0xF8u; req[12] = 0x8Fu;
        req[15] = 16u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[19] == 16u &&
              memcmp(&out[20], ff16, 16) == 0,
              "T-SPI-JOY-EDGE 8FF8 want16 0xFF fill");
        /* want==0 rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x60u;
        req[15] = 0u;
        CHECK(usb_build_21_reply(req, 16, out, 64, &ctx) == 0,
              "T-SPI-JOY-EDGE want0 rejected");
        /* want>44 rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x60u;
        req[15] = 45u;
        CHECK(usb_build_21_reply(req, 16, out, 64, &ctx) == 0,
              "T-SPI-JOY-EDGE want45 rejected");
        /* short frame (<16B) rejected. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x60u;
        req[15] = 12u;
        CHECK(usb_build_21_reply(req, 15, out, 64, &ctx) == 0,
              "T-SPI-JOY-EDGE short-frame rejected");
        /* 0x6050 want13 -> exact spi_color_6050 (beats blank). */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x50u; req[12] = 0x60u;
        req[15] = 13u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[15] == 0x50u && out[16] == 0x60u && out[19] == 13u &&
              memcmp(&out[20], spi_color_6050, 13) == 0,
              "T-SPI-JOY-EDGE 6050 exact beats blank");
        /* 0x601B want4 -> out[20]==0x01 rest zero. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x1Bu; req[12] = 0x60u;
        req[15] = 4u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[20] == 0x01u &&
              memcmp(&out[21], zeros3, 3) == 0,
              "T-SPI-JOY-EDGE 601B want4 0x01 rest zero");
    }

    printf("[T-SPI-JOY-WANT3334] Joy SPI want33..44 echo+blank32+pad (legacy-compat)\n");
    {
        /* Blocker 1: legacy-compatible meaning — echo field keeps want
         * (old USB impl: out[19]=want), data region = blank32 + zero pad
         * to want, report len 64. Guarded buffer proves no overrun even
         * when want44 fills out[20..63] exactly. Change A: pinned at
         * non-table 0x7000 (0x6000 is serial-none 0xFF, pinned in
         * T-SPI-JOY-HAPPY/EDGE/BOUND). */
        static const uint8_t zeros44[44] = {0};
        const uint8_t wants[2] = { 33u, 44u };
        for (int k = 0; k < 2; k++) {
            usb_sub_ctx_t ctx;
            uint8_t req[16];
            uint8_t g[66];
            char msg[96];
            int n;
            memset(&ctx, 0, sizeof(ctx));
            ctx.role = 1u;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x10u;
            req[11] = 0x00u; req[12] = 0x70u;
            req[15] = wants[k];
            g[64] = 0xA5u; g[65] = 0x5Au;
            n = usb_build_21_reply(req, 16, g, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-WANT3334 7000 want%u n==64",
                     (unsigned)wants[k]);
            CHECK(n == 64, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-WANT3334 7000 want%u ack 90 10",
                     (unsigned)wants[k]);
            CHECK(n == 64 && g[13] == 0x90u && g[14] == 0x10u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-WANT3334 7000 want%u echo==want",
                     (unsigned)wants[k]);
            CHECK(n == 64 && g[19] == wants[k], msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-WANT3334 7000 want%u data zeros",
                     (unsigned)wants[k]);
            CHECK(n == 64 && memcmp(&g[20], zeros44, wants[k]) == 0, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-WANT3334 7000 want%u canary intact",
                     (unsigned)wants[k]);
            CHECK(g[64] == 0xA5u && g[65] == 0x5Au, msg);
        }
    }

    printf("[T-SPI-JOY-OOR-FF] joy_spi_out_of_range_returns_usb_default_ff_response\n");
    {
        /* Blocker 3: out-of-range (below 0x6000, at/above 0x9000, or
         * straddling 0x9000) is USB transport-default 0xFF fill + ACK —
         * never NO-REPLY (USB must not stall the host; BT keeps no-reply
         * per transport). Full bytes: Report ID / ACK / subcommand /
         * echoed addr+want / 0xFF data / tail zeros / total 64. */
        static const struct { uint8_t lo; uint8_t hi; uint8_t want; } vec[4] = {
            { 0xFFu, 0x5Fu, 1u },
            { 0xFFu, 0x5Fu, 2u },
            { 0xFFu, 0x8Fu, 2u },
            { 0x00u, 0x90u, 1u },
        };
        static const uint8_t ffs[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        for (int k = 0; k < 4; k++) {
            usb_sub_ctx_t ctx;
            uint8_t req[16];
            uint8_t g[66];
            char msg[96];
            uint16_t addr;
            int n, t;
            bool tailz = true;
            memset(&ctx, 0, sizeof(ctx));
            ctx.role = 1u;
            memset(req, 0, sizeof(req));
            req[0] = 0x01u; req[10] = 0x10u;
            req[11] = vec[k].lo; req[12] = vec[k].hi;
            req[15] = vec[k].want;
            addr = (uint16_t)vec[k].lo | ((uint16_t)vec[k].hi << 8);
            g[64] = 0xA5u; g[65] = 0x5Au;
            n = usb_build_21_reply(req, 16, g, 64, &ctx);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-OOR-FF 0x%04X w%u n==64",
                     (unsigned)addr, (unsigned)vec[k].want);
            CHECK(n == 64, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-OOR-FF 0x%04X report 21 ack 90 10",
                     (unsigned)addr);
            CHECK(n == 64 && g[0] == 0x21u && g[13] == 0x90u &&
                  g[14] == 0x10u, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-OOR-FF 0x%04X echo addr+want",
                     (unsigned)addr);
            CHECK(n == 64 && g[15] == vec[k].lo && g[16] == vec[k].hi &&
                  g[17] == 0x00u && g[18] == 0x00u &&
                  g[19] == vec[k].want, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-OOR-FF 0x%04X 0xFF data",
                     (unsigned)addr);
            CHECK(n == 64 && memcmp(&g[20], ffs, vec[k].want) == 0, msg);
            for (t = 20 + vec[k].want; t < 64; t++) {
                if (g[t] != 0x00u) {
                    tailz = false;
                }
            }
            snprintf(msg, sizeof(msg), "T-SPI-JOY-OOR-FF 0x%04X tail zeros",
                     (unsigned)addr);
            CHECK(n == 64 && tailz, msg);
            snprintf(msg, sizeof(msg), "T-SPI-JOY-OOR-FF 0x%04X canary intact",
                     (unsigned)addr);
            CHECK(g[64] == 0xA5u && g[65] == 0x5Au, msg);
        }
    }

    printf("[T-SPI-JOY-BOUND] Joy SPI window boundaries + exact-size variants\n");
    {
        /* Blocker 5: 8FD4+44==0x9000 is in-range blank; 8FD5+44 straddles
         * to 0x9001 -> FF; 6050 want5 keeps first-5 color bytes only;
         * 6050 want14 is 13B color + 1 zero; 6000 want32 is blank32
         * (spi_joy_blank capacity edge). Guarded buffers throughout. */
        static const uint8_t zeros44b[44] = {0};
        static const uint8_t ffs44[44] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
        };
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        uint8_t g[66];
        int n;
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 1u;
        /* 8FD4 want44 -> in-range blank zeros44. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0xD4u; req[12] = 0x8Fu;
        req[15] = 44u;
        g[64] = 0xA5u; g[65] = 0x5Au;
        n = usb_build_21_reply(req, 16, g, 64, &ctx);
        CHECK(n == 64 && g[13] == 0x90u && g[19] == 44u &&
              memcmp(&g[20], zeros44b, 44) == 0 &&
              g[64] == 0xA5u && g[65] == 0x5Au,
              "T-SPI-JOY-BOUND 8FD4 want44 in-range blank");
        /* 8FD5 want44 -> straddle -> FF44. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0xD5u; req[12] = 0x8Fu;
        req[15] = 44u;
        g[64] = 0xA5u; g[65] = 0x5Au;
        n = usb_build_21_reply(req, 16, g, 64, &ctx);
        CHECK(n == 64 && g[13] == 0x90u && g[19] == 44u &&
              memcmp(&g[20], ffs44, 44) == 0 &&
              g[64] == 0xA5u && g[65] == 0x5Au,
              "T-SPI-JOY-BOUND 8FD5 want44 straddle FF");
        /* 6050 want5 -> first-5 color bytes only. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x50u; req[12] = 0x60u;
        req[15] = 5u;
        g[64] = 0xA5u; g[65] = 0x5Au;
        n = usb_build_21_reply(req, 16, g, 64, &ctx);
        CHECK(n == 64 && g[13] == 0x90u && g[19] == 5u &&
              memcmp(&g[20], spi_color_6050, 5) == 0 &&
              g[64] == 0xA5u && g[65] == 0x5Au,
              "T-SPI-JOY-BOUND 6050 want5 first-5 color");
        /* 6050 want14 -> 13B color + 1 zero. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x50u; req[12] = 0x60u;
        req[15] = 14u;
        g[64] = 0xA5u; g[65] = 0x5Au;
        n = usb_build_21_reply(req, 16, g, 64, &ctx);
        CHECK(n == 64 && g[13] == 0x90u && g[19] == 14u &&
              memcmp(&g[20], spi_color_6050, 13) == 0 && g[33] == 0x00u &&
              g[64] == 0xA5u && g[65] == 0x5Au,
              "T-SPI-JOY-BOUND 6050 want14 13B+1zero");
        /* 6000 want32 -> 0xFF serial-none x32 (Change A; 0x6000 is always
         * 0xFF for both roles, never blank). */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x60u;
        req[15] = 32u;
        g[64] = 0xA5u; g[65] = 0x5Au;
        n = usb_build_21_reply(req, 16, g, 64, &ctx);
        CHECK(n == 64 && g[13] == 0x90u && g[19] == 32u &&
              memcmp(&g[20], ffs44, 32) == 0 &&
              g[64] == 0xA5u && g[65] == 0x5Au,
              "T-SPI-JOY-BOUND 6000 want32 serial-none FF");
    }

    printf("[T-SPI-JOY-TABLE] Joy SPI known cal addrs serve table bytes (RED)\n");
    {
        /* Change A: Switch wired init strictly reads 6020/6080/603D/6086;
         * zeros likely fail validation (ProCon with table values passes).
         * New Joy USB policy serves spi_find table bytes for known cal
         * addrs (BT Joy keeps full-blank: USB-transport divergence,
         * documented at build_spi_response_joy). RED: 6020/603D currently
         * serve the virtual-cal shadow and 6080 serves blank zeros, so the
         * ==table checks FAIL until the fix lands. */
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        uint8_t out[64];
        int n;
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 1u;
        /* 0x6020 want24 == SPI_6020_CAL table bytes. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x20u; req[12] = 0x60u;
        req[15] = 24u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[19] == 24u && memcmp(&out[20], SPI_6020_CAL, 24) == 0,
              "T-SPI-JOY-TABLE 6020 want24 table bytes");
        /* 0x603D want18 == SPI_603D first 18 bytes. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x3Du; req[12] = 0x60u;
        req[15] = 18u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[19] == 18u && memcmp(&out[20], SPI_603D, 18) == 0,
              "T-SPI-JOY-TABLE 603D want18 table bytes");
        /* 0x6080 want6 == SPI_6080 first 6 bytes. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x80u; req[12] = 0x60u;
        req[15] = 6u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[19] == 6u && memcmp(&out[20], SPI_6080, 6) == 0,
              "T-SPI-JOY-TABLE 6080 want6 table bytes");
    }

    printf("[T-SPI-PROCON-GOLDEN] ProCon SPI pinned (must PASS now)\n");
    {
        /* Golden: role0 path byte-identical today. Locks the ProCon
         * behavior so the Joy RED above cannot regress role0. */
        usb_sub_ctx_t ctx;
        uint8_t req[16];
        uint8_t out[64];
        int n;
        static const uint8_t ff16g[16] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                          0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        /* Blocker 2: Golden compares the entire 64B report (not just
         * fields). Expected is rebuilt from the shared 12B controller
         * builder + explicit framing/SPI bytes, so any stray byte — data,
         * tail fill, or header — fails the pin. */
        /* role0 0x6010 want16 -> exact SPI_6010 + ACK 0x90/0x10. */
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 0u;
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x10u; req[12] = 0x60u;
        req[15] = 16u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[14] == 0x10u &&
              out[15] == 0x10u && out[16] == 0x60u && out[19] == 16u &&
              memcmp(&out[20], SPI_6010, 16) == 0,
              "T-SPI-PROCON-GOLDEN role0 6010 exact SPI_6010");
        {
            uint8_t exp[64];
            memset(exp, 0, sizeof(exp));
            exp[0] = 0x21u;
            usb_pack_controller_data(&exp[1], &ctx);
            exp[13] = 0x90u; exp[14] = 0x10u;
            exp[15] = 0x10u; exp[16] = 0x60u;
            exp[17] = 0x00u; exp[18] = 0x00u; exp[19] = 16u;
            memcpy(&exp[20], SPI_6010, 16);
            CHECK(n == 64 && memcmp(out, exp, 64) == 0,
                  "T-SPI-PROCON-GOLDEN role0 6010 full 64B");
        }
        /* role0 0x6000 want16 -> 0xFF fill. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x60u;
        req[15] = 16u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x90u && out[19] == 16u &&
              memcmp(&out[20], ff16g, 16) == 0,
              "T-SPI-PROCON-GOLDEN role0 6000 0xFF fill");
        {
            uint8_t exp[64];
            memset(exp, 0, sizeof(exp));
            exp[0] = 0x21u;
            usb_pack_controller_data(&exp[1], &ctx);
            exp[13] = 0x90u; exp[14] = 0x10u;
            exp[15] = 0x00u; exp[16] = 0x60u;
            exp[17] = 0x00u; exp[18] = 0x00u; exp[19] = 16u;
            memset(&exp[20], 0xFF, 16);
            CHECK(n == 64 && memcmp(out, exp, 64) == 0,
                  "T-SPI-PROCON-GOLDEN role0 6000 full 64B");
        }
        /* role0 0x7000 want8 -> miss, return 0. Miss leaves framing but
         * zero data: the full-64B pin locks that no data byte leaks. */
        memset(req, 0, sizeof(req));
        req[0] = 0x01u; req[10] = 0x10u;
        req[11] = 0x00u; req[12] = 0x70u;
        req[15] = 8u;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 0,
              "T-SPI-PROCON-GOLDEN role0 7000 miss returns 0");
        {
            uint8_t exp[64];
            memset(exp, 0, sizeof(exp));
            exp[0] = 0x21u;
            usb_pack_controller_data(&exp[1], &ctx);
            exp[13] = 0x90u; exp[14] = 0x10u;
            exp[15] = 0x00u; exp[16] = 0x70u;
            exp[17] = 0x00u; exp[18] = 0x00u; exp[19] = 8u;
            CHECK(n == 0 && memcmp(out, exp, 64) == 0,
                  "T-SPI-PROCON-GOLDEN role0 7000 full 64B miss shape");
        }
        /* role-latch: set/get + byte-identical devtype + OOB clamp. */
        usb_set_role(1u);
        CHECK(usb_get_role() == 1u,
              "T-SPI-PROCON-GOLDEN latch set(1)->get==1");
        {
            usb_sub_ctx_t c2;
            uint8_t r2[12];
            uint8_t o1[64], o2[64];
            int m1, m2;
            memset(&c2, 0, sizeof(c2));
            c2.role = 1u;
            memset(r2, 0, sizeof(r2));
            r2[0] = 0x01u; r2[10] = 0x02u;
            m1 = usb_build_21_reply(r2, 11, o1, 64, &c2);
            m2 = usb_build_21_reply(r2, 11, o2, 64, &c2);
            CHECK(m1 == 64 && m2 == 64 && memcmp(o1, o2, 64) == 0 &&
                  o1[17] == 0x01u,
                  "T-SPI-PROCON-GOLDEN role1 devtype byte-identical");
        }
        usb_set_role(9u);
        CHECK(usb_get_role() == 0u,
              "T-SPI-PROCON-GOLDEN latch set(9)->0");
        usb_set_role(0u);
    }

    printf("[T-PID-FIXED] USB enumeration fixed to ProCon regardless of role (Plan A RED)\n");
    {
        /* Plan A (separate PID wave): USB must enumerate as 057E:2009
         * Pro Controller regardless of EMUL role; role expression stays
         * in responses (dev_type/btn/stick/battery/SPI/kick81).
         * RED: current tables return 2006/2007 + Joy-Con strings. */
        CHECK(usb_pid_for_role(0) == 0x2009u, "T-PID-FIXED role0 pid 2009");
        CHECK(usb_pid_for_role(1) == 0x2009u, "T-PID-FIXED role1 pid 2009");
        CHECK(usb_pid_for_role(2) == 0x2009u, "T-PID-FIXED role2 pid 2009");
        CHECK(usb_pid_for_role(9u) == 0x2009u, "T-PID-FIXED oob pid 2009");
        CHECK(strcmp(usb_product_for_role(0), "Pro Controller") == 0,
              "T-PID-FIXED role0 product Pro Controller");
        CHECK(strcmp(usb_product_for_role(1), "Pro Controller") == 0,
              "T-PID-FIXED role1 product Pro Controller");
        CHECK(strcmp(usb_product_for_role(2), "Pro Controller") == 0,
              "T-PID-FIXED role2 product Pro Controller");
        CHECK(strcmp(usb_product_for_role(9u), "Pro Controller") == 0,
              "T-PID-FIXED oob product Pro Controller");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
