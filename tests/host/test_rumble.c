// test_rumble.c -- Switch HID-output 0x10 rumble decoder host tests (T3/T4).
// No Pico SDK needed. CTest name: rumble.
// Vectors: log/COM3_2026_09_18.rumble_vib.txt full-tail distinct set
// (re-extracted desk-side: 3 neutral variants + mid/strong episodes).
#include <stdbool.h>
#include <stdio.h>
#include "../../src/proto/rumble.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    uint8_t l = 0xFFu, r = 0xFFu;

    printf("[0] V0 sym neutral -> (0,0)\n");
    {
        const uint8_t v0[8] = { 0x00,0x01,0x40,0x40, 0x00,0x01,0x40,0x40 };
        rumble_decode8(v0, &l, &r);
        CHECK(l == 0 && r == 0, "V0 neutral (0,0)");
    }

    printf("[1] neutral L/R asymmetric variants -> (0,0)\n");
    {
        const uint8_t nl[8] = { 0x00,0x01,0x40,0x40, 0x00,0x00,0x00,0x00 };
        const uint8_t nr[8] = { 0x00,0x00,0x00,0x00, 0x00,0x01,0x40,0x40 };
        rumble_decode8(nl, &l, &r);
        CHECK(l == 0 && r == 0, "neutral L (0,0)");
        rumble_decode8(nr, &l, &r);
        CHECK(l == 0 && r == 0, "neutral R (0,0)");
    }

    printf("[2] NULL input -> (0,0)\n");
    {
        l = 0xFFu; r = 0xFFu;
        rumble_decode8(NULL, &l, &r);
        CHECK(l == 0 && r == 0, "NULL raw8 (0,0)");
    }

    printf("[3] mid vectors: L/R/both -> 87\n");
    {
        const uint8_t lm[8] = { 0x00,0x45,0x40,0x52, 0x00,0x00,0x00,0x00 };
        const uint8_t rm[8] = { 0x00,0x00,0x00,0x00, 0x00,0x45,0x40,0x52 };
        const uint8_t bm[8] = { 0x00,0x45,0x40,0x52, 0x00,0x45,0x40,0x52 };
        rumble_decode8(lm, &l, &r);
        CHECK(l == 87 && r == 0, "L-mid (87,0)");
        rumble_decode8(rm, &l, &r);
        CHECK(l == 0 && r == 87, "R-mid (0,87)");
        rumble_decode8(bm, &l, &r);
        CHECK(l == 87 && r == 87, "both-mid (87,87)");
    }

    printf("[4] both-strong (LF-driven peak) -> (255,255)\n");
    {
        const uint8_t st[8] = { 0x80,0x00,0x60,0x92, 0x80,0x00,0x60,0x92 };
        rumble_decode8(st, &l, &r);
        CHECK(l == 255 && r == 255, "both-strong (255,255)");
    }

    printf("[5] 010-entry: counter skip + size>=9 guard\n");
    {
        const uint8_t rep[9] = { 0x0D, 0x00,0x45,0x40,0x52, 0x00,0x45,0x40,0x52 };
        bool ok = rumble_decode_010(rep, 9, &l, &r);
        CHECK(ok && l == 87 && r == 87, "counter skipped (87,87)");
        l = 0xAAu; r = 0xBBu;
        ok = rumble_decode_010(rep, 8, &l, &r);
        CHECK(!ok && l == 0 && r == 0, "short report rejected (0,0)");
        ok = rumble_decode_010(NULL, 9, &l, &r);
        CHECK(!ok, "NULL report rejected");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
