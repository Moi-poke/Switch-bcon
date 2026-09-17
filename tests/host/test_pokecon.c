// test_pokecon.c -- PokeCon Modified compat input host tests (task-17).
// No Pico SDK needed. CTest name: pokecon. Style: test_config.c CHECK macro.
// Spec: docs/superpowers/specs/2026-09-16-pokecon-compat-design.md
//   POKE-HAPPY-01: basic line vectors (§1.2 spec examples).
//   POKE-EDGE-02: hat 0-8 full sweep (§1.4) + 'end' -> NEUTRAL (§1.6).
//   POKE-EDGE-03: RS-alone routing quirk (§1.5) + malformed/overlong/
//                 partial-line drop (prior state held, no overflow).
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "pokecon.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static ctrl_state_t fresh(void) {
    ctrl_state_t s;
    s.buttons = 0u;
    s.lx = s.ly = 0x800u;
    s.rx = s.ry = 0x800u;
    return s;
}

static pokecon_rc_t feed_str(const char *line, ctrl_state_t *st) {
    return pokecon_parse_line(line, strlen(line), st);
}

int main(void) {
    ctrl_state_t s;

    printf("[POKE-HAPPY-01] basic line vectors (spec section 1.2)\n");
    s = fresh();
    CHECK(feed_str("10 08", &s) == POKE_OK && s.buttons == BTN_A &&
          s.lx == 0x800u && s.ly == 0x800u &&
          s.rx == 0x800u && s.ry == 0x800u,
          "10 08 = A press, sticks held");
    s = fresh();
    CHECK(feed_str("2 08 ff 80", &s) == POKE_OK && s.buttons == 0u &&
          s.lx == 0xFF0u && s.ly == 0x800u &&
          s.rx == 0x800u && s.ry == 0x800u,
          "2 08 ff 80 = LS-alone -> LX/LY=lx/ly");
    s = fresh();
    CHECK(feed_str("3 08 ff 80 da da", &s) == POKE_OK && s.buttons == 0u &&
          s.lx == 0xFF0u && s.ly == 0x800u &&
          s.rx == 0xDA0u && s.ry == 0xDA0u,
          "3 08 ff 80 da da = both -> LX/LY+RX/RY");
    s = fresh();
    CHECK(feed_str("0004 08 80 80 80 80", &s) == POKE_OK &&
          s.buttons == BTN_Y &&
          s.lx == 0x800u && s.ly == 0x800u &&
          s.rx == 0x800u && s.ry == 0x800u,
          "0004 08 80 80 80 80 = Y press, no-flag sticks held");
    s = fresh();
    CHECK(feed_str("92 8 80 ff\r\n", &s) == POKE_OK &&
          s.buttons == (BTN_A | BTN_R) &&
          s.lx == 0x800u && s.ly == 0xFF0u,
          "92 8 80 ff = LS+A+R, LS-alone sticks (wiki typo resolved)");
    s = fresh();
    CHECK(feed_str("0010 08\r\n", &s) == POKE_OK && s.buttons == BTN_A,
          "CRLF terminated + zero-padded A");
    s = fresh();
    CHECK(feed_str("0010 08\n", &s) == POKE_OK && s.buttons == BTN_A,
          "LF-only terminated A");

    printf("[POKE-HAPPY-01b] wire bit -> VIIPER full map (spec section 1.3)\n");
    {
        static const struct { unsigned wire; uint32_t viiper; } m[] = {
            { 2, BTN_Y }, { 3, BTN_B }, { 4, BTN_A }, { 5, BTN_X },
            { 6, BTN_L }, { 7, BTN_R }, { 8, BTN_ZL }, { 9, BTN_ZR },
            { 10, BTN_MINUS }, { 11, BTN_PLUS }, { 12, BTN_LSTICK },
            { 13, BTN_RSTICK }, { 14, BTN_HOME }, { 15, BTN_CAPTURE },
        };
        unsigned k;
        char line[32];
        for (k = 0u; k < sizeof(m) / sizeof(m[0]); k++) {
            char msg[64];
            s = fresh();
            snprintf(line, sizeof(line), "%x 08", 1u << m[k].wire);
            snprintf(msg, sizeof(msg), "wire bit%u -> VIIPER 0x%05x",
                     m[k].wire, (unsigned)m[k].viiper);
            CHECK(feed_str(line, &s) == POKE_OK && s.buttons == m[k].viiper,
                  msg);
        }
        s = fresh();
        CHECK(feed_str("fffc 08", &s) == POKE_OK &&
              s.buttons == (BTN_Y | BTN_B | BTN_A | BTN_X | BTN_L | BTN_R |
                            BTN_ZL | BTN_ZR | BTN_MINUS | BTN_PLUS |
                            BTN_LSTICK | BTN_RSTICK | BTN_HOME | BTN_CAPTURE),
              "fffc = all 14 buttons, flags excluded");
    }

    printf("[POKE-EDGE-02] hat 0-8 sweep (spec section 1.4) + end\n");
    {
        static const struct { unsigned hat; uint32_t dpad; } h[] = {
            { 0, BTN_UP }, { 1, BTN_UP | BTN_RIGHT }, { 2, BTN_RIGHT },
            { 3, BTN_DOWN | BTN_RIGHT }, { 4, BTN_DOWN },
            { 5, BTN_DOWN | BTN_LEFT }, { 6, BTN_LEFT },
            { 7, BTN_UP | BTN_LEFT }, { 8, 0u },
        };
        unsigned k;
        for (k = 0u; k < sizeof(h) / sizeof(h[0]); k++) {
            char line[32], msg[48];
            s = fresh();
            snprintf(line, sizeof(line), "0 %u", h[k].hat);
            snprintf(msg, sizeof(msg), "hat %u -> dpad 0x%03x",
                     h[k].hat, (unsigned)h[k].dpad);
            CHECK(feed_str(line, &s) == POKE_OK && s.buttons == h[k].dpad,
                  msg);
        }
        s = fresh();
        CHECK(feed_str("10 09", &s) == POKE_IGNORE && s.buttons == 0u,
              "hat 9 rejected, state held");
    }
    s.buttons = BTN_A | BTN_PLUS;
    s.lx = 0xFF0u; s.ly = 0x100u; s.rx = 0x200u; s.ry = 0x300u;
    CHECK(feed_str("end\r\n", &s) == POKE_END && s.buttons == 0u &&
          s.lx == 0x800u && s.ly == 0x800u &&
          s.rx == 0x800u && s.ry == 0x800u,
          "end -> full NEUTRAL");
    s = fresh();
    CHECK(feed_str("END\n", &s) == POKE_END, "END uppercase accepted");

    printf("[POKE-EDGE-03] RS-alone routing + malformed/overlong/partial drop\n");
    s = fresh();
    CHECK(feed_str("1 08 12 34", &s) == POKE_OK && s.buttons == 0u &&
          s.lx == 0x800u && s.ly == 0x800u &&
          s.rx == 0x120u && s.ry == 0x340u,
          "RS-alone -> RX/RY=lx/ly, LX/LY held");
    s = fresh();
    s.rx = 0xABCu; s.ry = 0x123u;
    CHECK(feed_str("2 08 ff 80", &s) == POKE_OK &&
          s.lx == 0xFF0u && s.rx == 0xABCu && s.ry == 0x123u,
          "LS-alone leaves RX/RY held");
    {
        static const char *bad[] = {
            "", " ", "10", "zz 08", "10 0g", "10 08 80", "10 08 80 80 80",
            "1 08 12 34 56 78 9a", "10000 08", "10 08 100 80",
            "10 08 80 80 80 80 80", "mash_a", "hello", "10 08 end",
        };
        size_t k;
        for (k = 0u; k < sizeof(bad) / sizeof(bad[0]); k++) {
            char msg[64];
            s.buttons = BTN_X;
            s.lx = 0x111u; s.ly = 0x222u; s.rx = 0x333u; s.ry = 0x444u;
            snprintf(msg, sizeof(msg), "malformed '%s' ignored, held",
                     bad[k][0] ? bad[k] : "(empty)");
            CHECK(feed_str(bad[k], &s) == POKE_IGNORE &&
                  s.buttons == BTN_X &&
                  s.lx == 0x111u && s.ly == 0x222u &&
                  s.rx == 0x333u && s.ry == 0x444u, msg);
        }
    }
    {
        char over[POKECON_LINE_MAX + 16];
        memset(over, 'f', sizeof(over) - 2);
        over[sizeof(over) - 2] = '\0';
        s = fresh();
        s.buttons = BTN_B;
        CHECK(feed_str(over, &s) == POKE_IGNORE && s.buttons == BTN_B,
              "overlong line dropped, state held");
    }
    {
        pokecon_linebuf_t lb;
        pokecon_rc_t rc = POKE_IGNORE;
        ctrl_state_t cur = fresh();
        bool done;
        pokecon_linebuf_init(&lb);
        done = pokecon_linebuf_feed(&lb, '1', &cur, &rc);
        CHECK(!done && cur.buttons == 0u, "partial byte buffered, held");
        done = pokecon_linebuf_feed(&lb, '0', &cur, &rc);
        CHECK(!done && cur.buttons == 0u, "partial line held pre-newline");
        done = pokecon_linebuf_feed(&lb, ' ', &cur, &rc);
        CHECK(!done, "partial with space still pending");
        done = pokecon_linebuf_feed(&lb, '0', &cur, &rc);
        CHECK(!done, "partial hat pending");
        done = pokecon_linebuf_feed(&lb, '8', &cur, &rc);
        CHECK(!done, "line complete but no newline yet");
        done = pokecon_linebuf_feed(&lb, '\r', &cur, &rc);
        CHECK(!done, "CR does not terminate");
        done = pokecon_linebuf_feed(&lb, '\n', &cur, &rc);
        CHECK(done && rc == POKE_OK && cur.buttons == BTN_A,
              "newline completes 10 08 = A");
    }
    {
        pokecon_linebuf_t lb;
        pokecon_rc_t rc = POKE_IGNORE;
        ctrl_state_t cur = fresh();
        unsigned k;
        bool done = false;
        cur.buttons = BTN_Y;
        pokecon_linebuf_init(&lb);
        for (k = 0u; k < POKECON_LINE_MAX + 10u; k++) {
            done = pokecon_linebuf_feed(&lb, 'f', &cur, &rc);
            CHECK(!done, "overlong accumulation never completes early");
            if (done) break;
        }
        done = pokecon_linebuf_feed(&lb, '\n', &cur, &rc);
        CHECK(done && rc == POKE_IGNORE && cur.buttons == BTN_Y,
              "overlong linebuf dropped at newline, held, no overflow");
        done = pokecon_linebuf_feed(&lb, '\n', &cur, &rc);
        CHECK(done && rc == POKE_IGNORE, "empty line after drop ignored");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
