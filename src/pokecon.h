// pokecon.h -- PokeController Modified serial-dialect input (task-17).
// Pico/BTstack-independent, host-testable (pack.c pattern). No side effects.
// Spec: docs/superpowers/specs/2026-09-16-pokecon-compat-design.md
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "protocol.h"

// Max accepted line length (excludes terminator). Longer lines are dropped
// whole (no overflow, prior state held).
#define POKECON_LINE_MAX 64u

typedef enum {
    POKE_OK = 0,    // line applied to *st (sticks held unless routed)
    POKE_END = 1,   // 'end': full NEUTRAL written to *st
    POKE_IGNORE = 2 // malformed/overlong/empty: *st untouched
} pokecon_rc_t;

// Parse one complete line (need not be NUL-terminated; n excludes any
// terminator or includes trailing CR/LF which are treated as separators).
// *st is in/out: sticks are held unless the LS/RS quirk routes new values.
pokecon_rc_t pokecon_parse_line(const char *line, size_t n, ctrl_state_t *st);

// Core1 byte accumulator: buffers until '\n', then parses. Partial lines
// return false (nothing applied). Overlong lines are dropped at '\n' with
// rc=POKE_IGNORE (no overflow). '\r' never terminates.
typedef struct {
    char buf[POKECON_LINE_MAX + 1u];
    uint8_t n;
    bool drop;
} pokecon_linebuf_t;

void pokecon_linebuf_init(pokecon_linebuf_t *lb);
bool pokecon_linebuf_feed(pokecon_linebuf_t *lb, char c, ctrl_state_t *st,
                          pokecon_rc_t *rc);
