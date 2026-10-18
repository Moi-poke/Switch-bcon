// test_usb_cdc.c -- USB-CDC chunking vs UART equivalence (transport-independent pump).
//
// Judgment: NO new src/proto helper is needed. `parser_feed_buf()` IS the shared
// byte-pump: it is Pico/BTstack/TinyUSB-free (protocol.c includes only
// protocol.h + string.h), keeps an accumulation buffer across calls, and therefore
// accepts an arbitrary byte source (UART DMA ring chunk, CDC 1B chunk, CDC 64B
// bulk, split/rejoin, noise-injected) with identical results. This test pins that.
//
// FW wiring assumption (mode-switch): a SINGLE parser instance is fed from the
// active transport (UART DMA in wired-log/PoC path today, tud_cdc Rx in wireless
// CDC path). Never two parser instances (that would fork the SEQ baseline).
// Double delivery (same frame from UART+CDC) therefore lands sequentially in one
// parser. Policy fixed here:
//   - STATE payload: last-wins, idempotent (full-state resync, spec design goal 3).
//   - SEQ layer: NOT idempotent. A duplicate SEQ != expect+1, so it counts as one
//     gap event (err_drop+1, errcode=ERR_SEQ_GAP). Diagnostic visibility wins over
//     silent dedup; payload safety is already guaranteed by last-wins.
// dispatch.h FX_*/ACT_* enums are append-only; this test adds none.
//
// Vectors in [0] are the exact bytes poc_send.py builders emit
// (build_state/build_state_full/build_state12/build_ping/build_hello,
// crc8_smbus, BTN_MASK=0x003FFFFF). Hardcoded so the C canonical generator
// (frame_build) is checked against the Python sender, not against itself.
#include <stdio.h>
#include <string.h>

#include "../../src/proto/protocol.h"
#include "../../src/proto/dispatch.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

// ---- FW-mirror inbox (Core1 -> Core0, main.c IB_N=8) ----
#define IB_N 8
typedef struct {
    uint8_t type, len, seq;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} ib_msg_t;
typedef struct {
    ib_msg_t msg[IB_N];
    uint8_t w, r, drop;
} inbox_t;

static void inbox_init(inbox_t *ib) {
    memset(ib, 0, sizeof(*ib));
}

static void inbox_push(inbox_t *ib, uint8_t type, const uint8_t *p,
                       uint8_t len, uint8_t seq) {
    uint8_t nw = (uint8_t)((ib->w + 1u) % IB_N);
    if (nw == ib->r) {
        ib->drop++;
        return;
    }
    ib->msg[ib->w].type = type;
    ib->msg[ib->w].len = (len > PROTO_MAX_PAYLOAD) ? PROTO_MAX_PAYLOAD : len;
    ib->msg[ib->w].seq = seq;
    if (p != NULL && ib->msg[ib->w].len > 0) {
        memcpy(ib->msg[ib->w].payload, p, ib->msg[ib->w].len);
    }
    ib->w = nw;
}

// ---- One transport pipeline: parser + live STATE apply + inbox ----
typedef struct {
    parser_t p;
    link_stats_t st;
    ctrl_state_t ctrl;
    uint32_t state_frames;
    uint32_t frames;
    uint8_t last_type, last_seq;
    inbox_t ib;
    v3_session_t vsess;
} pipe_t;

static void pipe_cb(uint8_t type, const uint8_t *pl, uint8_t len,
                    uint8_t seq, void *user) {
    pipe_t *q = (pipe_t *)user;
    q->frames++;
    q->last_type = type;
    q->last_seq = seq;
    if (type == T_STATE && proto_state_len_ok(len)) {
        // Mirrors main.c bcon_frame_cb trusted path (LEN8 u8<<4 / LEN12 u16LE).
        q->ctrl.buttons = (uint32_t)pl[0] | ((uint32_t)pl[1] << 8) |
                          ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24);
        if (len == 8) {
            q->ctrl.lx = (uint16_t)pl[4] << 4;
            q->ctrl.ly = (uint16_t)pl[5] << 4;
            q->ctrl.rx = (uint16_t)pl[6] << 4;
            q->ctrl.ry = (uint16_t)pl[7] << 4;
        } else {
            q->ctrl.lx = (uint16_t)(pl[4] | ((uint16_t)pl[5] << 8)) & 0x0FFFu;
            q->ctrl.ly = (uint16_t)(pl[6] | ((uint16_t)pl[7] << 8)) & 0x0FFFu;
            q->ctrl.rx = (uint16_t)(pl[8] | ((uint16_t)pl[9] << 8)) & 0x0FFFu;
            q->ctrl.ry = (uint16_t)(pl[10] | ((uint16_t)pl[11] << 8)) & 0x0FFFu;
        }
        q->state_frames++;
    } else if (type == T_NEUTRAL && len == 0) {
        q->ctrl.buttons = 0u;
        q->ctrl.lx = q->ctrl.ly = 0x800u;
        q->ctrl.rx = q->ctrl.ry = 0x800u;
        q->state_frames++;
    } else {
        inbox_push(&q->ib, type, pl, len, seq);
    }
}

