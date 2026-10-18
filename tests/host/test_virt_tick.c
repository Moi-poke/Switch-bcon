// test_virt_tick.c -- FW poll_tick相当の仮想駆動ハーネス (host runnable)。
// 対象: CDC/USB profile切替・inbox tr輸送タグ・応答輸送帰属・flush・
// state_accept mirror・CDC idle resync・UART hunt非接触。Pico SDK不要。
// 本物から取り込むのは src/proto のみ (protocol.c/dispatch.c/baud.c)。
// TinyUSB/BTstack/UART/GPIO/mutex/flash は全てhost側スタブで代替し、
// main.c の当該ロジックだけを忠実に複写した tick モデルで駆動する。
// 方針: FWの振舞いを固定する回帰ピンであり、新仕様の追加ではない。
#include <stdio.h>
#include <string.h>
#include "../../src/proto/protocol.h"
#include "../../src/proto/dispatch.h"
#include "../../src/proto/baud.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

#define TR_UART 0u
#define TR_CDC  1u
#define IB_N 8
#define SEQ_RESYNC_IDLE_MS 1000u
#define TIMEOUT_NEUTRAL_MS 200u

typedef struct {
    uint8_t type, len, seq;
    uint8_t payload[PROTO_MAX_PAYLOAD];
    uint8_t tr;
} ib_msg_t;

// ---- 仮想共有状態 (g_s の必要分) ----
typedef struct {
    ctrl_state_t state;
    uint8_t last_seq, last_type;
    uint32_t frames, state_frames;
    uint32_t crc_err, drop_ev;
    uint8_t perrcode;
    int state_accept; // Core0 mirror後 (g_vs.state_accept写し)
    ib_msg_t ib[IB_N];
    uint8_t ib_w, ib_r;
    uint32_t ib_drop;
} vshared_t;

// ---- 仮想TX記録 (輸送別SEQ + 送出先) ----
typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t seq;
    uint8_t tr; // 0=UART 1=CDC
} txrec_t;
#define TX_CAP 64
typedef struct {
    txrec_t rec[TX_CAP];
    int n;
    uint8_t seq_uart, seq_cdc;
    int uart_locked;   // s_baud_tx_ok 相当
    int cdc_connected; // usb_cdc_connected 相当 (+wired profile時はfalse)
    int wired_profile; // usb_cdc_is_wired_profile 相当
} vtx_t;

// ---- tick モデル全体 (g_vs + CDC parser + UART parser + 共有 + TX) ----
typedef struct {
    v3_session_t vs;
    vshared_t sh;
    vtx_t tx;
    parser_t pu, pc;      // UART路 / CDC路 parser (stats分離)
    link_stats_t stu, stc;
    int tr_last;          // s_last_rx_transport
    // CDC路境界
    int cdc_fed, cdc_idle_gated;
    uint32_t cdc_last_ms, now;
    // UART hunt相当: locked まで2連続有効frame (baud_lock_tそのものを使用)
    baud_lock_t hlock;
    int uart_hlocked;
    // timeout-neutral相当
    uint32_t last_state_ms;
    uint32_t last_state_frames;
    int neutral_hold;
} tick_t;

static void vtx_send(vtx_t *t, uint8_t type, const uint8_t *pl, uint8_t len, int tr) {
    uint8_t frame[PROTO_FRAME_MAX];
    size_t n;
    (void)pl;
    if (t->n >= TX_CAP) return;
    if (tr == 1) {
        if (!t->cdc_connected || t->wired_profile) return; // HID profile時はCDC不応答
        n = frame_build(frame, type, pl, len, t->seq_cdc);
        if (n == 0) return;
        t->rec[t->n].seq = t->seq_cdc;
        t->seq_cdc++;
    } else {
        if (!t->uart_locked) return; // UART hunt未確定は捨てる
        n = frame_build(frame, type, pl, len, t->seq_uart);
        if (n == 0) return;
        t->rec[t->n].seq = t->seq_uart;
        t->seq_uart++;
    }
    t->rec[t->n].type = type;
    t->rec[t->n].len = len;
    t->rec[t->n].tr = (uint8_t)tr;
    t->n++;
}

