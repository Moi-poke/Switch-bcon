// test_protocol.c -- switch-bcon v3 host tests (no Pico SDK needed).
// Build: gcc -Wall -Wextra -O2 -o test_protocol test_protocol.c ../../src/proto/protocol.c
#include <stdio.h>
#include <string.h>
#include "../../src/proto/protocol.h"

static int hits = 0, fails = 0;
static uint8_t lt, ls, ll, lp[64];
static void cb(uint8_t t, const uint8_t *p, uint8_t len, uint8_t seq, void *u) {
    (void)u;
    hits++; lt = t; ls = seq; ll = len;
    if (len <= sizeof(lp)) memcpy(lp, p, len);
}
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static void put_btn32(uint8_t *pl, uint32_t b) {
    pl[0] = (uint8_t)(b & 0xFFu);
    pl[1] = (uint8_t)((b >> 8) & 0xFFu);
    pl[2] = (uint8_t)((b >> 16) & 0xFFu);
    pl[3] = (uint8_t)((b >> 24) & 0xFFu);
}

int main(void) {
    printf("[0] CRC check\n");
    CHECK(crc8((const uint8_t *)"123456789", 9) == 0xF4, "crc8==0xF4");

    link_stats_t st;
    parser_t p;
    memset(&st, 0, sizeof(st));
    parser_init(&p, cb, NULL, &st);

    printf("[1] Valid STATE v3 (A pressed, sticks center, seq 0x2A)\n");
    uint8_t s1[64]; size_t n1;
    { uint8_t pl[8]; put_btn32(pl, BTN_A); pl[4]=pl[5]=pl[6]=pl[7]=0x80;
      n1 = frame_build(s1, T_STATE, pl, 8, 0x2A); }
    CHECK(n1 == 13, "STATE v3 total 13B");
    hits = 0; parser_feed_buf(&p, s1, n1);
    { uint32_t b = (uint32_t)lp[0] | ((uint32_t)lp[1] << 8) |
                   ((uint32_t)lp[2] << 16) | ((uint32_t)lp[3] << 24);
      CHECK(hits == 1 && lt == T_STATE && ls == 0x2A && (b & BTN_A), "parsed STATE v3"); }

    printf("[2] Strict LEN: STATE with LEN=7 must be rejected (v3 needs 8)\n");
    { uint8_t bad[12] = {0xAB, 0x01, 0x07, 0,0,0,0,0,0,0,0,0};
      bad[11] = crc8(&bad[1], 10);
      hits = 0; memset(&st, 0, sizeof(st)); parser_init(&p, cb, NULL, &st);
      parser_feed_buf(&p, bad, sizeof(bad));
      CHECK(hits == 0 && st.err_crc >= 1 && st.errcode == ERR_BAD_LEN, "bad-LEN dropped"); }

    printf("[3] Sliding resync after bad CRC\n");
    { uint8_t badf[64]; size_t nb;
      uint8_t pl[8]; put_btn32(pl, BTN_B); pl[4]=pl[5]=pl[6]=pl[7]=0x80;
      nb = frame_build(badf, T_STATE, pl, 8, 0x30); badf[nb-1] ^= 0xFF;
      uint8_t nz[64]; size_t nn = frame_build(nz, T_NEUTRAL, NULL, 0, 0x31);
      hits = 0; memset(&st, 0, sizeof(st)); parser_init(&p, cb, NULL, &st);
      parser_feed_buf(&p, badf, nb);
      parser_feed_buf(&p, nz, nn);
      CHECK(hits == 1 && lt == T_NEUTRAL && ls == 0x31, "recovered after bad CRC"); }

    printf("[4] Stray 0xAB inside payload\n");
    { uint8_t s5[64]; size_t n5;
      uint8_t pl[8]; put_btn32(pl, 0); pl[4] = 0xAB; pl[5]=pl[6]=pl[7]=0x80;
      n5 = frame_build(s5, T_STATE, pl, 8, 0x40);
      uint8_t pg[64]; size_t ng = frame_build(pg, T_PING, NULL, 0, 0x41);
      hits = 0; memset(&st, 0, sizeof(st)); parser_init(&p, cb, NULL, &st);
      parser_feed_buf(&p, s5, n5);
      parser_feed_buf(&p, pg, ng);
      CHECK(hits == 2, "STATE(with 0xAB) + PING both parsed"); }

    printf("[5] Leading garbage + lone 0xAB\n");
    { uint8_t garbage[] = {0x00, 0xFF, 0x12, 0xAB, 0x99};
      hits = 0; memset(&st, 0, sizeof(st)); parser_init(&p, cb, NULL, &st);
      parser_feed_buf(&p, garbage, sizeof(garbage));
      parser_feed_buf(&p, s1, n1);
      CHECK(hits == 1 && lt == T_STATE, "recovers from noise"); }

    printf("[6] SEQ gap mod-256 (first frame not counted)\n");
    { link_stats_t st2; memset(&st2, 0, sizeof(st2));
      parser_init(&p, cb, NULL, &st2);
      uint8_t a[64], b[64];
      size_t na = frame_build(a, T_NEUTRAL, NULL, 0, 0xFF);
      size_t nb2 = frame_build(b, T_NEUTRAL, NULL, 0, 0x01);
      parser_feed_buf(&p, a, na); parser_feed_buf(&p, b, nb2);
      CHECK(st2.err_drop == 1 && st2.errcode == ERR_SEQ_GAP, "0xFF->0x01 = 1 event"); }

    printf("[7] CONFIG WIRED_MODE accepted, unknown TYPE skipped\n");
    { uint8_t w[64]; uint8_t pay = 1;
      size_t nw = frame_build(w, T_WIRED_MODE, &pay, 1, 0x50);
      uint8_t u[64]; uint8_t upay[2] = {0xAA, 0xBB};
      size_t nu = frame_build(u, 0x41, upay, 2, 0x51);
      hits = 0; memset(&st, 0, sizeof(st)); parser_init(&p, cb, NULL, &st);
      parser_feed_buf(&p, w, nw);
      parser_feed_buf(&p, u, nu);
      CHECK(hits == 2 && st.err_drop == 0, "CONFIG + unknown skip"); }

    printf("[8] frame_build guards len>32\n");
    { uint8_t out[64]; uint8_t big[33] = {0};
      CHECK(frame_build(out, T_STATE, big, 33, 0) == 0, "len>32 rejected"); }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
