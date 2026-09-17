// test_config.c -- v3 dispatch host tests (Steps 1+2: CONFIG/RUMBLE+HELLO/PING).
// No Pico SDK needed. CTest name: config.
#include <stdio.h>
#include <string.h>
#include "../../src/proto/protocol.h"
#include "../../src/proto/dispatch.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    v3_session_t s;

    printf("[0] HELLO v4 -> OK + HELLO_ACK queued\n");
    v3_session_init(&s);
    {
        const uint8_t hello[] = { 0x04, 0x00 };
        v3_live_t r = v3_on_frame(&s, T_HELLO, hello, 2, 0x01);
        CHECK(r == V3_IGNORE && s.result == RESULT_OK && s.state_accept &&
              !s.auto_status && s.ob_n == 1 && s.ob[0].act == ACT_SEND_HELLO_ACK,
              "HELLO v4 OK");
    }

    printf("[1] HELLO newer -> UNSUPPORTED (no downgrade in v4)\n");
    v3_session_init(&s);
    {
        const uint8_t hello[] = { 0x05, 0x01 };
        v3_on_frame(&s, T_HELLO, hello, 2, 0x01);
        CHECK(s.result == RESULT_VER_UNSUPPORTED && !s.state_accept && s.auto_status,
              "HELLO v5 UNSUPPORTED + auto_status");
    }

    printf("[2] HELLO older -> UNSUPPORTED: STATE refused, NEUTRAL passes\n");
    v3_session_init(&s);
    {
        const uint8_t hello[] = { 0x03, 0x00 };
        const uint8_t st[8] = { 0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80 };
        v3_on_frame(&s, T_HELLO, hello, 2, 0x01);
        CHECK(s.result == RESULT_VER_UNSUPPORTED && !s.state_accept,
              "HELLO v3 UNSUPPORTED");
        CHECK(v3_on_frame(&s, T_STATE, st, 8, 0x02) == V3_IGNORE &&
              s.errcode == ERR_UNSUPPORTED, "STATE refused under UNSUPPORTED");
        CHECK(v3_on_frame(&s, T_NEUTRAL, NULL, 0, 0x03) == V3_APPLY_NEUTRAL,
              "NEUTRAL still applies (safe stop)");
    }

    printf("[3] default (no HELLO): STATE accepted (sweep/PoC compat)\n");
    v3_session_init(&s);
    {
        const uint8_t st[8] = { 2, 0, 0, 0, 0x80, 0x80, 0x80, 0x80 };
        CHECK(v3_on_frame(&s, T_STATE, st, 8, 0x10) == V3_APPLY_STATE,
              "STATE accepted without HELLO");
    }

    printf("[4] PING -> PONG echoes header SEQ\n");
    v3_session_init(&s);
    {
        v3_on_frame(&s, T_PING, NULL, 0, 0xAB);
        CHECK(s.ob_n == 1 && s.ob[0].act == ACT_SEND_PONG && s.ob[0].arg == 0xAB,
              "PONG echoes 0xAB");
    }

    printf("[5] CAPTURE_START range 1-60 (else ERRCODE 0x10)\n");
    v3_session_init(&s);
    {
        const uint8_t bad0[] = { 0x00 };
        const uint8_t bad61[] = { 61 };
        const uint8_t ok[] = { 30 };
        CHECK(v3_on_frame(&s, T_CAPTURE_START, bad0, 1, 1) == V3_IGNORE &&
              s.errcode == 0x10 && s.fx == FX_NONE, "sec=0 rejected (0x10)");
        CHECK(v3_on_frame(&s, T_CAPTURE_START, bad61, 1, 2) == V3_IGNORE &&
              s.errcode == 0x10, "sec=61 rejected (0x10)");
        CHECK(v3_on_frame(&s, T_CAPTURE_START, ok, 1, 3) == V3_IGNORE &&
              s.fx == FX_CAPTURE_START && s.fx_arg == 30 && s.cap_seconds == 30,
              "sec=30 accepted");
    }

    printf("[6] BEACON_START needs saved capture (else 0x11)\n");
    v3_session_init(&s);
    {
        CHECK(v3_on_frame(&s, T_BEACON_START, NULL, 0, 1) == V3_IGNORE &&
              s.errcode == 0x11 && s.fx == FX_NONE, "no-save rejected (0x11)");
        s.cap_valid = true;
        CHECK(v3_on_frame(&s, T_BEACON_START, NULL, 0, 2) == V3_IGNORE &&
              s.fx == FX_BEACON_START, "saved -> FX_BEACON_START");
    }

    printf("[7] COLOR_SET copies 12B payload\n");
    v3_session_init(&s);
    {
        uint8_t col[12];
        for (int k = 0; k < 12; k++) col[k] = (uint8_t)(0x10 + k);
        CHECK(v3_on_frame(&s, T_COLOR_SET, col, 12, 1) == V3_IGNORE &&
              s.fx == FX_COLOR_SET && memcmp(s.color, col, 12) == 0,
              "COLOR_SET accepted + copied");
    }

    printf("[8] KEY_DELETE\n");
    v3_session_init(&s);
    {
        CHECK(v3_on_frame(&s, T_KEY_DELETE, NULL, 0, 1) == V3_IGNORE &&
              s.fx == FX_KEY_DELETE, "KEY_DELETE -> FX");
    }

    printf("[9] WIRED_MODE 0/1 only (else 0x14)\n");
    v3_session_init(&s);
    {
        const uint8_t bad[] = { 0x02 };
        const uint8_t ok[] = { 0x01 };
        CHECK(v3_on_frame(&s, T_WIRED_MODE, bad, 1, 1) == V3_IGNORE &&
              s.errcode == 0x14 && s.fx == FX_NONE, "WIRED=2 rejected (0x14)");
        CHECK(v3_on_frame(&s, T_WIRED_MODE, ok, 1, 2) == V3_IGNORE &&
              s.fx == FX_WIRED_MODE && s.fx_arg == 1 && s.wired_val == 1,
              "WIRED=1 accepted");
    }

    printf("[10] STATUS_REQ queues STATUS + PLAYER_INFO piggyback\n");
    v3_session_init(&s);
    {
        bool has_status = false, has_pi = false;
        CHECK(v3_on_frame(&s, T_STATUS_REQ, NULL, 0, 5) == V3_IGNORE,
              "STATUS_REQ ignored (outbox only)");
        for (uint8_t k = 0; k < s.ob_n; k++) {
            if (s.ob[k].act == ACT_SEND_STATUS) has_status = true;
            if (s.ob[k].act == ACT_SEND_PLAYER_INFO) has_pi = true;
        }
        CHECK(s.ob_n == 2 && has_status && has_pi && s.ob_n <= V3_OB_N,
              "STATUS_REQ -> STATUS + PLAYER_INFO");
    }

    printf("[11] unknown TYPE ignored, no side effects\n");
    v3_session_init(&s);
    {
        const uint8_t p[] = { 0xAA, 0xBB };
        CHECK(v3_on_frame(&s, 0x41, p, 2, 9) == V3_IGNORE &&
              s.ob_n == 0 && s.fx == FX_NONE, "unknown 0x41 skipped");
    }

    printf("[12] outbox overflow counted\n");
    v3_session_init(&s);
    {
        for (int k = 0; k < 10; k++) v3_on_frame(&s, T_PING, NULL, 0, (uint8_t)k);
        CHECK(s.ob_n == V3_OB_N && s.ob_dropped == 2 &&
              s.ob[0].arg == 0 && s.ob[7].arg == 7, "8 kept + 2 dropped");
    }

    printf("[13] STATUS 7B layout (spec section 5.5)\n");
    {
        uint8_t out[7];
        v3_pack_status(0xC3, 0x2A, 0x1234, 0x0007, 0x10, out);
        CHECK(out[0] == 0xC3 && out[1] == 0x2A &&
              out[2] == 0x34 && out[3] == 0x12 &&
              out[4] == 0x07 && out[5] == 0x00 && out[6] == 0x10,
              "flags/seq/LE16/LE16/errcode");
    }

    printf("[14] PLAYER_INFO expected LEN=2, 0x24 unknown\n");
    {
        CHECK(proto_expected_len(T_PLAYER_INFO) == 2, "PLAYER_INFO LEN=2");
        CHECK(proto_expected_len(0x24) == -1, "0x24 still unknown");
    }

    printf("[15] v3_player_tick change-only\n");
    v3_session_init(&s);
    {
        s.player_valid = false;
        v3_player_tick(&s);
        CHECK(s.ob_n == 0, "invalid -> quiet");
        s.player_valid = true;
        s.player_lamp = 0x03;
        s.player_flags = 0x01;
        v3_player_tick(&s);
        CHECK(s.ob_n == 1 && s.ob[0].act == ACT_SEND_PLAYER_INFO,
              "first valid -> queued once");
        {
            uint8_t n = s.ob_n;
            v3_player_tick(&s);
            CHECK(s.ob_n == n, "unchanged -> quiet");
        }
        s.player_lamp = 0x04;
        v3_player_tick(&s);
        CHECK(s.ob_n == 2 && s.ob[1].act == ACT_SEND_PLAYER_INFO,
              "lamp change -> queued");
        s.player_flags = 0x03;
        v3_player_tick(&s);
        CHECK(s.ob_n == 3 && s.ob[2].act == ACT_SEND_PLAYER_INFO,
              "flags change -> queued");
    }

    printf("[16] STATE LEN=12 accepted (u16LE sticks), LEN=7 rejected\n");
    v3_session_init(&s);
    {
        const uint8_t st12[12] = { 2,0,0,0, 0x00,0x08, 0x00,0x08, 0x00,0x08, 0x00,0x08 };
        CHECK(v3_on_frame(&s, T_STATE, st12, 12, 0x20) == V3_APPLY_STATE,
              "STATE LEN=12 accepted");
        const uint8_t st7[7] = { 0,0,0,0, 0x80,0x80,0x80 };
        CHECK(v3_on_frame(&s, T_STATE, st7, 7, 0x21) == V3_IGNORE &&
              s.errcode == ERR_BAD_LEN, "STATE LEN=7 rejected (BAD_LEN)");
        CHECK(proto_state_len_ok(8) && proto_state_len_ok(12),
              "state_len_ok(8/12)");
        CHECK(!proto_state_len_ok(7) && !proto_state_len_ok(13) &&
              !proto_state_len_ok(0), "state_len_ok rejects 7/13/0");
        CHECK(proto_expected_len(T_STATE) == 8, "T_STATE canonical LEN stays 8");
    }

    printf("[17] BAUD_SET (B §3) range 0-4 -> FX + STATUS ACK, else 0x16\n");
    v3_session_init(&s);
    {
        const uint8_t ok0[] = { 0x00 };
        const uint8_t ok4[] = { 0x04 };
        const uint8_t bad5[] = { 0x05 };
        CHECK(proto_expected_len(T_BAUD_SET) == 1, "BAUD_SET LEN=1");
        CHECK(v3_on_frame(&s, T_BAUD_SET, ok0, 1, 1) == V3_IGNORE &&
              s.fx == FX_BAUD_SET && s.fx_arg == 0, "idx0 accepted");
        CHECK(s.ob_n == 1 && s.ob[0].act == ACT_SEND_STATUS,
              "accept queues STATUS ACK (old rate)");
        CHECK(v3_on_frame(&s, T_BAUD_SET, ok4, 1, 2) == V3_IGNORE &&
              s.fx == FX_BAUD_SET && s.fx_arg == 4, "idx4 accepted");
        CHECK(v3_on_frame(&s, T_BAUD_SET, bad5, 1, 3) == V3_IGNORE &&
              s.errcode == 0x16 && s.fx == FX_NONE, "idx5 rejected (0x16)");
        CHECK(v3_on_frame(&s, T_BAUD_SET, ok0, 0, 4) == V3_IGNORE &&
              s.errcode == ERR_BAD_LEN, "LEN=0 rejected (BAD_LEN)");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