static void vsh_push(vshared_t *s, uint8_t type, const uint8_t *p,
                     uint8_t len, uint8_t seq, uint8_t tr) {
    uint8_t nw = (uint8_t)((s->ib_w + 1u) % IB_N);
    if (nw == s->ib_r) { s->ib_drop++; return; }
    s->ib[s->ib_w].type = type;
    s->ib[s->ib_w].len = (len > PROTO_MAX_PAYLOAD) ? PROTO_MAX_PAYLOAD : len;
    s->ib[s->ib_w].seq = seq;
    s->ib[s->ib_w].tr = tr;
    if (p != NULL && s->ib[s->ib_w].len > 0)
        memcpy(s->ib[s->ib_w].payload, p, s->ib[s->ib_w].len);
    s->ib_w = nw;
}

static void vcb_uart(uint8_t type, const uint8_t *p, uint8_t len,
                     uint8_t seq, void *user) {
    tick_t *t = (tick_t *)user;
    // hunt: 2連続有効frameで確定 (main.c Core1相当。確定purgeはUART路のみ捨て・CDC温存)
    if (!t->uart_hlocked) {
        if (baud_lock_feed(&t->hlock, 1)) {
            uint8_t rr, ww, n;
            t->uart_hlocked = 1;
            rr = t->sh.ib_r;
            ww = t->sh.ib_w;
            n = 0u;
            while (rr != ww) {
                if (t->sh.ib[rr].tr == TR_CDC) {
                    if (n != rr) t->sh.ib[n] = t->sh.ib[rr];
                    n = (uint8_t)((n + 1u) % IB_N);
                }
                rr = (uint8_t)((rr + 1u) % IB_N);
            }
            t->sh.ib_r = 0u;
            t->sh.ib_w = n;
        }
    }
    if (type == T_STATE && proto_state_len_ok(len)) {
        if (t->uart_hlocked && t->sh.state_accept) {
            t->sh.state_frames++;
            t->sh.state.buttons = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            if (len == 8) {
                t->sh.state.lx = (uint16_t)p[4] << 4;
                t->sh.state.ly = (uint16_t)p[5] << 4;
                t->sh.state.rx = (uint16_t)p[6] << 4;
                t->sh.state.ry = (uint16_t)p[7] << 4;
            } else {
                t->sh.state.lx = (uint16_t)(p[4] | ((uint16_t)p[5] << 8)) & 0x0FFFu;
                t->sh.state.ly = (uint16_t)(p[6] | ((uint16_t)p[7] << 8)) & 0x0FFFu;
                t->sh.state.rx = (uint16_t)(p[8] | ((uint16_t)p[9] << 8)) & 0x0FFFu;
                t->sh.state.ry = (uint16_t)(p[10] | ((uint16_t)p[11] << 8)) & 0x0FFFu;
            }
        }
    } else if (type == T_NEUTRAL && len == 0) {
        if (t->uart_hlocked) {
            t->sh.state_frames++;
            t->sh.state.buttons = 0u;
            t->sh.state.lx = t->sh.state.ly = 0x800u;
            t->sh.state.rx = t->sh.state.ry = 0x800u;
        }
    } else {
        vsh_push(&t->sh, type, p, len, seq, TR_UART);
    }
    t->sh.last_seq = seq;
    t->sh.last_type = type;
    t->sh.frames++;
    t->sh.crc_err = t->stu.err_crc;
    t->sh.drop_ev = t->stu.err_drop;
}

static void vcb_cdc(uint8_t type, const uint8_t *p, uint8_t len,
                    uint8_t seq, void *user) {
    tick_t *t = (tick_t *)user;
    // hunt計数なし・trusted=connected (main.c cdc_frame_cb相当)
    if (type == T_STATE && proto_state_len_ok(len)) {
        if (t->sh.state_accept) {
            t->sh.state_frames++;
            t->sh.state.buttons = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            if (len == 8) {
                t->sh.state.lx = (uint16_t)p[4] << 4;
                t->sh.state.ly = (uint16_t)p[5] << 4;
                t->sh.state.rx = (uint16_t)p[6] << 4;
                t->sh.state.ry = (uint16_t)p[7] << 4;
            } else {
                t->sh.state.lx = (uint16_t)(p[4] | ((uint16_t)p[5] << 8)) & 0x0FFFu;
                t->sh.state.ly = (uint16_t)(p[6] | ((uint16_t)p[7] << 8)) & 0x0FFFu;
                t->sh.state.rx = (uint16_t)(p[8] | ((uint16_t)p[9] << 8)) & 0x0FFFu;
                t->sh.state.ry = (uint16_t)(p[10] | ((uint16_t)p[11] << 8)) & 0x0FFFu;
            }
        }
    } else if (type == T_NEUTRAL && len == 0) {
        t->sh.state_frames++;
        t->sh.state.buttons = 0u;
        t->sh.state.lx = t->sh.state.ly = 0x800u;
        t->sh.state.rx = t->sh.state.ry = 0x800u;
    } else {
        vsh_push(&t->sh, type, p, len, seq, TR_CDC);
    }
    t->sh.last_seq = seq;
    t->sh.last_type = type;
    t->sh.frames++;
    t->sh.crc_err = t->stc.err_crc;
    t->sh.drop_ev = t->stc.err_drop;
}