static void pipe_init(pipe_t *q) {
    memset(q, 0, sizeof(*q));
    inbox_init(&q->ib);
    q->ctrl.buttons = 0u;
    q->ctrl.lx = q->ctrl.ly = 0x800u;
    q->ctrl.rx = q->ctrl.ry = 0x800u;
    parser_init(&q->p, pipe_cb, q, &q->st);
    v3_session_init(&q->vsess);
}

static void pipe_feed_all(pipe_t *q, const uint8_t *d, size_t n) {
    parser_feed_buf(&q->p, d, n);
}

static void pipe_feed_1b(pipe_t *q, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i++) {
        parser_feed(&q->p, d[i]);
    }
}

static void pipe_feed_chunks(pipe_t *q, const uint8_t *d, size_t n, size_t chunk) {
    size_t off = 0;
    while (off < n) {
        size_t k = (n - off < chunk) ? (n - off) : chunk;
        parser_feed_buf(&q->p, d + off, k);
        off += k;
    }
}

static void pipe_feed_split(pipe_t *q, const uint8_t *d, size_t n, size_t at) {
    if (at > n) {
        at = n;
    }
    if (at > 0) {
        parser_feed_buf(&q->p, d, at);
    }
    if (at < n) {
        parser_feed_buf(&q->p, d + at, n - at);
    }
}

// Drain inbox FIFO through dispatch (mirrors Core0 poll: inbox -> v3_on_frame).
static void pipe_drain(pipe_t *q) {
    while (q->ib.r != q->ib.w) {
        ib_msg_t *m = &q->ib.msg[q->ib.r];
        const uint8_t *pl = (m->len > 0) ? m->payload : NULL;
        v3_on_frame(&q->vsess, m->type, pl, m->len, m->seq);
        q->ib.r = (uint8_t)((q->ib.r + 1u) % IB_N);
    }
}

static int inbox_equal(const inbox_t *a, const inbox_t *b) {
    if (a->w != b->w || a->r != b->r || a->drop != b->drop) {
        return 0;
    }
    for (int i = 0; i < IB_N; i++) {
        if (a->msg[i].type != b->msg[i].type ||
            a->msg[i].len != b->msg[i].len ||
            a->msg[i].seq != b->msg[i].seq) {
            return 0;
        }
        if (a->msg[i].len > 0 &&
            memcmp(a->msg[i].payload, b->msg[i].payload, a->msg[i].len) != 0) {
            return 0;
        }
    }
    return 1;
}

static int stats_equal(const link_stats_t *a, const link_stats_t *b) {
    return a->last_seq == b->last_seq && a->err_crc == b->err_crc &&
           a->err_drop == b->err_drop && a->errcode == b->errcode &&
           a->have_seq == b->have_seq;
}

