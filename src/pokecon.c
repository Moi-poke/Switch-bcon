// pokecon.c -- PokeController Modified serial-dialect input (task-17).
// Pico/BTstack-independent, host-testable (pack.c pattern). Pure mapping,
// no side effects; malformed input leaves *st untouched.
// Spec: docs/superpowers/specs/2026-09-16-pokecon-compat-design.md
//   §1.2 line grammar, §1.3 wire-bit map, §1.4 hat, §1.5 stick quirk,
//   §1.6 'end' -> NEUTRAL.
#include "pokecon.h"

static bool hexval(char c, uint8_t *v) {
    if (c >= '0' && c <= '9') {
        *v = (uint8_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *v = (uint8_t)((c - 'a') + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *v = (uint8_t)((c - 'A') + 10);
        return true;
    }
    return false;
}

// Variable-width hex token (case-insensitive, padding-agnostic).
// False on empty / non-hex / value exceeding max.
static bool hex_tok(const char *s, size_t n, uint32_t *out, uint32_t max) {
    uint32_t v = 0u;
    size_t k;
    if (n == 0u || s == NULL || out == NULL) {
        return false;
    }
    for (k = 0u; k < n; k++) {
        uint8_t d;
        if (!hexval(s[k], &d)) {
            return false;
        }
        v = (v << 4) | d;
        if (v > max) {
            return false;
        }
    }
    *out = v;
    return true;
}

static bool is_sep(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Case-insensitive "end" match over a raw token.
static bool is_end_tok(const char *s, size_t n) {
    if (n != 3u || s == NULL) {
        return false;
    }
    return (s[0] | 0x20) == 'e' && (s[1] | 0x20) == 'n' &&
           (s[2] | 0x20) == 'd';
}

// wire bit -> VIIPER (§1.3). Bits 0-1 are LS/RS routing flags, not buttons.
static uint32_t wire_to_buttons(uint16_t wire) {
    uint32_t b = 0u;
    if (wire & (1u << 2)) b |= BTN_Y;
    if (wire & (1u << 3)) b |= BTN_B;
    if (wire & (1u << 4)) b |= BTN_A;
    if (wire & (1u << 5)) b |= BTN_X;
    if (wire & (1u << 6)) b |= BTN_L;
    if (wire & (1u << 7)) b |= BTN_R;
    if (wire & (1u << 8)) b |= BTN_ZL;
    if (wire & (1u << 9)) b |= BTN_ZR;
    if (wire & (1u << 10)) b |= BTN_MINUS;
    if (wire & (1u << 11)) b |= BTN_PLUS;
    if (wire & (1u << 12)) b |= BTN_LSTICK;
    if (wire & (1u << 13)) b |= BTN_RSTICK;
    if (wire & (1u << 14)) b |= BTN_HOME;
    if (wire & (1u << 15)) b |= BTN_CAPTURE;
    return b;
}

// hat 0-8 -> dpad buttons (§1.4). False for any other value.
static bool hat_to_buttons(uint32_t hat, uint32_t *out) {
    switch (hat) {
        case 0: *out = BTN_UP; break;
        case 1: *out = BTN_UP | BTN_RIGHT; break;
        case 2: *out = BTN_RIGHT; break;
        case 3: *out = BTN_DOWN | BTN_RIGHT; break;
        case 4: *out = BTN_DOWN; break;
        case 5: *out = BTN_DOWN | BTN_LEFT; break;
        case 6: *out = BTN_LEFT; break;
        case 7: *out = BTN_UP | BTN_LEFT; break;
        case 8: *out = 0u; break;
        default: return false;
    }
    return true;
}

pokecon_rc_t pokecon_parse_line(const char *line, size_t n, ctrl_state_t *st) {
    const char *tok[6];
    size_t tlen[6];
    size_t ntok = 0u;
    size_t i = 0u;
    uint32_t wire32, hat, dpad, lx, ly, rx, ry;
    bool ls, rs;

    if (line == NULL || st == NULL) {
        return POKE_IGNORE;
    }
    if (n > POKECON_LINE_MAX) {
        return POKE_IGNORE; // overlong: drop whole, hold prior state
    }
    // Split on space/TAB/CR/LF (§3: yqYo1 ParseLine equivalent).
    while (i < n) {
        while (i < n && is_sep(line[i])) {
            i++;
        }
        if (i >= n) {
            break;
        }
        if (ntok >= 6u) {
            return POKE_IGNORE; // 7+ fields: malformed
        }
        tok[ntok] = &line[i];
        tlen[ntok] = 0u;
        while (i < n && !is_sep(line[i])) {
            i++;
            tlen[ntok]++;
        }
        ntok++;
    }
    if (ntok == 0u) {
        return POKE_IGNORE;
    }
    if (ntok == 1u) {
        // v1: only 'end' is a valid word (§0: MCU/keyboard/Date out of scope).
        if (!is_end_tok(tok[0], tlen[0])) {
            return POKE_IGNORE;
        }
        st->buttons = 0u;
        st->lx = st->ly = 0x800u;
        st->rx = st->ry = 0x800u;
        return POKE_END;
    }
    if (ntok != 2u && ntok != 4u && ntok != 6u) {
        return POKE_IGNORE;
    }
    if (!hex_tok(tok[0], tlen[0], &wire32, 0xFFFFu)) {
        return POKE_IGNORE;
    }
    if (!hex_tok(tok[1], tlen[1], &hat, 0xFFu) || !hat_to_buttons(hat, &dpad)) {
        return POKE_IGNORE;
    }
    if (ntok >= 4u) {
        if (!hex_tok(tok[2], tlen[2], &lx, 0xFFu) ||
            !hex_tok(tok[3], tlen[3], &ly, 0xFFu)) {
            return POKE_IGNORE;
        }
    } else {
        lx = ly = 0u; // unused: no stick fields -> held (§1.5)
    }
    if (ntok == 6u) {
        if (!hex_tok(tok[4], tlen[4], &rx, 0xFFu) ||
            !hex_tok(tok[5], tlen[5], &ry, 0xFFu)) {
            return POKE_IGNORE;
        }
    } else {
        rx = ry = 0u; // unused: RS routes lx/ly unless 6 fields present
    }

    st->buttons = wire_to_buttons((uint16_t)wire32) | dpad;
    ls = (wire32 & 0x02u) != 0u;
    rs = (wire32 & 0x01u) != 0u;
    if (ls) {
        // u8 1:1 copy; internal u16 = u8<<4 like LEN-8 ingest (§1.5).
        st->lx = (uint16_t)(lx << 4);
        st->ly = (uint16_t)(ly << 4);
    }
    if (rs) {
        if (ntok == 6u) {
            st->rx = (uint16_t)(rx << 4);
            st->ry = (uint16_t)(ry << 4);
        } else if (ntok == 4u) {
            st->rx = (uint16_t)(lx << 4); // RS-alone quirk: RX/RY = lx/ly
            st->ry = (uint16_t)(ly << 4);
        }
    }
    // No flags, or no stick fields: sticks held (§1.5).
    return POKE_OK;
}

void pokecon_linebuf_init(pokecon_linebuf_t *lb) {
    if (lb != NULL) {
        lb->n = 0u;
        lb->drop = false;
        lb->buf[0] = '\0';
    }
}

bool pokecon_linebuf_feed(pokecon_linebuf_t *lb, char c, ctrl_state_t *st,
                          pokecon_rc_t *rc) {
    pokecon_rc_t r;
    if (lb == NULL || st == NULL || rc == NULL) {
        return false;
    }
    if (c == '\n') {
        if (lb->drop) {
            lb->drop = false;
            lb->n = 0u;
            *rc = POKE_IGNORE; // overlong line dropped, state held
            return true;
        }
        r = pokecon_parse_line(lb->buf, lb->n, st);
        lb->n = 0u;
        *rc = r;
        return true;
    }
    if (lb->drop) {
        return false; // swallow until '\n'
    }
    if (lb->n >= POKECON_LINE_MAX) {
        lb->drop = true; // cap: no overflow, drop through newline
        return false;
    }
    lb->buf[lb->n++] = c; // '\r' buffered as separator, never terminates
    return false;
}