static void tick_init(tick_t *t) {
    memset(t, 0, sizeof(*t));
    v3_session_init(&t->vs);
    t->sh.state.lx = t->sh.state.ly = 0x800u;
    t->sh.state.rx = t->sh.state.ry = 0x800u;
    t->sh.state_accept = 1;
    t->tr_last = TR_UART;
    t->tx.wired_profile = 1; // 既定HID-only
    parser_init(&t->pu, vcb_uart, t, &t->stu);
    parser_init(&t->pc, vcb_cdc, t, &t->stc);
    baud_lock_reset(&t->hlock);
}

// FW flush_outbox相当: queue順に要求元輸送へ (dual禁止)
static void vflush(tick_t *t) {
    for (uint8_t i = 0u; i < t->vs.ob_n; i++) {
        v3_ob_t *o = &t->vs.ob[i];
        if (o->act == ACT_SEND_STATUS) {
            uint8_t pl[7];
            v3_pack_status(0, t->sh.last_seq, (uint16_t)t->sh.crc_err,
                           (uint16_t)t->sh.drop_ev, t->vs.errcode, pl);
            vtx_send(&t->tx, T_STATUS, pl, 7, t->tr_last);
        } else if (o->act == ACT_SEND_PONG) {
            vtx_send(&t->tx, T_PONG, &o->arg, 1, t->tr_last);
        } else if (o->act == ACT_SEND_HELLO_ACK) {
            uint8_t ack[4] = { PROTO_VER, FW_MAJOR, FW_MINOR, t->vs.result };
            vtx_send(&t->tx, T_HELLO_ACK, ack, 4, t->tr_last);
        } else if (o->act == ACT_SEND_PLAYER_INFO) {
            uint8_t pi[2] = { t->vs.player_sent_lamp, t->vs.player_sent_flags };
            vtx_send(&t->tx, T_PLAYER_INFO, pi, 2, t->tr_last);
        } else if (o->act == ACT_SEND_RUMBLE) {
            uint8_t ra[2] = { t->vs.rumble_sent_l, t->vs.rumble_sent_r };
            vtx_send(&t->tx, T_RUMBLE, ra, 2, t->tr_last);
        } else if (o->act == ACT_SEND_COLOR_INFO) {
            uint8_t c[12];
            memset(c, 0x11, sizeof(c));
            vtx_send(&t->tx, T_COLOR_INFO, c, 12, t->tr_last);
        }
    }
    t->vs.ob_n = 0u;
}

// FW exec_fx相当 (副作用は記録のみ)。BAUD_SETはUART路のみ切替扱い。
static int vfx_baud_req = -1;
static void vexec(tick_t *t) {
    if (t->vs.fx == FX_BAUD_SET) vfx_baud_req = (int)t->vs.fx_arg;
    t->vs.fx = FX_NONE;
}