static int sess_equal(const v3_session_t *a, const v3_session_t *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static int pipe_equal(const pipe_t *a, const pipe_t *b) {
    if (a->frames != b->frames || a->state_frames != b->state_frames ||
        a->last_type != b->last_type || a->last_seq != b->last_seq) {
        return 0;
    }
    if (a->ctrl.buttons != b->ctrl.buttons || a->ctrl.lx != b->ctrl.lx ||
        a->ctrl.ly != b->ctrl.ly || a->ctrl.rx != b->ctrl.rx ||
        a->ctrl.ry != b->ctrl.ry) {
        return 0;
    }
    if (!stats_equal(&a->st, &b->st)) {
        return 0;
    }
    if (!inbox_equal(&a->ib, &b->ib)) {
        return 0;
    }
    return 1;
}

static void put_btn32(uint8_t *pl, uint32_t b) {
    pl[0] = (uint8_t)(b & 0xFFu);
    pl[1] = (uint8_t)((b >> 8) & 0xFFu);
    pl[2] = (uint8_t)((b >> 16) & 0xFFu);
    pl[3] = (uint8_t)((b >> 24) & 0xFFu);
}

// Build the shared mixed stream used by [1]: HELLO + STATE + STATE12 + PING +
// NEUTRAL + STATUS_REQ + STATE(with 0xAB byte in payload). SEQ 0x00..0x06.
static size_t build_mixed(uint8_t *out) {
    size_t n = 0;
    uint8_t pl[16];
    pl[0] = 0x04;
    pl[1] = 0x00;
    n += frame_build(out + n, T_HELLO, pl, 2, 0x00);
    put_btn32(pl, BTN_A);
    pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
    n += frame_build(out + n, T_STATE, pl, 8, 0x01);
    put_btn32(pl, BTN_B);
    // BTN_B + sticks 0x0800/0x0800/0x0800/0x0800 as u16LE pairs (LEN12).
    pl[4] = 0x00;
    pl[5] = 0x08;
    pl[6] = 0x00;
    pl[7] = 0x08;
    pl[8] = 0x00;
    pl[9] = 0x08;
    pl[10] = 0x00;
    pl[11] = 0x08;
    n += frame_build(out + n, T_STATE, pl, 12, 0x02);
    n += frame_build(out + n, T_PING, NULL, 0, 0x03);
    n += frame_build(out + n, T_NEUTRAL, NULL, 0, 0x04);
    n += frame_build(out + n, T_STATUS_REQ, NULL, 0, 0x05);
    // Payload containing SYNC byte 0xAB (BTN low byte) to prove CRC-gated parse.
    put_btn32(pl, 0x0000AB00u);
    pl[4] = 0x00;
    pl[5] = 0xFF;
    pl[6] = 0x80;
    pl[7] = 0x7F;
    n += frame_build(out + n, T_STATE, pl, 8, 0x06);
    return n;
}

int main(void) {
    printf("[0] poc_send.py real bytes vs frame_build\n");
    CHECK(crc8((const uint8_t *)"123456789", 9) == 0xF4, "crc8==0xF4 (poc parity)");
    {
        // build_state(btn=0x02, seq=0x2A): A pressed, sticks center.
        const uint8_t exp[] = { 0xAB, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00,
                                0x80, 0x80, 0x80, 0x80, 0x2A, 0x69 };
        uint8_t pl[8];
        uint8_t out[16];
        size_t nn;
        put_btn32(pl, BTN_A);
        pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        nn = frame_build(out, T_STATE, pl, 8, 0x2A);
        CHECK(nn == sizeof(exp) && memcmp(out, exp, sizeof(exp)) == 0,
              "build_state(A,0x2A) matches poc");
    }
    {
        // build_state_full(btn=0x05, lx=0, ly=0xFF, rx=0x80, ry=0x7F, seq=0x10).
        const uint8_t exp[] = { 0xAB, 0x01, 0x08, 0x05, 0x00, 0x00, 0x00,
                                0x00, 0xFF, 0x80, 0x7F, 0x10, 0x07 };
        uint8_t pl[8];
        uint8_t out[16];
        size_t nn;
        put_btn32(pl, 0x05u);
        pl[4] = 0x00;
        pl[5] = 0xFF;
        pl[6] = 0x80;
        pl[7] = 0x7F;
        nn = frame_build(out, T_STATE, pl, 8, 0x10);
        CHECK(nn == sizeof(exp) && memcmp(out, exp, sizeof(exp)) == 0,
              "build_state_full matches poc");
    }
    {
        // build_state12(btn=0x02, 0x800 x4, seq=0x11).
        const uint8_t exp[] = { 0xAB, 0x01, 0x0C, 0x02, 0x00, 0x00, 0x00,
                                0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00,
                                0x08, 0x11, 0x82 };
        uint8_t pl[12];
        uint8_t out[24];
        size_t nn;
        put_btn32(pl, BTN_A);
        pl[4] = 0x00;
        pl[5] = 0x08;
        pl[6] = 0x00;
        pl[7] = 0x08;
        pl[8] = 0x00;
        pl[9] = 0x08;
        pl[10] = 0x00;
        pl[11] = 0x08;
        nn = frame_build(out, T_STATE, pl, 12, 0x11);
        CHECK(nn == sizeof(exp) && memcmp(out, exp, sizeof(exp)) == 0,
              "build_state12 matches poc");
    }
    {
        // build_ping(seq=0x31).
        const uint8_t exp[] = { 0xAB, 0x03, 0x00, 0x31, 0x2A };
        uint8_t out[8];
        size_t nn = frame_build(out, T_PING, NULL, 0, 0x31);
        CHECK(nn == sizeof(exp) && memcmp(out, exp, sizeof(exp)) == 0,
              "build_ping(0x31) matches poc");
    }
    {
        // build_hello(ver=4, flags=0, seq=0x01).
        const uint8_t exp[] = { 0xAB, 0x10, 0x02, 0x04, 0x00, 0x01, 0xB2 };
        uint8_t pl[2] = { 0x04, 0x00 };
        uint8_t out[8];
        size_t nn = frame_build(out, T_HELLO, pl, 2, 0x01);
        CHECK(nn == sizeof(exp) && memcmp(out, exp, sizeof(exp)) == 0,
              "build_hello(4,0,0x01) matches poc");
    }

    printf("[1] UART(all-at-once) vs CDC chunkings: same frames + STATE\n");
    {
        uint8_t stream[256];
        size_t slen = build_mixed(stream);
        pipe_t ref, c1, c64, c7;
        pipe_init(&ref);
        pipe_feed_all(&ref, stream, slen);
        pipe_drain(&ref);

        pipe_init(&c1);
        pipe_feed_1b(&c1, stream, slen);
        pipe_drain(&c1);
        CHECK(pipe_equal(&ref, &c1) && sess_equal(&ref.vsess, &c1.vsess),
              "1B CDC chunks == UART");

        pipe_init(&c64);
        pipe_feed_chunks(&c64, stream, slen, 64);
        pipe_drain(&c64);
        CHECK(pipe_equal(&ref, &c64) && sess_equal(&ref.vsess, &c64.vsess),
              "64B CDC bulks == UART");

        pipe_init(&c7);
        pipe_feed_chunks(&c7, stream, slen, 7);
        pipe_drain(&c7);
        CHECK(pipe_equal(&ref, &c7) && sess_equal(&ref.vsess, &c7.vsess),
              "7B CDC chunks == UART");

        // Final STATE is the 0xAB-payload frame: BTN=0xAB00, sticks as built.
        CHECK(ref.ctrl.buttons == 0x0000AB00u && ref.ctrl.lx == (0x00u << 4) &&
              ref.ctrl.ly == (0xFFu << 4) && ref.ctrl.rx == (0x80u << 4) &&
              ref.ctrl.ry == (0x7Fu << 4),
              "final STATE applied (0xAB payload survived)");
        CHECK(ref.state_frames == 4 && ref.frames == 7,
              "live count 3xSTATE+NEUTRAL, total 7 frames");
        CHECK(ref.vsess.ob_n == 4, "drained HELLO_ACK+PONG+STATUS+PLAYER_INFO");
    }

    printf("[1b] every split point: split/rejoin == unfragmented\n");
    {
        uint8_t stream[256];
        size_t slen = build_mixed(stream);
        pipe_t ref;
        int bad = 0;
        pipe_init(&ref);
        pipe_feed_all(&ref, stream, slen);
        for (size_t at = 1; at < slen; at++) {
            pipe_t q;
            pipe_init(&q);
            pipe_feed_split(&q, stream, slen, at);
            if (!pipe_equal(&ref, &q)) {
                bad++;
            }
        }
        CHECK(bad == 0, "all split positions identical");
    }

    printf("[1c] noise-wrapped stream: chunking still identical\n");
    {
        uint8_t stream[256];
        uint8_t noisy[288];
        size_t slen = build_mixed(stream);
        const uint8_t pre[] = { 0x00, 0xFF, 0x12, 0xAB, 0x99 };
        const uint8_t mid[] = { 0x00, 0xAB };
        size_t nlen = 0;
        memcpy(noisy + nlen, pre, sizeof(pre));
        nlen += sizeof(pre);
        memcpy(noisy + nlen, stream, slen);
        nlen += slen;
        memcpy(noisy + nlen, mid, sizeof(mid));
        nlen += sizeof(mid);
        pipe_t a, b;
        pipe_init(&a);
        pipe_feed_all(&a, noisy, nlen);
        pipe_init(&b);
        pipe_feed_1b(&b, noisy, nlen);
        CHECK(pipe_equal(&a, &b), "noise + 1B == noise + bulk");
        CHECK(a.frames == 7, "noise did not create phantom frames");
    }

    printf("[2a] CRC-broken frame: UART vs CDC identical\n");
    {
        uint8_t good[16];
        uint8_t badf[16];
        uint8_t nz[8];
        uint8_t stream_u[32];
        size_t ng = 0;
        size_t nn;
        uint8_t pl[8];
        put_btn32(pl, BTN_A);
        pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        ng = frame_build(good, T_STATE, pl, 8, 0x20);
        memcpy(badf, good, ng);
        badf[5] ^= 0xFF; // corrupt payload, keep stale CRC
        nn = frame_build(nz, T_NEUTRAL, NULL, 0, 0x21);
        memcpy(stream_u, badf, ng);
        memcpy(stream_u + ng, nz, nn);
        pipe_t a, b, c;
        pipe_init(&a);
        pipe_feed_all(&a, stream_u, ng + nn);
        pipe_init(&b);
        pipe_feed_1b(&b, stream_u, ng + nn);
        pipe_init(&c);
        pipe_feed_chunks(&c, stream_u, ng + nn, 64);
        CHECK(pipe_equal(&a, &b) && pipe_equal(&a, &c), "CRC path chunk-independent");
        CHECK(a.frames == 1 && a.st.err_crc >= 1 && a.st.errcode == ERR_BAD_CRC,
              "bad CRC dropped, NEUTRAL recovered, code latched");
    }

    printf("[2b] LEN-invalid frame: UART vs CDC identical\n");
    {
        // STATE with LEN=7 (spec needs 8/12). CRC over the wire bytes.
        uint8_t bad[12] = { 0xAB, 0x01, 0x07, 0, 0, 0, 0, 0, 0, 0, 0x22, 0x00 };
        uint8_t pg[8];
        size_t npg;
        uint8_t stream[32];
        bad[11] = crc8(&bad[1], 10);
        npg = frame_build(pg, T_PING, NULL, 0, 0x23);
        memcpy(stream, bad, sizeof(bad));
        memcpy(stream + sizeof(bad), pg, npg);
        pipe_t a, b;
        pipe_init(&a);
        pipe_feed_all(&a, stream, sizeof(bad) + npg);
        pipe_init(&b);
        pipe_feed_chunks(&b, stream, sizeof(bad) + npg, 5);
        CHECK(pipe_equal(&a, &b), "LEN path chunk-independent");
        CHECK(a.frames == 1 && a.st.errcode == ERR_BAD_LEN,
              "LEN=7 dropped, PING parsed");
    }

    printf("[2c] SEQ gap: UART vs CDC identical\n");
    {
        uint8_t f0[8], f1[8], f2[8];
        uint8_t stream[24];
        size_t n0, n1, n2;
        n0 = frame_build(f0, T_NEUTRAL, NULL, 0, 0x10);
        n1 = frame_build(f1, T_NEUTRAL, NULL, 0, 0x11);
        n2 = frame_build(f2, T_NEUTRAL, NULL, 0, 0x13); // skip 0x12
        memcpy(stream, f0, n0);
        memcpy(stream + n0, f1, n1);
        memcpy(stream + n0 + n1, f2, n2);
        pipe_t a, b;
        pipe_init(&a);
        pipe_feed_all(&a, stream, n0 + n1 + n2);
        pipe_init(&b);
        pipe_feed_1b(&b, stream, n0 + n1 + n2);
        CHECK(pipe_equal(&a, &b), "SEQ path chunk-independent");
        CHECK(a.st.err_drop == 1 && a.st.errcode == ERR_SEQ_GAP,
              "skip counts exactly 1 gap event");
    }

    printf("[2d] unknown TYPE + inbox/outbox overflow: UART vs CDC identical\n");
    {
        uint8_t u[40];
        uint8_t upay[2] = { 0xAA, 0xBB };
        size_t nu = frame_build(u, 0x41, upay, 2, 0x51);
        pipe_t a, b;
        pipe_init(&a);
        pipe_feed_all(&a, u, nu);
        pipe_init(&b);
        pipe_feed_1b(&b, u, nu);
        CHECK(pipe_equal(&a, &b) && a.frames == 1, "unknown 0x41 accepted by parser");
        pipe_drain(&a);
        pipe_drain(&b);
        CHECK(sess_equal(&a.vsess, &b.vsess) && a.vsess.ob_n == 0 &&
              a.vsess.fx == FX_NONE,
              "unknown TYPE ignored by dispatch");
    }
    {
        // 10 PINGs into an 8-slot ring (IB_N=8, one slot kept empty: cap 7).
        uint8_t stream[10 * 5];
        size_t slen = 0;
        for (int k = 0; k < 10; k++) {
            uint8_t f[8];
            size_t nf = frame_build(f, T_PING, NULL, 0, (uint8_t)k);
            memcpy(stream + slen, f, nf);
            slen += nf;
        }
        pipe_t a, b, c;
        pipe_init(&a);
        pipe_feed_all(&a, stream, slen);
        pipe_init(&b);
        pipe_feed_1b(&b, stream, slen);
        pipe_init(&c);
        pipe_feed_chunks(&c, stream, slen, 64);
        CHECK(pipe_equal(&a, &b) && pipe_equal(&a, &c), "inbox fill chunk-independent");
        CHECK(a.frames == 10 && a.ib.drop == 3 && a.ib.w == 7,
              "inbox drops 3 beyond 7-slot cap");
        pipe_drain(&a);
        pipe_drain(&b);
        pipe_drain(&c);
        CHECK(sess_equal(&a.vsess, &b.vsess) && sess_equal(&a.vsess, &c.vsess),
              "drained dispatch identical");
        CHECK(a.vsess.ob_n == 7 && a.vsess.ob_dropped == 0,
              "7 PINGs fit 8-slot outbox without drop");
    }
    {
        // 10 PINGs drained from a fresh session overflow the 8-slot outbox.
        v3_session_t s;
        v3_session_init(&s);
        for (int k = 0; k < 10; k++) {
            v3_on_frame(&s, T_PING, NULL, 0, (uint8_t)k);
        }
        CHECK(s.ob_n == V3_OB_N && s.ob_dropped == 2 && s.ob[0].arg == 0 &&
              s.ob[7].arg == 7,
              "outbox keeps 8, drops 2 (existing [12] pin)");
    }

    printf("[3] double delivery (UART+CDC same frame): last-wins + SEQ event\n");
    {
        uint8_t f[16];
        uint8_t pl[8];
        size_t nf;
        put_btn32(pl, BTN_A);
        pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        nf = frame_build(f, T_STATE, pl, 8, 0x20);
        pipe_t once, twice;
        pipe_init(&once);
        pipe_feed_all(&once, f, nf);
        pipe_init(&twice);
        pipe_feed_all(&twice, f, nf);
        pipe_feed_chunks(&twice, f, nf, 1); // second copy via other transport
        CHECK(twice.ctrl.buttons == once.ctrl.buttons &&
              twice.ctrl.lx == once.ctrl.lx && twice.ctrl.ly == once.ctrl.ly &&
              twice.ctrl.rx == once.ctrl.rx && twice.ctrl.ry == once.ctrl.ry,
              "duplicate payload idempotent (same STATE)");
        CHECK(twice.frames == once.frames + 1 &&
              twice.st.err_drop == once.st.err_drop + 1 &&
              twice.st.last_seq == 0x20 && twice.st.errcode == ERR_SEQ_GAP,
              "duplicate SEQ counts 1 gap event");
    }
    {
        // Same SEQ, different payload: last copy wins (not first, not merged).
        uint8_t f1[16], f2[16];
        uint8_t pl[8];
        size_t n1, n2;
        put_btn32(pl, BTN_A);
        pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        n1 = frame_build(f1, T_STATE, pl, 8, 0x30);
        put_btn32(pl, BTN_B);
        pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        n2 = frame_build(f2, T_STATE, pl, 8, 0x30);
        pipe_t q;
        pipe_init(&q);
        pipe_feed_all(&q, f1, n1);
        pipe_feed_1b(&q, f2, n2);
        CHECK(q.ctrl.buttons == BTN_B, "same-SEQ race: last-wins");
        CHECK(q.st.err_drop == 1 && q.st.errcode == ERR_SEQ_GAP,
              "same-SEQ race still 1 gap event");
        (void)n2;
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
