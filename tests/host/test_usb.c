// test_usb.c -- switch-bcon v3 USB/pack host tests (no Pico SDK needed).
// Covers: u32->3B pack (spec section 11), stick 12bit pack,
// 81/21/30 builders (ported from wakecon, response bytes identical).
// Build: via tests/host/CMakeLists.txt (ctest name: usb).
#include <stdio.h>
#include <string.h>
#include "../../src/proto/protocol.h"
#include "../../src/proto/pack.h"
#include "../../src/usb/usb_hid.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static void pack_btn(uint32_t b, uint8_t o[3]) {
    ctrl_state_t st;
    memset(&st, 0, sizeof(st));
    st.buttons = b;
    st.lx = st.ly = st.rx = st.ry = 0x800;
    ctrl_pack_btn3(&st, o);
}

int main(void) {
    uint8_t o[3];

    printf("[0] u32 single buttons -> Nintendo 3B (spec section 11)\n");
    pack_btn(BTN_B, o);
    CHECK(o[0] == 0x04 && o[1] == 0 && o[2] == 0, "B -> B0.b2");
    pack_btn(BTN_A, o);
    CHECK(o[0] == 0x08 && o[1] == 0 && o[2] == 0, "A -> B0.b3");
    pack_btn(BTN_Y, o);
    CHECK(o[0] == 0x01, "Y -> B0.b0");
    pack_btn(BTN_X, o);
    CHECK(o[0] == 0x02, "X -> B0.b1");
    pack_btn(BTN_R, o);
    CHECK(o[0] == 0x40, "R -> B0.b6");
    pack_btn(BTN_ZR, o);
    CHECK(o[0] == 0x80, "ZR -> B0.b7");
    pack_btn(BTN_PLUS, o);
    CHECK(o[1] == 0x02, "Plus -> B1.b1");
    pack_btn(BTN_RSTICK, o);
    CHECK(o[1] == 0x04, "R-click -> B1.b2");
    pack_btn(BTN_LSTICK, o);
    CHECK(o[1] == 0x08, "L-click -> B1.b3");
    pack_btn(BTN_HOME, o);
    CHECK(o[1] == 0x10, "Home -> B1.b4");
    pack_btn(BTN_CAPTURE, o);
    CHECK(o[1] == 0x20, "Capture -> B1.b5");
    pack_btn(BTN_MINUS, o);
    CHECK(o[1] == 0x01, "Minus -> B1.b0");
    pack_btn(BTN_DOWN, o);
    CHECK(o[2] == 0x01, "Down -> B2.b0");
    pack_btn(BTN_UP, o);
    CHECK(o[2] == 0x02, "Up -> B2.b1");
    pack_btn(BTN_RIGHT, o);
    CHECK(o[2] == 0x04, "Right -> B2.b2");
    pack_btn(BTN_LEFT, o);
    CHECK(o[2] == 0x08, "Left -> B2.b3");
    pack_btn(BTN_L, o);
    CHECK(o[2] == 0x40, "L -> B2.b6");
    pack_btn(BTN_ZL, o);
    CHECK(o[2] == 0x80, "ZL -> B2.b7");

    printf("[1] dpad diagonals set 2 bits at once (no HAT field)\n");
    pack_btn(BTN_UP | BTN_RIGHT, o);
    CHECK(o[2] == (0x02 | 0x04), "Up+Right");
    pack_btn(BTN_DOWN | BTN_LEFT, o);
    CHECK(o[2] == (0x01 | 0x08), "Down+Left");

    printf("[2] Switch2-only + reserved bits are dropped on Switch1 transports\n");
    pack_btn(BTN_GR | BTN_GL | BTN_C | BTN_HEADSET | BTN_RESERVED_MASK, o);
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0, "GR/GL/C/headset/reserved dropped");
    pack_btn(0xFFFFFFFFu, o);
    CHECK(o[0] == 0xCF && o[1] == 0x3F && o[2] == 0xCF, "all-bits packed shape");

    printf("[3] stick 12bit pack (legacy u8<<4 inputs)\n");
    {
        uint8_t s[3];
        pack_stick_12bit(0x800, 0x800, s);
        CHECK(s[0] == 0x00 && s[1] == 0x08 && s[2] == 0x80, "center -> 0x800/0x800");
        pack_stick_12bit(0x000, 0x000, s);
        CHECK(s[0] == 0x00 && s[1] == 0xF0 && s[2] == 0xFF, "min -> x=0/y=4095");
        pack_stick_12bit(0xFF0, 0xFF0, s);
        CHECK(s[0] == 0xF0 && s[1] == 0x0F && s[2] == 0x01, "max -> x=0xFF0/y=0x010");
    }

    printf("[3b] stick 12bit direct (STATE LEN=12 path)\n");
    {
        uint8_t s[3];
        pack_stick_12bit(0x800, 0x800, s);
        CHECK(s[0] == 0x00 && s[1] == 0x08 && s[2] == 0x80, "12bit center -> 0x800/0x800");
        pack_stick_12bit(0xABC, 0x123, s);
        CHECK(s[0] == 0xBC && s[1] == 0xDA && s[2] == 0xED, "12bit mid -> BC DA ED");
        pack_stick_12bit(0xFFF, 0xFFF, s);
        CHECK(s[0] == 0xFF && s[1] == 0x1F && s[2] == 0x00, "12bit max -> FF 1F 00");
    }

    printf("[4] 81 handshake replies are 64B zero-padded\n");
    {
        uint8_t out[64];
        const uint8_t mac[6] = { 0x7C, 0xBB, 0x8A, 0x01, 0x02, 0x03 };
        const uint8_t req01[] = { 0x80, 0x01 };
        int n = usb_build_81_reply(req01, 2, out, 64, mac, 0x03);
        CHECK(n == 64 && out[0] == 0x81 && out[1] == 0x01 && out[2] == 0x00 &&
              out[3] == 0x03 && memcmp(&out[4], mac, 6) == 0 && out[63] == 0,
              "81 01 + MAC + 64B");
        const uint8_t req04[] = { 0x80, 0x04 };
        n = usb_build_81_reply(req04, 2, out, 64, mac, 0x03);
        CHECK(n == 64 && out[0] == 0x81 && out[1] == 0x04, "81 04 ack");
        const uint8_t bad[] = { 0x00, 0x04 };
        CHECK(usb_build_81_reply(bad, 2, out, 64, mac, 0x03) == 0, "non-80 rejected");
        CHECK(usb_build_81_reply(req01, 2, out, 63, mac, 0x03) == 0, "short buf rejected");
    }

    printf("[5] 21 sub replies (device info + SPI)\n");
    {
        usb_sub_ctx_t ctx;
        uint8_t out[64];
        uint8_t req[16];
        memset(&ctx, 0, sizeof(ctx));
        ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800;
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x02;
        memcpy(ctx.mac, (const uint8_t[]){1, 2, 3, 4, 5, 6}, 6);
        int n = usb_build_21_reply(req, 11, out, 64, &ctx);
        CHECK(n == 64 && out[0] == 0x21 && out[13] == 0x82 && out[14] == 0x02 &&
              out[15] == 0x03 && out[16] == 0x48 && out[26] == 0x02 &&
              memcmp(&out[19], ctx.mac, 6) == 0, "02 device info (fw 03 48, tail 02)");
        // SPI color read 0x6050 x12.
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10; req[11] = 0x50; req[12] = 0x60; req[15] = 12;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x82 && out[21] == 0x82 &&
               out[26] == 0x46 && out[27] == 0x46 && out[28] == 0x46 &&
               out[29] == 0xFF && out[30] == 0xFF && out[31] == 0xFF,
               "10 SPI color 6050 (L/R differ)");
        // SPI serial area 0x6000 answers 0xFF (2162-0002 avoidance).        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10; req[11] = 0x00; req[12] = 0x60; req[15] = 16;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        {
            int allff = 1;
            for (int k = 0; k < 16; k++) allff &= (out[20 + k] == 0xFF);
            CHECK(n == 64 && allff, "10 SPI serial 6000 -> 0xFF");
        }
        // SPI 0x601B answers 0x01 (color info exists -> console uses 6050).
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10; req[11] = 0x1B; req[12] = 0x60; req[15] = 1;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[20] == 0x01, "10 SPI 601B -> 0x01 (use 6050 colors)");
        // Bulk 0x6010 read carries the same 0x01 at offset 0x0B.
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x10; req[11] = 0x10; req[12] = 0x60; req[15] = 16;
        n = usb_build_21_reply(req, 16, out, 64, &ctx);
        CHECK(n == 64 && out[20 + 11] == 0x01, "10 SPI 6010 bulk keeps 601B=0x01");
        // Unknown sub gets 80 sub ack (host never stalls).
        memset(req, 0, sizeof(req));
        req[0] = 0x01; req[10] = 0x48;
        n = usb_build_21_reply(req, 11, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0x80 && out[14] == 0x48, "48 ack");
    }

    printf("[6] 30 input report layout (2wiCC ControllerData compatible)\n");
    {
        usb_sub_ctx_t ctx;
        uint8_t out[64];
        memset(&ctx, 0, sizeof(ctx));
        ctx.btn[0] = 0x08; ctx.btn[1] = 0x02; ctx.btn[2] = 0x40 | 0x10;
        ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800;
        ctx.timer = 0x5A;
        int n = usb_build_30_report(&ctx, out);
        CHECK(n == 64 && out[0] == 0x30 && out[1] == 0x5A && out[2] == 0x91 &&
              out[3] == 0x08 && out[4] == (0x02 | 0x80) &&
              out[5] == ((0x40 | 0x10) & 0xCF) && out[12] == 0x09 &&
              out[63] == 0, "30 report masks (|0x80, &0xCF, 0x91/0x09)");
    }

    printf("[7] end-to-end: STATE(A) -> parse -> pack -> 30 report bit\n");
    {
        uint8_t frame[64], payload[8];
        link_stats_t st;
        parser_t p;
        payload[0] = (uint8_t)(BTN_A & 0xFFu);
        payload[1] = (uint8_t)((BTN_A >> 8) & 0xFFu);
        payload[2] = payload[3] = 0;
        payload[4] = payload[5] = payload[6] = payload[7] = 0x80;
        size_t n = frame_build(frame, T_STATE, payload, 8, 0x77);
        (void)n;
        // Minimal inline parser use: feed and check via pack of known u32.
        memset(&st, 0, sizeof(st));
        parser_init(&p, NULL, NULL, &st);
        parser_feed_buf(&p, frame, n);
        CHECK(st.have_seq && st.last_seq == 0x77 && st.err_crc == 0, "STATE parsed");
        {
            ctrl_state_t cst;
            uint8_t b3[3];
            usb_sub_ctx_t ctx;
            uint8_t rep[64];
            cst.buttons = BTN_A;
            cst.lx = cst.ly = cst.rx = cst.ry = 0x800;
            ctrl_pack_btn3(&cst, b3);
            memset(&ctx, 0, sizeof(ctx));
            ctx.btn[0] = b3[0]; ctx.btn[1] = b3[1]; ctx.btn[2] = b3[2];
            ctx.lx = ctx.ly = ctx.rx = ctx.ry = 0x800;
            usb_build_30_report(&ctx, rep);
            CHECK((rep[3] & 0x08) != 0, "A reaches USB byte0 bit3");
        }
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