// FW poll_tick相当 (CDC pump→mirror→drain→flush→neutral)
static void vtick(tick_t *t, const uint8_t *cdc_bytes, size_t cdc_n,
                  const uint8_t *uart_bytes, size_t uart_n) {
    // CDC pump (connected時のみ・初回resync・idle resync)
    if (t->tx.cdc_connected && !t->tx.wired_profile) {
        if (t->cdc_fed && t->cdc_idle_gated &&
            (int32_t)(t->now - t->cdc_last_ms) >= (int32_t)SEQ_RESYNC_IDLE_MS) {
            link_stats_seq_resync(&t->stc);
            t->cdc_idle_gated = 0;
        }
        if (cdc_bytes != NULL && cdc_n > 0) {
            uint32_t f0, f1;
            if (!t->cdc_fed) { link_stats_seq_resync(&t->stc); t->cdc_fed = 1; }
            f0 = t->sh.frames;
            parser_feed_buf(&t->pc, cdc_bytes, cdc_n);
            f1 = t->sh.frames;
            if (f1 != f0) {
                t->cdc_last_ms = t->now;
                t->cdc_idle_gated = 1;
                t->tr_last = TR_CDC;
            }
        }
    }
    // UART pump (DMA相当。hunt内蔵cb)
    if (uart_bytes != NULL && uart_n > 0)
        parser_feed_buf(&t->pu, uart_bytes, uart_n);
    t->tx.uart_locked = t->uart_hlocked;
    // m3 mirror
    t->sh.state_accept = t->vs.state_accept ? 1 : 0;
    // drain gate (FW poll_tickと同順序: msg毎にtr→dispatch→exec→当該msg分flush)
    {
        int drained = 0;
        int open = t->uart_hlocked || (t->tx.cdc_connected && !t->tx.wired_profile);
        if (open) {
            for (;;) {
                ib_msg_t cur;
                int empty;
                uint8_t ob_base;
                empty = (t->sh.ib_r == t->sh.ib_w);
                if (!empty) {
                    cur = t->sh.ib[t->sh.ib_r];
                    t->sh.ib_r = (uint8_t)((t->sh.ib_r + 1u) % IB_N);
                }
                if (empty) break;
                drained = 1;
                t->tr_last = (cur.tr == TR_CDC) ? TR_CDC : TR_UART;
                ob_base = t->vs.ob_n;
                (void)v3_on_frame(&t->vs, cur.type, cur.payload, cur.len, cur.seq);
                vexec(t);
                while (t->vs.ob_n > ob_base) {
                    v3_ob_t o = t->vs.ob[ob_base];
                    uint8_t k;
                    for (k = ob_base; k + 1u < t->vs.ob_n; k++) {
                        t->vs.ob[k] = t->vs.ob[k + 1u];
                    }
                    t->vs.ob_n--;
                    if (o.act == ACT_SEND_STATUS) {
                        uint8_t pl[7];
                        v3_pack_status(0, t->sh.last_seq, (uint16_t)t->sh.crc_err,
                                       (uint16_t)t->sh.drop_ev, t->vs.errcode, pl);
                        vtx_send(&t->tx, T_STATUS, pl, 7, t->tr_last);
                    } else if (o.act == ACT_SEND_PONG) {
                        vtx_send(&t->tx, T_PONG, &o.arg, 1, t->tr_last);
                    } else if (o.act == ACT_SEND_HELLO_ACK) {
                        uint8_t ack[4] = { PROTO_VER, FW_MAJOR, FW_MINOR, t->vs.result };
                        vtx_send(&t->tx, T_HELLO_ACK, ack, 4, t->tr_last);
                    } else if (o.act == ACT_SEND_PLAYER_INFO) {
                        uint8_t pi[2] = { t->vs.player_sent_lamp, t->vs.player_sent_flags };
                        vtx_send(&t->tx, T_PLAYER_INFO, pi, 2, t->tr_last);
                    } else if (o.act == ACT_SEND_RUMBLE) {
                        uint8_t ra[2] = { t->vs.rumble_sent_l, t->vs.rumble_sent_r };
                        vtx_send(&t->tx, T_RUMBLE, ra, 2, t->tr_last);
                    } else if (o.act == ACT_SEND_COLOR_INFO) {
                        uint8_t c[12];
                        memset(c, 0x11, sizeof(c));
                        vtx_send(&t->tx, T_COLOR_INFO, c, 12, t->tr_last);
                    }
                }
            }
        }
        if (drained) { /* trはmsg毎に設定済み */ }
        else {
            if (t->tx.cdc_connected && !t->tx.wired_profile) t->tr_last = TR_CDC;
            else if (t->uart_hlocked) t->tr_last = TR_UART;
        }
    }
    vflush(t);
    // timeout-neutral (適用STATE/NEUTRALのみ延長)
    if (t->sh.state_frames != t->last_state_frames) {
        t->last_state_frames = t->sh.state_frames;
        t->last_state_ms = t->now;
        t->neutral_hold = 0;
    } else if (!t->neutral_hold &&
               (int32_t)(t->now - t->last_state_ms) >= (int32_t)TIMEOUT_NEUTRAL_MS) {
        if (t->sh.state.buttons != 0u || t->sh.state.lx != 0x800u ||
            t->sh.state.ly != 0x800u || t->sh.state.rx != 0x800u ||
            t->sh.state.ry != 0x800u) {
            t->sh.state.buttons = 0u;
            t->sh.state.lx = t->sh.state.ly = 0x800u;
            t->sh.state.rx = t->sh.state.ry = 0x800u;
        }
        t->neutral_hold = 1;
    }
    t->now++;
}

static size_t mk(uint8_t *out, uint8_t type, const uint8_t *pl, uint8_t len, uint8_t seq) {
    return frame_build(out, type, pl, len, seq);
}
static void put32(uint8_t *p, uint32_t b) {
    p[0] = (uint8_t)(b & 0xFFu); p[1] = (uint8_t)((b >> 8) & 0xFFu);
    p[2] = (uint8_t)((b >> 16) & 0xFFu); p[3] = (uint8_t)((b >> 24) & 0xFFu);
}

int main(void) {
    printf("[V0] 有線profile旧等価: HID時はCDC不応答・UART hunt経路のみ\n");
    {
        tick_t t;
        uint8_t f[16], g[16];
        uint8_t pl[8];
        tick_init(&t); // wired_profile=1 (既定HID)
        t.tx.cdc_connected = 1; // 物理CDCに何か繋がってもprofileが塞ぐ
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        mk(f, T_STATE, pl, 8, 0x00);
        mk(g, T_PING, NULL, 0, 0x01);
        vtick(&t, f, sizeof(f) - 3, NULL, 0); // CDCへSTATE相当を流しても無視
        vtick(&t, g, 5, NULL, 0);
        CHECK(t.sh.frames == 0 && t.tx.n == 0, "HID profileではCDC bytes無視・TXなし");
        // UART路: 2連続でhunt確定→STATE適用→PING応答はUARTへ
        tick_init(&t);
        {
            uint8_t s1[16], s2[16], pg[8];
            size_t n1, n2, np;
            put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
            n1 = mk(s1, T_STATE, pl, 8, 0x00);
            n2 = mk(s2, T_STATE, pl, 8, 0x01);
            np = mk(pg, T_PING, NULL, 0, 0x02);
            vtick(&t, NULL, 0, s1, n1);
            CHECK(t.uart_hlocked == 0 && t.sh.state.buttons == 0u, "hunt 1発目は未確定・STATE未適用");
            vtick(&t, NULL, 0, s2, n2);
            CHECK(t.uart_hlocked == 1 && t.sh.state.buttons == BTN_A, "hunt 2連続で確定・STATE適用");
            vtick(&t, NULL, 0, pg, np);
            CHECK(t.tx.n == 1 && t.tx.rec[0].type == T_PONG && t.tx.rec[0].tr == TR_UART,
                  "PING応答はUARTへ (PONG/TR_UART)");
            CHECK(t.tx.rec[0].seq == 0u, "UART TX SEQは0開始");
        }
    }
    printf("[V1] 無線profile: CDC単発実行・応答はCDCへ・UART hunt非接触\n");
    {
        tick_t t;
        uint8_t f[16], g[8];
        uint8_t pl[8];
        size_t nf, ng;
        tick_init(&t);
        t.tx.wired_profile = 0; // 無線=CDC-only
        t.tx.cdc_connected = 1;
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        nf = mk(f, T_STATE, pl, 8, 0x00);
        ng = mk(g, T_PING, NULL, 0, 0x01);
        vtick(&t, f, nf, NULL, 0);
        CHECK(t.sh.state.buttons == BTN_A && t.uart_hlocked == 0, "CDC STATEはhuntなし単発適用・UART hunt不変");
        vtick(&t, g, ng, NULL, 0);
        CHECK(t.tx.n == 1 && t.tx.rec[0].type == T_PONG && t.tx.rec[0].tr == TR_CDC,
              "PING応答はCDCへ (PONG/TR_CDC)");
        CHECK(t.tx.rec[0].seq == 0u, "CDC TX SEQはUARTと独立0開始");
    }
    printf("[V2] 同一tick混在: 応答は各msgの投入路へ (CDC PONGがUARTへ誤送されない)\n");
    {
        tick_t t;
        uint8_t s1[16], g1[8], g2[8];
        uint8_t pl[8];
        size_t n1, np1, np2;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        n1 = mk(s1, T_STATE, pl, 8, 0x10);
        np1 = mk(g1, T_PING, NULL, 0, 0x11);
        np2 = mk(g2, T_PING, NULL, 0, 0x02); // CDC路はSEQ基準分離・0x02で新規
        // UART確定のため先に2発UART STATE (SEQ 0x00/0x01)
        {
            uint8_t u1[16], u2[16];
            size_t m1, m2;
            m1 = mk(u1, T_STATE, pl, 8, 0x00);
            m2 = mk(u2, T_STATE, pl, 8, 0x01);
            vtick(&t, NULL, 0, u1, m1);
            vtick(&t, NULL, 0, u2, m2);
        }
        CHECK(t.uart_hlocked == 1, "UART確定済み");
        t.tx.n = 0;
        // 同一tickにUART PING + CDC PINGを同時投入
        {
            uint8_t both[32];
            memcpy(both, g1, np1);
            // FW pump順序通り: CDC pump→UART pump (別parser・tr別inbox)。
            // CDC側はSEQ基準分離のため初回resync済み想定で基準捨て。
            // 投入順=CDC→UARTのため、応答順もCDC→UARTになる (順序保持の証拠)。
            link_stats_seq_resync(&t.stc);
            parser_feed_buf(&t.pc, g2, np2);
            parser_feed_buf(&t.pu, g1, np1);
            // drain相当 (FWと同順序: msg毎flush。vdrain相当をinline展開)
            while (t.sh.ib_r != t.sh.ib_w) {
                ib_msg_t cur = t.sh.ib[t.sh.ib_r];
                uint8_t ob_base = t.vs.ob_n;
                t.sh.ib_r = (uint8_t)((t.sh.ib_r + 1u) % IB_N);
                t.tr_last = (cur.tr == TR_CDC) ? TR_CDC : TR_UART;
                (void)v3_on_frame(&t.vs, cur.type, cur.payload, cur.len, cur.seq);
                vexec(&t);
                while (t.vs.ob_n > ob_base) {
                    v3_ob_t o = t.vs.ob[ob_base];
                    uint8_t k;
                    for (k = ob_base; k + 1u < t.vs.ob_n; k++) {
                        t.vs.ob[k] = t.vs.ob[k + 1u];
                    }
                    t.vs.ob_n--;
                    if (o.act == ACT_SEND_PONG) {
                        vtx_send(&t.tx, T_PONG, &o.arg, 1, t.tr_last);
                    } else {
                        vtx_send(&t.tx, 0xFF, NULL, 0, t.tr_last);
                    }
                }
            }
            vflush(&t);
            (void)both; (void)n1; (void)np1; (void)np2;
        }
        CHECK(t.tx.n == 2, "PONG 2件送出");
        if (t.tx.n == 2) {
            CHECK(t.tx.rec[0].tr == TR_CDC && t.tx.rec[1].tr == TR_UART,
                  "CDC分が先・UART分が後 (投入順通り・誤送なし)");
            CHECK(t.tx.rec[0].seq == 0u && t.tx.rec[1].seq == 0u,
                  "輸送別SEQ (各0開始・独立カウンタ)");
        }
    }
    printf("[V2b] 逆順混在 (UART-first): 応答は各msgの投入路へ\n");
    {
        tick_t t;
        uint8_t g1[8], g2[8];
        uint8_t pl[8];
        size_t np1, np2;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        np1 = mk(g1, T_PING, NULL, 0, 0x02); // UART路 (確定後のSEQ継続)
        np2 = mk(g2, T_PING, NULL, 0, 0x02); // CDC路 (SEQ基準分離)
        // UART確定のため先に2発UART STATE (SEQ 0x00/0x01)
        {
            uint8_t u1[16], u2[16];
            size_t m1, m2;
            m1 = mk(u1, T_STATE, pl, 8, 0x00);
            m2 = mk(u2, T_STATE, pl, 8, 0x01);
            vtick(&t, NULL, 0, u1, m1);
            vtick(&t, NULL, 0, u2, m2);
        }
        CHECK(t.uart_hlocked == 1, "UART確定済み (V2b)");
        t.tx.n = 0;
        // 逆順投入: UART→CDC (V2はCDC→UART)。FW pump順序とは逆のinterleave。
        link_stats_seq_resync(&t.stc);
        parser_feed_buf(&t.pu, g1, np1);
        parser_feed_buf(&t.pc, g2, np2);
        while (t.sh.ib_r != t.sh.ib_w) {
            ib_msg_t cur = t.sh.ib[t.sh.ib_r];
            uint8_t ob_base = t.vs.ob_n;
            t.sh.ib_r = (uint8_t)((t.sh.ib_r + 1u) % IB_N);
            t.tr_last = (cur.tr == TR_CDC) ? TR_CDC : TR_UART;
            (void)v3_on_frame(&t.vs, cur.type, cur.payload, cur.len, cur.seq);
            vexec(&t);
            while (t.vs.ob_n > ob_base) {
                v3_ob_t o = t.vs.ob[ob_base];
                uint8_t k;
                for (k = ob_base; k + 1u < t.vs.ob_n; k++) {
                    t.vs.ob[k] = t.vs.ob[k + 1u];
                }
                t.vs.ob_n--;
                if (o.act == ACT_SEND_PONG) {
                    vtx_send(&t.tx, T_PONG, &o.arg, 1, t.tr_last);
                } else {
                    vtx_send(&t.tx, 0xFF, NULL, 0, t.tr_last);
                }
            }
        }
        vflush(&t);
        CHECK(t.tx.n == 2, "PONG 2件送出 (V2b)");
        if (t.tx.n == 2) {
            CHECK(t.tx.rec[0].tr == TR_UART && t.tx.rec[1].tr == TR_CDC,
                  "UART分が先・CDC分が後 (逆順でも誤送なし)");
            CHECK(t.tx.rec[0].seq == 0u && t.tx.rec[1].seq == 0u,
                  "輸送別SEQ (各0開始・独立カウンタ)");
        }
    }
    printf("[V3] CDC idle resync: 1000ms無通信境界でSEQ基準捨て・累計保持\n");
    {
        tick_t t;
        uint8_t f[8], g[8];
        size_t nf, ng;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        nf = mk(f, T_PING, NULL, 0, 0x00);
        ng = mk(g, T_PING, NULL, 0, 0x05); // 欠番ありだが境界後は初回扱い
        vtick(&t, f, nf, NULL, 0);
        CHECK(t.stc.err_drop == 0, "初回は欠番計数なし");
        // 1000tick無通信
        for (int i = 0; i < 1000; i++) vtick(&t, NULL, 0, NULL, 0);
        vtick(&t, g, ng, NULL, 0);
        CHECK(t.stc.err_drop == 0, "境界後は新初回として欠番計数なし");
        CHECK(t.sh.frames == 2, "frame受理2件");
    }
    printf("[V4] UART復帰優先: UART live後はTR_UARTへ戻る・UART SEQ独立\n");
    {
        tick_t t;
        uint8_t u[16], g[8];
        uint8_t pl[8];
        size_t nu, ng;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        put32(pl, BTN_B); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        nu = mk(u, T_STATE, pl, 8, 0x00);
        ng = mk(g, T_PING, NULL, 0, 0x02);
        vtick(&t, NULL, 0, u, nu); // UART 1発目 (未確定)
        {
            uint8_t u2[16];
            size_t m2 = mk(u2, T_STATE, pl, 8, 0x01);
            vtick(&t, NULL, 0, u2, m2); // UART確定
        }
        vtick(&t, NULL, 0, g, ng); // UART PING
        CHECK(t.tx.n >= 1 && t.tx.rec[t.tx.n - 1].tr == TR_UART, "UART確定後の応答はUARTへ");
    }
    printf("[V5] BAUD_SET輸送帰属: CDC要求はUART切替のみ・ACKはCDCへ\n");
    {
        tick_t t;
        uint8_t b[8];
        size_t nb;
        uint8_t pl[1] = { 0x00 };
        vfx_baud_req = -1;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        nb = mk(b, T_BAUD_SET, pl, 1, 0x00);
        vtick(&t, b, nb, NULL, 0);
        CHECK(vfx_baud_req == 0, "BAUD_SET受理・UART切替要求記録");
        CHECK(t.tx.n == 1 && t.tx.rec[0].type == T_STATUS && t.tx.rec[0].tr == TR_CDC,
              "ACK STATUSはCDCへ");
        CHECK(t.uart_hlocked == 0, "CDC BAUD_SETでUART hunt状態不変");
    }
    printf("[V6] timeout-neutral: CDC STATE 200ms無受信で中立化\n");
    {
        tick_t t;
        uint8_t f[16];
        uint8_t pl[8];
        size_t nf;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        nf = mk(f, T_STATE, pl, 8, 0x00);
        vtick(&t, f, nf, NULL, 0);
        CHECK(t.sh.state.buttons == BTN_A, "CDC STATE適用");
        for (int i = 0; i < 200; i++) vtick(&t, NULL, 0, NULL, 0);
        CHECK(t.sh.state.buttons == 0u && t.sh.state.lx == 0x800u, "200msで中立化");
        CHECK(t.neutral_hold == 1, "neutral_hold成立");
    }
    printf("[V7] UNSUPPORTED mirror: HELLO異版後はSTATE拒否 (両輸送)\n");
    {
        tick_t t;
        uint8_t h[8], s[16];
        uint8_t plh[2] = { 0x99, 0x00 };
        uint8_t pl[8];
        size_t nh, ns;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        nh = mk(h, T_HELLO, plh, 2, 0x00);
        vtick(&t, h, nh, NULL, 0); // pump+drain: HELLOはinbox経由でdispatchへ
        vtick(&t, NULL, 0, NULL, 0); // mirror適用tick (FW poll順序: pump→mirror→drain)
        CHECK(t.vs.state_accept == 0, "異版でstate_accept=false");
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        ns = mk(s, T_STATE, pl, 8, 0x01);
        vtick(&t, s, ns, NULL, 0);
        CHECK(t.sh.state.buttons == 0u, "CDC STATE拒否");
        vtick(&t, NULL, 0, s, ns); // UART路でも拒否 (mirror後)
        CHECK(t.sh.state.buttons == 0u, "UART STATE拒否 (mirror)");
    }
    printf("[V8] 背圧nonblock: CDC未接続時はTX捨て・blockなし\n");
    {
        tick_t t;
        uint8_t g[8];
        size_t ng;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 0; // 未接続
        ng = mk(g, T_PING, NULL, 0, 0x00);
        // gate閉のためdispatch自体が閉じる→TXなし・ハングなし
        vtick(&t, g, ng, NULL, 0);
        CHECK(t.tx.n == 0, "CDC未接続では応答なし・blockなし");
    }
    printf("[V9] tick queue温存: PLAYER_INFO tick分はdrain後flushで送出\n");
    {
        tick_t t;
        uint8_t g[8];
        size_t ng;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 1;
        // FW poll順序通り: player tick queue (drain前) + CDC PING drain。
        // tick queue分はbase以前として残り、最終vflushで送出される。
        t.vs.player_valid = 1;
        t.vs.player_lamp = 3;
        t.vs.player_flags = 0x07;
        v3_player_tick(&t.vs); // ACT_SEND_PLAYER_INFOをqueue
        ng = mk(g, T_PING, NULL, 0, 0x00);
        vtick(&t, g, ng, NULL, 0);
        CHECK(t.tx.n == 2, "PLAYER_INFO+PONG 2件送出");
        if (t.tx.n == 2) {
            // FW順序: msg毎flushが先 (PONG) → tick queue分は最終flush (PLAYER_INFO)。
            CHECK(t.tx.rec[0].type == T_PONG && t.tx.rec[0].tr == TR_CDC,
                  "当該msg分はmsg毎flushでCDCへ");
            CHECK(t.tx.rec[1].type == T_PLAYER_INFO && t.tx.rec[1].tr == TR_CDC,
                  "tick queue分は最終flushでCDCへ");
        }
    }
    printf("[V10] hunt purge温存: UART確定時にCDC inbox要求が消えない\n");
    {
        tick_t t;
        uint8_t g[8], u1[16], u2[16];
        uint8_t pl[8];
        size_t ng, m1, m2;
        tick_init(&t);
        t.tx.wired_profile = 0;
        t.tx.cdc_connected = 0;
        // CDC PINGをparser直feedでinboxへ (drainを回さず滞留)。
        ng = mk(g, T_PING, NULL, 0, 0x00);
        parser_feed_buf(&t.pc, g, ng);
        CHECK(t.sh.ib_w == 1 && t.sh.ib_r == 0 && t.uart_hlocked == 0,
              "CDC要求がinbox滞留・UART未確定");
        // UART hunt確定をparser直feedで再現 (drainなし・purgeのみ観測)。
        // FW修正前は全purge (ib_r=ib_w) でCDC要求が消えた。
        put32(pl, BTN_A); pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
        m1 = mk(u1, T_STATE, pl, 8, 0x00);
        m2 = mk(u2, T_STATE, pl, 8, 0x01);
        parser_feed_buf(&t.pu, u1, m1);
        parser_feed_buf(&t.pu, u2, m2);
        CHECK(t.uart_hlocked == 1, "UART hunt確定");
        CHECK(t.sh.ib_w == 1 && t.sh.ib_r == 0 &&
              t.sh.ib[0].tr == TR_CDC && t.sh.ib[0].type == T_PING,
              "CDC要求がpurge後も温存 (tr/type保持)");
        // gate開 (CDC接続+UART確定) → vtick drainでCDC PONGがCDCへ
        t.tx.cdc_connected = 1;
        {
            int n0 = t.tx.n;
            vtick(&t, NULL, 0, NULL, 0);
            CHECK(t.tx.n == n0 + 1 && t.tx.rec[t.tx.n - 1].type == T_PONG &&
                  t.tx.rec[t.tx.n - 1].tr == TR_CDC, "温存CDC要求の応答はCDCへ");
        }
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
