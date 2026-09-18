// main.c -- pico-bcon 統合FW (USB + Classic BT + BLE wake)。
// PC →(UART1)→ Pico →(USB-HID / Classic BT)→ Switch 1。BLEはwake取込再生のみ。
//
// 構成:
//   Core1: UART1 DMA排出＋v3 parser＋mutex pushのみ (PoC確定)。
//          STATE適用は state_accept (UNSUPPORTED時のみfalse)、NEUTRALは常時。
//          STATE以外の受信frameは inbox (8slot) へ。Flash/USB/BT禁止。
//   Core0: BTstack run loop＋TinyUSB＋CYW43 (wakeconと同lib・同初期化順)。
//          1ms timerで inbox drain→dispatch→FX実行→outbox flush→pack→usb。
//          無線の要否は WIRED_MODE (Flash保存) で管理 (link_radio_update)。
// 合成Flash試験 (PoC) は廃止。TLV実書込がFlash活動になる。

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/flash.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/exception.h" // HardFault報告の掛け替えに使用
#include "btstack.h"
#include "btstack_tlv_flash_bank.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "pico/btstack_flash_bank.h"

#include "protocol.h"
#include "baud.h"
#include "dispatch.h"
#include "pack.h"
#include "pokecon.h"
#include "spi.h"
#include "usb_wired.h"
#include "bt/hid.h"
#include "bt/link.h"
#include "bt/store.h"
#include "bt/cap.h"
#include "bt/switch_hid.h"
#include "bt/bt_compat.h"

// ---- 前提ピン・速度 (spec §2: 既定 GP4/5・UART1・1Mbps) ----
#define LOG_UART      uart0
#define LOG_BAUD      115200
#define LOG_TX_PIN    0
#define LOG_RX_PIN    1

#define DATA_UART     uart1
#ifndef POC_DATA_BAUD
#define POC_DATA_BAUD 1000000
#endif
#define DATA_TX_PIN   4
#define DATA_RX_PIN   5

#define TIMEOUT_NEUTRAL_MS 200u // spec §8 (直近STATEから200ms無受信で全解放)

// ---- DMA ring: 16KB (PoC確定) ----
#define RING_BITS 14
#define RING_SIZE (1u << RING_BITS)
static uint8_t dma_ring[RING_SIZE] __attribute__((aligned(RING_SIZE)));

// ---- Core1 -> Core0 inbox (STATE/NEUTRAL以外の受信frame) ----
#define IB_N 8
typedef struct {
    uint8_t type, len, seq;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} ib_msg_t;

// ---- Core1 -> Core0 共有状態 (mutex保護。Core1はpushのみ) ----
typedef struct {
    ctrl_state_t state;   // 最新STATE (u32＋sticks)
    uint8_t  last_seq;
    uint8_t  last_type;
    uint8_t  perrcode;    // parser errcode mirror (STATUS用)
    uint32_t frames;
    uint32_t crc_err;
    uint32_t drop_ev;
    uint32_t iters;
    uint32_t overruns;
    uint32_t boot_code;   // 0=boot中 1=selftest PASS 2=FAIL 3=走行中
    uint32_t boot_detail;
    volatile bool state_accept; // Core0書込・Core1読込 (既定true)
    // baud hunt (B-0) の跨コア信号。全てg_m mutex区間で受渡し。
    volatile bool bt_session;    // Core0書込 (hid open中のみtrue)・Core1読込
    volatile bool baud_locked;   // Core1書込 (2連続有効frameで確定)・Core0読込
    volatile uint8_t baud_idx;   // 現slotの表index (Core1書込・Core0読込)
    volatile bool baud_save_req; // Core1→Core0のBCBR保存要求 (pollがdrain)
    uint8_t baud_save_idx;
    // BAUD_SET (B §3) の跨コア信号。切替実行はCore1 (DMA/UART所有)。
    volatile bool baudx_req;     // Core0→Core1の切替要求 (pollが発行)
    uint8_t baudx_idx;           // 目標index
    volatile bool baudx_reverted; // Core1→Core0の自動復帰報告 (pollがdrain)
    uint8_t baudx_rev_idx;        // 復帰先index
    uint8_t baud_slots[BAUD_N];  // Core0が起動時に組むsweep順 (slot0=last-good)
    uint8_t baud_nslots;
    ib_msg_t ib[IB_N];
    volatile uint8_t ib_w, ib_r;
    uint32_t ib_drop;
} bcon_shared_t;

static mutex_t g_m;
static bcon_shared_t g_s;
static volatile bool g_core1_booted = false;

// ---- USB/BT層への供給 (所有元はCore0。各層は読みのみ) ----
uint8_t bcon_btn[3];
uint16_t bcon_lx = 0x800u, bcon_ly = 0x800u, bcon_rx = 0x800u, bcon_ry = 0x800u;
uint8_t bcon_mac[6]; // 応答順 (初期化で反転済み。BT addrと同一機器)

// ---- RUMBLE受信 (BT 0x10復号amp保持。変化時のみRUMBLE frameでPCへ転送) ----
uint32_t bcon_usb_rumble_n;
uint32_t bcon_bt_rumble_n;

// ---- v3セッション (dispatch所有。Core0のみ触る) ----
static v3_session_t g_vs;
static uint8_t g_tx_seq; // Pico->PC 方向SEQ (方向独立・mod256)
static bool s_wired = true;
static bool s_baud_tx_ok; // hunt確定までUART1 TX抑制 (B-0)。起動時に初期化。
static bool s_baud_was_locked; // re-hunt行の検出用 (起動時に初期化)。
static uint32_t s_boot_baud; // 起動baud (ready表示用。BCBR/sweep解決済み)。
static bool s_neutral_hold; // timeout-neutral中 (STATUS bit2)
static uint32_t s_last_state_ms;
static uint32_t s_last_frames;
static uint32_t s_rumble_reported; // 前回STATUS時のrumble合計
static bool s_wdt_recovered;
static bool s_bt_init; // BTstack初期化済み (無線起動時のみtrue)
static uint32_t s_reboot_at; // 0以外は指定msでwatchdog reboot (WIRED切替適用)
static uint32_t s_bootsel_at; // 0以外は指定msでreset_usb_boot (開発用BOOTSEL)

// bt_compat.h (probe_line) の実体。UART0 printf。
void probe_line(const char *s) {
    if (s != NULL) {
        printf("%s\n", s);
    }
}

// ---- Fault可視化 ----
// hid open直後のWDT発火がハングかFault停止かを切り分ける。
// Fault時は積まれたPC/LR等を生UARTへ出す (stdioはIRQ停止時に詰む)。
// 復帰せずぶら下がり、WDTが回収する (wdt=1)。
static void fault_raw_putc(char c)
{
    while (!uart_is_writable(LOG_UART)) {
    }
    uart_putc_raw(LOG_UART, (uint8_t)c);
}

static void fault_raw_hex(uint32_t v)
{
    int k;
    for (k = 28; k >= 0; k -= 4) {
        uint32_t n = (v >> (uint32_t)k) & 0xFu;
        fault_raw_putc((char)(n < 10u ? (uint32_t)'0' + n : (uint32_t)'a' + (n - 10u)));
    }
}

static void fault_raw_str(const char *s)
{
    while (*s != '\0') {
        fault_raw_putc(*s++);
    }
}

/* EXC_RETURN(bit2)でMSP/PSPを選び、CFSR/HFSR/MMFAR/BFARを出す。
 * snprintf/printfは使わない (溢れスタック上での再フォルト回避)。 */
__attribute__((used)) static void fault_entry_ex(uint32_t excret, uint32_t *sp)
{
    /* sp: r0 r1 r2 r3 r12 lr pc psr */
    fault_raw_str("FAULT lr=");
    fault_raw_hex(excret);
    fault_raw_str(" pc=");
    fault_raw_hex(sp[6]);
    fault_raw_str(" slr=");
    fault_raw_hex(sp[5]);
    fault_raw_str(" cfsr=");
    fault_raw_hex(*((volatile uint32_t *)0xE000ED28u));
    fault_raw_str(" hfsr=");
    fault_raw_hex(*((volatile uint32_t *)0xE000ED2Cu));
    fault_raw_str(" mmfar=");
    fault_raw_hex(*((volatile uint32_t *)0xE000ED34u));
    fault_raw_str(" bfar=");
    fault_raw_hex(*((volatile uint32_t *)0xE000ED38u));
    fault_raw_putc('\n');
    while (1) {
        tight_loop_contents();
    }
}

/* SDKのベクタはHardFault_Handlerを見ない (isr_invalid無言停止) ため、
 * exception_set_exclusive_handlerで掛けるのが正規手段。
 * C関数化するとプロローグpushでSPがずれ積みフレームを外すためnaked必須。 */
__attribute__((naked)) static void hardfault_reporter(void)
{
    __asm volatile (
        "mrs r1, msp\n"   /* r1 = sp候補 */
        "mov r0, lr\n"    /* r0 = EXC_RETURN */
        "tst r0, #4\n"    /* bit2: 0=MSP, 1=PSP */
        "beq 1f\n"
        "mrs r1, psp\n"
        "1: b fault_entry_ex\n" /* tail-call (r0=excret, r1=sp)。pushなし */
    );
}

/* スタックガード (MSPLIM)。
 * SDK既定では無効 (PICO_USE_STACK_GUARDS=0) のため手で入れる。
 * 溢れの無言破壊をSTKOF確定Fault化する。Core0/Core1それぞれの自コアで呼ぶ。 */
extern uint32_t __StackBottom;
extern uint32_t __StackOneBottom;
static void stack_guard_install(uint32_t *bottom)
{
    __asm volatile ("msr msplim, %0" :: "r" (bottom));
}

// ---------------- Core1: parser callback (mutex pushのみ) ----------------

// ---------------- Core1: parser callback (mutex pushのみ) ----------------
static link_stats_t s_pst;
static parser_t s_parser;

// ---- baud hunt (B-0) のCore1側状態。非ブロッキング遷移のみ (WDT安全) ----
#define HUNT_DWELL_MS 150u // spec §1: 各slot 150ms試聴
#define BREAK_LOW_MS 20u // re-hunt要求BREAKの判定閾値 (通常traffic最長lowの
                          // 約78us@115200に対し十分なマージン)
typedef struct {
    uint8_t slots[BAUD_N];
    uint8_t nslots;
    uint8_t at;
    uint32_t dwell_until;
    baud_lock_t lock;
    uint16_t err_base;  // gap検出用 (err_crc baseline)
    uint16_t drop_base; // gap検出用 (err_drop baseline)
    bool locked;
    bool armed; // selftest合成frameをlock計数から外す錠 (init後にtrue)
    uint32_t brk_low_since; // RX-low継続の起点 (BREAK検出用)
    bool brk_fired;         // 現low-episodeで発火済み (再発火防止)
} hunt_t;
static hunt_t s_hunt;

// ---- BAUD_SET (B §3) のCore1側状態。切替実行はCore1 (DMA/UART所有)。
// Core0は要求 (baudx_req) の発行と保存drainのみ。
#define BAUDX_GUARD_MS 100u   // 旧rate ACK送出後の双方guard (B §3机上値)
#define BAUDX_REVERT_MS 2000u // 有効frameなき自動復帰 (B §3机上値)
typedef enum { BAUDX_IDLE = 0, BAUDX_ARMED, BAUDX_MOVED } baudx_phase_t;
typedef struct {
    baudx_phase_t phase;
    uint8_t target; // 目標index
    uint8_t prev;   // 切替前index (復帰先)
    uint8_t cur;    // 現rate index (Core1管理の正本)
    uint32_t at;    // 切替予定時刻 (ARMED)
    uint32_t revert_at;
    uint8_t adopt_n;      // 新rateの連続有効frame数 (2で確定保存)
    uint16_t err_base;    // adopt対の分断検出用
    uint16_t drop_base;
} baudx_t;
static baudx_t s_baudx;

static void bcon_frame_cb(uint8_t type, const uint8_t *p, uint8_t len,
                          uint8_t seq, void *user) {
    (void)user;
    mutex_enter_blocking(&g_m);
    if (!g_s.baud_locked && s_hunt.armed) {
        // hunt中 (B-0): 有効frame 2連続で確定。確定前のinboxは未確定baudの
        // 産物のため捨てる (selftest直後のpurgeと同義)。
        if (baud_lock_feed(&s_hunt.lock, true)) {
            g_s.baud_locked = true;
            g_s.baud_idx = s_hunt.slots[s_hunt.at];
            g_s.baud_save_req = true;
            g_s.baud_save_idx = g_s.baud_idx;
            g_s.ib_r = g_s.ib_w;
            s_hunt.locked = true;
        }
    }
    bool trusted = g_s.baud_locked;
    if (s_baudx.phase == BAUDX_MOVED && s_baudx.adopt_n < 2u) {
        // BAUD_SET確定待ち: 新rateの連続有効frameを数える (B §3の初回を
        // 2連続に硬化。単発fluke保存の防止。分断はloop側が割る)。
        s_baudx.adopt_n++;
        if (s_baudx.adopt_n >= 2u) {
            g_s.baud_save_req = true; // 確定保存 (Core0がdrain)
            g_s.baud_save_idx = s_baudx.cur;
        }
    }
    if (type == T_STATE && proto_state_len_ok(len)) {
        if (trusted && g_s.state_accept) {
            g_s.state.buttons =
                (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            if (len == 8) {
                g_s.state.lx = (uint16_t)p[4] << 4;
                g_s.state.ly = (uint16_t)p[5] << 4;
                g_s.state.rx = (uint16_t)p[6] << 4;
                g_s.state.ry = (uint16_t)p[7] << 4;
            } else { /* LEN=12: u16LE pairs, masked to 12 bits */
                g_s.state.lx = (uint16_t)(p[4] | ((uint16_t)p[5] << 8)) & 0x0FFFu;
                g_s.state.ly = (uint16_t)(p[6] | ((uint16_t)p[7] << 8)) & 0x0FFFu;
                g_s.state.rx = (uint16_t)(p[8] | ((uint16_t)p[9] << 8)) & 0x0FFFu;
                g_s.state.ry = (uint16_t)(p[10] | ((uint16_t)p[11] << 8)) & 0x0FFFu;
            }
        }
    } else if (type == T_NEUTRAL && len == 0) {
        // 安全停止はUNSUPPORTED下でも適用する (dispatchと同義)。
        // ただしhunt確定前は未確定baudのため適用しない。
        if (trusted) {
            g_s.state.buttons = 0u;
            g_s.state.lx = g_s.state.ly = 0x800u;
            g_s.state.rx = g_s.state.ry = 0x800u;
        }
    } else {
        // CONFIG/HELLO/PING等は inbox へ。Core0がdispatchする
        // (Flash書込を伴うためCore1では実行しない)。
        uint8_t nw = (uint8_t)((g_s.ib_w + 1u) % IB_N);
        if (nw == g_s.ib_r) {
            g_s.ib_drop++;
        } else {
            ib_msg_t *m = &g_s.ib[g_s.ib_w];
            m->type = type;
            m->len = (len > PROTO_MAX_PAYLOAD) ? PROTO_MAX_PAYLOAD : len;
            m->seq = seq;
            if (p != NULL && m->len > 0) memcpy(m->payload, p, m->len);
            g_s.ib_w = nw;
        }
    }
    g_s.last_seq = seq;
    g_s.last_type = type;
    g_s.frames++;
    g_s.crc_err = s_pst.err_crc;
    g_s.drop_ev = s_pst.err_drop;
    mutex_exit(&g_m);
}

#if POKECON_INPUT
// PokeCon mode: line buffer -> pokecon_parse_line() -> g_s.state.
// Applies under the same trust rules as the v3 fast path (STATE needs
// state_accept, END/NEUTRAL always, malformed counted like err_crc).
// Frames advance so the 200ms timeout-neutral path keeps working.
static pokecon_linebuf_t s_pokelb;

static void pokecon_drain_byte(uint8_t b) {
    ctrl_state_t ns;
    pokecon_rc_t rc;
    bool done;
    mutex_enter_blocking(&g_m);
    ns = g_s.state;
    mutex_exit(&g_m);
    done = pokecon_linebuf_feed(&s_pokelb, (char)b, &ns, &rc);
    if (!done) {
        return;
    }
    mutex_enter_blocking(&g_m);
    if (rc == POKE_OK) {
        if (g_s.baud_locked && g_s.state_accept) {
            g_s.state = ns;
        }
    } else if (rc == POKE_END) {
        if (g_s.baud_locked) {
            g_s.state = ns;
        }
    } else {
        if (g_s.crc_err < 0xFFFFu) {
            g_s.crc_err++;
        }
    }
    if (rc != POKE_IGNORE) {
        g_s.frames++;
        g_s.last_type = (rc == POKE_END) ? T_NEUTRAL : T_STATE;
    }
    mutex_exit(&g_m);
}
#endif

static uint32_t core1_selftest(void) {
#if POKECON_INPUT
    {
        size_t k;
        const char *a = "10 08\r\n"; // A press, sticks held
        const char *e = "end\r\n";   // full neutral
        pokecon_linebuf_init(&s_pokelb);
        for (k = 0u; a[k] != '\0'; k++) {
            pokecon_drain_byte((uint8_t)a[k]);
        }
        if (g_s.state.buttons != BTN_A) return 0x10;
        if (g_s.state.lx != 0x800u) return 0x20;
        if (g_s.frames != 1u) return 0x30;
        for (k = 0u; e[k] != '\0'; k++) {
            pokecon_drain_byte((uint8_t)e[k]);
        }
        if (g_s.state.buttons != 0u || g_s.state.lx != 0x800u) return 0x40;
        if (g_s.frames != 2u) return 0x50;
        if (g_s.last_type != T_NEUTRAL) return 0x60;
        if (g_s.ib_w != 0u) return 0x70;
        return 0;
    }
#else
    uint8_t f[64];
    uint8_t pl[8];
    pl[0] = (uint8_t)(BTN_A & 0xFFu);
    pl[1] = (uint8_t)((BTN_A >> 8) & 0xFFu);
    pl[2] = 0; pl[3] = 0;
    pl[4] = pl[5] = pl[6] = pl[7] = 0x80;
    size_t n;
    n = frame_build(f, T_STATE, pl, 8, 0x10);
    parser_feed_buf(&s_parser, f, n);
    n = frame_build(f, T_NEUTRAL, NULL, 0, 0x11);
    parser_feed_buf(&s_parser, f, n);
    n = frame_build(f, T_PING, NULL, 0, 0x12);
    parser_feed_buf(&s_parser, f, n);
    if (g_s.frames != 3) return 0x10 + g_s.frames;
    if (s_pst.err_crc != 0 || s_pst.err_drop != 0) return 0x20;
    if (g_s.last_type != T_PING || g_s.last_seq != 0x12) return 0x30;
    if (g_s.state.buttons != 0u || g_s.state.lx != 0x800u) return 0x40;
    // PINGはinboxに届いている事 (STATE/NEUTRALはlive適用のためinbox外)。
    if (g_s.ib_w != 1u) return 0x50;
    return 0;
#endif
}

static int g_dma_ch = -1;
static uint32_t g_rd = 0;

// BREAK受信 (re-hunt要求): lock解除→sweep再開 (slot0=現rate)。
// 通常trafficはstop bitで必ずhighに戻るため、20ms以上のlowは
// 明示操作以外にあり得ない。UART非依存のためどのbaudでも検出できる。
// inboxは旧rateの産物のため捨てる。TXはCore0 pollが閉じる。
static void hunt_on_break(uint32_t now) {
    uint8_t order[BAUD_N];
    uint8_t n;
    uint8_t cur;
    uint8_t k;
    mutex_enter_blocking(&g_m);
    cur = g_s.baud_idx;
    mutex_exit(&g_m);
    if (!baud_idx_valid(cur)) {
        cur = 0u;
    }
    n = baud_sweep_order(cur, (uint32_t)POC_DATA_BAUD, order);
    if (n == 0u) {
        order[0] = 0u;
        n = 1u;
    }
    for (k = 0u; k < n; k++) {
        s_hunt.slots[k] = order[k];
    }
    s_hunt.nslots = n;
    s_hunt.at = 0u; // UARTは既にorder[0]=cur。切替不要、dwellのみ更新。
    s_hunt.locked = false;
    baud_lock_reset(&s_hunt.lock);
    s_hunt.err_base = s_pst.err_crc;
    s_hunt.drop_base = s_pst.err_drop;
    s_hunt.dwell_until = now + HUNT_DWELL_MS;
    s_baudx.phase = BAUDX_IDLE; // BAUD_SET途上なら破棄 (BREAK優先)
    s_baudx.adopt_n = 0u;
    mutex_enter_blocking(&g_m);
    g_s.baud_locked = false;
    g_s.baud_idx = order[0];
    g_s.baud_save_req = false; // 旧要求は無効
    g_s.ib_r = g_s.ib_w;       // 旧rateのinboxは捨てる
    mutex_exit(&g_m);
}

// UART1のbaud切替のHW部 (DMA停止→drain→set_baudrate→parser再init
// (acc捨て・stats保持)→ring再同期→DMA再開)。hunt/BAUD_SETの共有下請け。
// Core1専用 (DMA/UARTはCore1所有)。
static void baud_hw_switch(uint32_t bps) {
    dma_channel_abort(g_dma_ch); // 停止＋無効化 (再開はset_write_addr trig)
    while (uart_is_readable(DATA_UART)) {
        (void)uart_getc(DATA_UART);
    }
    // RP2350: UARTRSR/UARTECRは同一アドレス。rsrへの書込でclear。
    uart_get_hw(DATA_UART)->rsr = 0xFF;
    (void)uart_set_baudrate(DATA_UART, bps);
    parser_init(&s_parser, bcon_frame_cb, NULL, &s_pst); // acc捨て・stats保持
    {
        uint32_t wa = dma_hw->ch[g_dma_ch].write_addr;
        g_rd = (wa - (uint32_t)dma_ring) & (RING_SIZE - 1); // 旧baud残渣捨て
    }
    dma_channel_set_write_addr(g_dma_ch, dma_ring, true);
}

// slot切替: 上記HW部＋hunt bookkeeping (2-frame計数reset等)。
static void hunt_set_slot(uint8_t at, uint32_t now) {
    uint32_t bps = baud_bps(s_hunt.slots[at]);
    if (bps == 0u) {
        return;
    }
    baud_hw_switch(bps);
    baud_lock_reset(&s_hunt.lock);
    s_hunt.err_base = s_pst.err_crc;
    s_hunt.drop_base = s_pst.err_drop;
    s_hunt.at = at;
    s_hunt.dwell_until = now + HUNT_DWELL_MS;
    mutex_enter_blocking(&g_m);
    g_s.baud_idx = s_hunt.slots[at];
    mutex_exit(&g_m);
}

// BAUD_SET切替の実行部 (HW切替＋正本・mirror更新)。
static void baudx_do_switch(uint8_t idx) {
    uint32_t bps = baud_bps(idx);
    if (bps == 0u) {
        return;
    }
    baud_hw_switch(bps);
    s_baudx.cur = idx;
    s_baudx.err_base = s_pst.err_crc;
    s_baudx.drop_base = s_pst.err_drop;
    mutex_enter_blocking(&g_m);
    g_s.baud_idx = idx;
    mutex_exit(&g_m);
}

static void core1_entry(void) {
    stack_guard_install(&__StackOneBottom); // MSPLIMガード
    // PoC確定: lockout victim初期化 (flash_safe_executeの相手側)。
    bool ok_init = flash_safe_execute_core_init();
    (void)ok_init;

    memset(&s_pst, 0, sizeof(s_pst));
    parser_init(&s_parser, bcon_frame_cb, NULL, &s_pst);
    g_s.state.lx = g_s.state.ly = 0x800u;
    g_s.state.rx = g_s.state.ry = 0x800u;
    g_s.state_accept = true;
#if POKECON_INPUT
    g_s.baud_locked = true; // 固定rateのため先頭byteからtrustする
#endif

    uint32_t st = core1_selftest();
    mutex_enter_blocking(&g_m);
    g_s.boot_code = (st == 0) ? 1u : 2u;
    g_s.boot_detail = st;
    // selftestのinbox残渣を捨てる (起動時1回のみ)。
    g_s.ib_r = g_s.ib_w;
    mutex_exit(&g_m);

    // hunt init (B-0): slotsはCore0がBCBR込で組済み。armedはselftest後に
    // 立てる (合成3 frameをlock計数に入れない)。
    {
        uint8_t k;
        mutex_enter_blocking(&g_m);
        s_hunt.nslots = g_s.baud_nslots;
        if (s_hunt.nslots == 0u || s_hunt.nslots > BAUD_N) {
            s_hunt.nslots = 1u;
            s_hunt.slots[0] = 0u;
        } else {
            for (k = 0u; k < s_hunt.nslots; k++) {
                s_hunt.slots[k] = g_s.baud_slots[k];
            }
        }
        s_hunt.at = 0u;
        s_hunt.locked = g_s.baud_locked; // 単slot構成は確定済みで起動
        s_baudx.cur = g_s.baud_idx;      // BAUD_SET正本の初期値
        s_baudx.phase = BAUDX_IDLE;
        s_baudx.adopt_n = 0u;
#if POKECON_INPUT
        // PokeCon modeは固定rate (derated 115200 buildと組。spec §2)。
        // hunt sweepなし・即lockでTXを開ける (B-0 bypass。自動判別なし)。
        s_hunt.nslots = 1u;
        s_hunt.slots[0] = g_s.baud_idx;
        s_hunt.locked = true;
        g_s.baud_locked = true;
#endif
        mutex_exit(&g_m);
        baud_lock_reset(&s_hunt.lock);
        s_hunt.err_base = s_pst.err_crc;
        s_hunt.drop_base = s_pst.err_drop;
        s_hunt.dwell_until = to_ms_since_boot(get_absolute_time()) +
            HUNT_DWELL_MS;
        s_hunt.armed = true;
    }

    uint32_t boot_bps = baud_bps(g_s.baud_idx);
    if (boot_bps == 0u) {
        boot_bps = POC_DATA_BAUD;
    }
    uart_init(DATA_UART, boot_bps);
    gpio_set_function(DATA_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(DATA_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(DATA_RX_PIN); // フロート時の誤BREAK防止＋ノイズ耐性
    uart_set_hw_flow(DATA_UART, false, false);
    uart_set_format(DATA_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(DATA_UART, true);
    while (uart_is_readable(DATA_UART)) (void)uart_getc(DATA_UART);

    g_dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(g_dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RING_BITS);
    channel_config_set_dreq(&c, DREQ_UART1_RX);
    dma_channel_configure(g_dma_ch, &c, dma_ring, &uart_get_hw(DATA_UART)->dr,
                          0xFFFFFFFFu, true);

    uint32_t iters = 0;
    uint32_t ovr = 0;
    mutex_enter_blocking(&g_m);
    if (g_s.boot_code == 1) g_s.boot_code = 3u;
    mutex_exit(&g_m);
    g_core1_booted = true;

    for (;;) {
        // hunt sweep (B-0): !lockedならsession有無にかかわらず継続する。
        // (旧一時停止は撤去: Switch先行接続でPCと無関係なslotに凍結し、
        // PCが永久にlockできなくなるため。確定前UARTは無trafficのため
        // sessionへの影響なし)。確定後は永久固定 (再開はBREAKのみ)。
        if ((iters & 0x3FFu) == 0u) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            // BREAK検出 (re-hunt要求)。gpio_getはpin機能に関わらず
            // 読めるため、どのbaudでも検出できる。
            if (gpio_get(DATA_RX_PIN)) {
                s_hunt.brk_low_since = now;
                s_hunt.brk_fired = false;
            } else if (!s_hunt.brk_fired &&
                       (int32_t)(now - s_hunt.brk_low_since) >=
                       (int32_t)BREAK_LOW_MS) {
                s_hunt.brk_fired = true;
                hunt_on_break(now);
            }
            if (!s_hunt.locked) {
                if ((int32_t)(now - s_hunt.dwell_until) >= 0) {
                    hunt_set_slot((uint8_t)((s_hunt.at + 1u) % s_hunt.nslots),
                                  now);
                } else if (s_pst.err_crc != s_hunt.err_base ||
                           s_pst.err_drop != s_hunt.drop_base) {
                    // 有効でないframeの痕跡＝lock対の分断。計数を割る。
                    s_hunt.err_base = s_pst.err_crc;
                    s_hunt.drop_base = s_pst.err_drop;
                    baud_lock_feed(&s_hunt.lock, false);
                }
            }
            // BAUD_SET (B §3): Core0要求の合意切替＋2s自動復帰。実行は
            // Core1 (DMA/UART所有)。確定保存はcb側のadopt対が行う。
            {
                bool req = false;
                uint8_t ridx = 0u;
                mutex_enter_blocking(&g_m);
                if (g_s.baudx_req) {
                    g_s.baudx_req = false;
                    req = true;
                    ridx = g_s.baudx_idx;
                }
                mutex_exit(&g_m);
                if (req && baud_idx_valid(ridx) && ridx != s_baudx.cur &&
                    s_baudx.phase == BAUDX_IDLE) {
                    s_baudx.target = ridx;
                    s_baudx.prev = s_baudx.cur;
                    s_baudx.at = now + BAUDX_GUARD_MS;
                    s_baudx.phase = BAUDX_ARMED;
                }
            }
            if (s_baudx.phase == BAUDX_ARMED &&
                (int32_t)(now - s_baudx.at) >= 0) {
                baudx_do_switch(s_baudx.target);
                s_baudx.phase = BAUDX_MOVED;
                s_baudx.revert_at = now + BAUDX_REVERT_MS;
                s_baudx.adopt_n = 0u;
            } else if (s_baudx.phase == BAUDX_MOVED) {
                if (s_baudx.adopt_n >= 2u) {
                    s_baudx.phase = BAUDX_IDLE; // 確定。以後は新rateで運用。
                } else {
                    if (s_pst.err_crc != s_baudx.err_base ||
                        s_pst.err_drop != s_baudx.drop_base) {
                        s_baudx.err_base = s_pst.err_crc;
                        s_baudx.drop_base = s_pst.err_drop;
                        s_baudx.adopt_n = 0u;
                    }
                    if ((int32_t)(now - s_baudx.revert_at) >= 0) {
                        baudx_do_switch(s_baudx.prev);
                        s_baudx.phase = BAUDX_IDLE;
                        mutex_enter_blocking(&g_m);
                        g_s.baudx_reverted = true;
                        g_s.baudx_rev_idx = s_baudx.cur;
                        mutex_exit(&g_m);
                    }
                }
            }
        }
        uint32_t wa = dma_hw->ch[g_dma_ch].write_addr;
        uint32_t idx = (wa - (uint32_t)dma_ring) & (RING_SIZE - 1);
        if (idx != g_rd) {
#if POKECON_INPUT
            {
                uint32_t p = g_rd;
                while (p != idx) {
                    pokecon_drain_byte(dma_ring[p]);
                    p = (p + 1u) & (RING_SIZE - 1u);
                }
            }
#else
            if (idx > g_rd) {
                parser_feed_buf(&s_parser, &dma_ring[g_rd], idx - g_rd);
            } else {
                parser_feed_buf(&s_parser, &dma_ring[g_rd], RING_SIZE - g_rd);
                if (idx > 0) parser_feed_buf(&s_parser, &dma_ring[0], idx);
            }
#endif
            g_rd = idx;
        }
        if (uart_get_hw(DATA_UART)->rsr & UART_UARTRSR_OE_BITS) {
            // RP2350: UARTRSR/UARTECRは同一アドレス。rsrへの書込でclear。
            uart_get_hw(DATA_UART)->rsr = 0xFF;
            ovr++;
        }
        iters++;
        if ((iters & 0xFFFu) == 0) {
            mutex_enter_blocking(&g_m);
            g_s.iters = iters;
            g_s.overruns = ovr;
            g_s.crc_err = s_pst.err_crc;
            g_s.drop_ev = s_pst.err_drop;
            g_s.last_seq = s_pst.last_seq;
            g_s.perrcode = s_pst.errcode;
            mutex_exit(&g_m);
        }
    }
}

// ---------------- Core0: UART1 TX (Pico->PC。RX DMAと方向が独立) ----------------
static void uart_tx_frame(uint8_t type, const uint8_t *payload, uint8_t len) {
    uint8_t out[PROTO_FRAME_MAX];
    size_t n;
    if (!s_baud_tx_ok) {
        return; // hunt中TX抑制 (B-0)。確定後に開放する。
    }
    n = frame_build(out, type, payload, len, g_tx_seq);
    g_tx_seq++;
    if (n > 0) {
        uart_write_blocking(DATA_UART, out, n);
    }
}

// ---------------- Core0: STATUS組立 (新鮮なHW状態で作る) ----------------
static void send_status(void) {
    uint8_t flags = 0u;
    uint8_t payload[7];
    bcon_shared_t cp;
    usb_wired_stats_t ws;
    uint32_t rumble_total;
    mutex_enter_blocking(&g_m);
    cp = g_s;
    mutex_exit(&g_m);
    usb_wired_get_stats(&ws);
    if (usb_wired_is_configured()) flags |= ST_USB_MOUNTED;
    if (probe_hid_cid != 0u || usb_wired_handshake_done()) flags |= ST_SWITCH_READY;
    if (s_neutral_hold) flags |= ST_TIMEOUT_NEUTRAL;
    if (s_wdt_recovered) flags |= ST_WDT_RECOVERED;
    if (cp.overruns > 0u) flags |= ST_UART_OVERRUN;
    if (s_wired) flags |= ST_WIRED_MODE;
    if (probe_hid_cid != 0u) flags |= ST_BT_CONNECTED;
    rumble_total = bcon_usb_rumble_n + bcon_bt_rumble_n;
    if (rumble_total != s_rumble_reported) {
        flags |= ST_RUMBLE_SEEN;
        s_rumble_reported = rumble_total;
    }
    v3_pack_status(flags, cp.last_seq, cp.crc_err, cp.drop_ev,
                   (g_vs.errcode != 0u) ? g_vs.errcode : cp.perrcode, payload);
    uart_tx_frame(T_STATUS, payload, 7);
}

// ---------------- Core0: FX実行 (dispatch受理の効果) ----------------
// 注意: BT未初期化 (有線起動) では電波系FXは拒否する (gap/hci呼出不可)。
static void exec_fx(uint32_t now_ms) {
    switch (g_vs.fx) {
        case FX_NONE:
            break;
        case FX_CAPTURE_START:
            if (!s_bt_init || !link_cap_start(g_vs.fx_arg)) {
                g_vs.errcode = (uint8_t)(0x10u | (T_CAPTURE_START & 0x0Fu));
            }
            break;
        case FX_BEACON_START:
            if (!s_bt_init || !link_beacon_start()) {
                g_vs.errcode = (uint8_t)(0x10u | (T_BEACON_START & 0x0Fu));
            }
            break;
        case FX_COLOR_SET:
            memcpy(spi_color_6050, g_vs.color, 12);
            store_color();
            probe_color_set_count++;
            break;
        case FX_KEY_DELETE:
            if (s_bt_init) {
                gap_delete_all_link_keys();
            }
            store_host_forget();
            probe_line("keys deleted (classic + host tag)");
            break;
        case FX_WIRED_MODE: {
            bool w = g_vs.fx_arg != 0u;
            if (w == s_wired) {
                break; // 変化なし。再起動しない。
            }
            s_wired = w;
            store_wired(w);
            usb_wired_set_enabled(w);
            if (s_bt_init) {
                link_apply_wired_mode(w);
            }
            // 切替は再起動で適用 (CYW43/BTstackの有無が起動時確定のため)。
            // 同一tickのoutbox flush後に落ちるよう500ms猶予。
            s_reboot_at = now_ms + 500u;
            probe_line(w ? "WIRED_MODE=1 -> reboot to wired"
                         : "WIRED_MODE=0 -> reboot to wireless");
            break;
        }
        case FX_BAUD_SET: {
            // B §3: ACK (STATUS) はdispatch時にqueue済みで旧rate送信。
            // ここではCore1への切替要求のみ (実行・復帰はCore1)。
            uint8_t idx = g_vs.fx_arg;
            uint8_t cur;
            mutex_enter_blocking(&g_m);
            cur = g_s.baud_idx;
            mutex_exit(&g_m);
            if (!baud_idx_valid(idx)) {
                g_vs.errcode = 0x16u; // 二重防御 (dispatchで弾済みのはず)
            } else if (idx != cur) {
                mutex_enter_blocking(&g_m);
                g_s.baudx_idx = idx;
                g_s.baudx_req = true;
                mutex_exit(&g_m);
            }
            break;
        }
        case FX_BOOTSEL: {
            s_bootsel_at = now_ms + 500u;
            probe_line("BOOTSEL req -> usb_boot in 500ms");
            break;
        }
    }
    g_vs.fx = FX_NONE;
}

// ---------------- Core0: outbox flush ----------------
static void flush_outbox(void) {
    for (uint8_t i = 0u; i < g_vs.ob_n; i++) {
        v3_ob_t *o = &g_vs.ob[i];
        if (o->act == ACT_SEND_STATUS) {
            send_status();
        } else if (o->act == ACT_SEND_PONG) {
            uart_tx_frame(T_PONG, &o->arg, 1);
        } else if (o->act == ACT_SEND_RUMBLE) {
            uint8_t ra[2];
            ra[0] = g_vs.rumble_sent_l;
            ra[1] = g_vs.rumble_sent_r;
            uart_tx_frame(T_RUMBLE, ra, 2);
        } else if (o->act == ACT_SEND_HELLO_ACK) {
            uint8_t ack[4];
            ack[0] = PROTO_VER;
            ack[1] = FW_MAJOR;
            ack[2] = FW_MINOR;
            ack[3] = g_vs.result;
            uart_tx_frame(T_HELLO_ACK, ack, 4);
        } else if (o->act == ACT_SEND_PLAYER_INFO) {
            uint8_t pi[2];
            pi[0] = g_vs.player_sent_lamp;
            pi[1] = g_vs.player_sent_flags;
            uart_tx_frame(T_PLAYER_INFO, pi, 2);
        }
    }
    g_vs.ob_n = 0u;
}

// ---------------- Core0: LOG_UART BOOTSEL行 (開発用・単独UART運用) ----------------
// UART0だけ繋いだ状態でもBOOTSELに入れるよう、LOG_RXを行単位でpollする。
// 改行終端で行内に "bootsel" (大小不問) があればDATA側T_BOOTSELと同一の
// s_bootsel_atをarmする。TXログとは独立 (RXのみ消費)。
static char s_logline[32];
static uint8_t s_logline_n;
static void poll_log_bootsel(uint32_t now) {
    bool matched;
    uint8_t k;
    while (uart_is_readable(LOG_UART)) {
        int c = uart_getc(LOG_UART);
        if (c == '\r' || c == '\n') {
            matched = false;
            if (s_logline_n >= 7u) {
                for (k = 0u; k + 7u <= s_logline_n; k++) {
                    if ((s_logline[k] | 0x20) == 'b' &&
                        (s_logline[k + 1u] | 0x20) == 'o' &&
                        (s_logline[k + 2u] | 0x20) == 'o' &&
                        (s_logline[k + 3u] | 0x20) == 't' &&
                        (s_logline[k + 4u] | 0x20) == 's' &&
                        (s_logline[k + 5u] | 0x20) == 'e' &&
                        (s_logline[k + 6u] | 0x20) == 'l') {
                        matched = true;
                        break;
                    }
                }
            }
            s_logline_n = 0u;
            if (matched && s_bootsel_at == 0u) {
                s_bootsel_at = now + 500u;
                probe_line("BOOTSEL req (log) -> usb_boot in 500ms");
            }
        } else if (s_logline_n < sizeof(s_logline)) {
            s_logline[s_logline_n++] = (char)c;
        } else {
            s_logline_n = 0u;
        }
    }
}

// ---------------- Core0: 1ms tick (pull/pack/drain/FX/flush/usb/neutral) ----------------
static void poll_tick(uint32_t now) {
    bcon_shared_t cp;
    uint8_t pb[3];
    bool advanced;
    mutex_enter_blocking(&g_m);
    cp = g_s;
    mutex_exit(&g_m);

    // inbox drain → dispatch。live indexで空になるまで回す。
    // (旧コードはスナップショット判定で無限ループしたためlive indexで回す。)
    g_vs.cap_valid = probe_cap_valid;
    g_vs.player_lamp = probe_player_id;
    g_vs.player_flags = (uint8_t)((probe_imu_enabled ? 0x01u : 0u) |
                                  (probe_vibration_enabled ? 0x02u : 0u));
    g_vs.player_valid = probe_player_seen;
    v3_player_tick(&g_vs);
    g_vs.rumble_l = probe_rumble_l;
    g_vs.rumble_r = probe_rumble_r;
    g_vs.rumble_valid = probe_rumble_seen;
    v3_rumble_tick(&g_vs);
    // baud hunt (B-0)＋BAUD_SET (B §3): session発行＋保存drain＋復帰報告。
    // inbox/FXは確定まで閉じる (確定前バイトはuntrusted)。
    bool baud_locked;
    {
        bool save_req;
        uint8_t save_idx;
        bool rev;
        uint8_t rev_idx;
        mutex_enter_blocking(&g_m);
        g_s.bt_session = (probe_hid_cid != 0u);
        save_req = g_s.baud_save_req;
        save_idx = g_s.baud_save_idx;
        if (save_req) {
            g_s.baud_save_req = false;
        }
        rev = g_s.baudx_reverted;
        rev_idx = g_s.baudx_rev_idx;
        if (rev) {
            g_s.baudx_reverted = false;
        }
        baud_locked = g_s.baud_locked;
        mutex_exit(&g_m);
        if (save_req) {
            char bl[48];
            store_baud(save_idx); // 変化時のみ1 write (Core1直接flash禁止)
            snprintf(bl, sizeof(bl), "data baud=%lu locked",
                     (unsigned long)baud_bps(save_idx));
            probe_line(bl);
        }
        if (rev) {
            char br[48];
            snprintf(br, sizeof(br), "data baud=%lu reverted",
                     (unsigned long)baud_bps(rev_idx));
            probe_line(br);
        }
    }
    if (s_baud_was_locked && !baud_locked) {
        probe_line("data baud re-hunt"); // BREAK等による確定解除
    }
    s_baud_was_locked = baud_locked;
    s_baud_tx_ok = baud_locked;
    if (baud_locked) {
    for (;;) {
        ib_msg_t cur;
        bool empty;
        mutex_enter_blocking(&g_m);
        empty = (g_s.ib_r == g_s.ib_w);
        if (!empty) {
            // payload最大32Bを局所複写 (mutex内でdispatchしない)。
            cur = g_s.ib[g_s.ib_r];
            g_s.ib_r = (uint8_t)((g_s.ib_r + 1u) % IB_N);
        }
        mutex_exit(&g_m);
        if (empty) {
            break;
        }
        // STATE/NEUTRALのlive適用はCore1高速路が担う (UNSUPPORTED下の
        // STATE拒否を含む)。ここではCONFIG等のfx/obのみ扱う。
        (void)v3_on_frame(&g_vs, cur.type, cur.payload, cur.len, cur.seq);
    }
    exec_fx(now);
    }
    flush_outbox();

    // 共有u32 → pack → bcon_*/probe_* (Core0単独)。
    ctrl_pack_btn3(&cp.state, pb);
    bcon_btn[0] = pb[0];
    bcon_btn[1] = pb[1];
    bcon_btn[2] = pb[2];
    bcon_lx = cp.state.lx;
    bcon_ly = cp.state.ly;
    bcon_rx = cp.state.rx;
    bcon_ry = cp.state.ry;
    probe_btn[0] = pb[0];
    probe_btn[1] = pb[1];
    probe_btn[2] = pb[2];
    probe_lx = cp.state.lx;
    probe_ly = cp.state.ly;
    probe_rx = cp.state.rx;
    probe_ry = cp.state.ry;

    // STATE到着追跡＋timeout-neutral (200ms。spec §8)。
    advanced = (cp.frames != s_last_frames);
    if (advanced) {
        s_last_frames = cp.frames;
        s_last_state_ms = now;
        if (s_neutral_hold) {
            s_neutral_hold = false;
        }
        // 正常受信でCONFIG拒否コードを戻す (spec §5.5)。
        if (g_vs.errcode != 0u && cp.frames > 0u) {
            g_vs.errcode = 0u;
        }
    } else if (!s_neutral_hold &&
               (int32_t)(now - s_last_state_ms) >= (int32_t)TIMEOUT_NEUTRAL_MS) {
        bool live;
        mutex_enter_blocking(&g_m);
        live = (g_s.state.buttons != 0u || g_s.state.lx != 0x800u ||
                g_s.state.ly != 0x800u || g_s.state.rx != 0x800u ||
                g_s.state.ry != 0x800u);
        if (live) {
            g_s.state.buttons = 0u;
            g_s.state.lx = g_s.state.ly = 0x800u;
            g_s.state.rx = g_s.state.ry = 0x800u;
        }
        mutex_exit(&g_m);
        if (live ||
            probe_btn[0] != 0u || probe_btn[1] != 0u || probe_btn[2] != 0u) {
            probe_input_reset();
            probe_request_send();
            probe_line("timeout-neutral");
        }
        s_neutral_hold = true;
    }

    usb_wired_task(now);
    link_poll(now);
    watchdog_update();
    poll_log_bootsel(now);
    if (s_bootsel_at != 0u && (int32_t)(now - s_bootsel_at) >= 0) {
        probe_line("rebooting to BOOTSEL...");
        sleep_ms(50);
        reset_usb_boot(0, 0);
        while (1) tight_loop_contents();
    }
    if (s_reboot_at != 0u && (int32_t)(now - s_reboot_at) >= 0) {
        probe_line("rebooting to apply WIRED_MODE...");
        sleep_ms(50);
        watchdog_reboot(0, 0, 0);
        while (1) tight_loop_contents();
    }
}
// ---------------- Core0: BTstack timers ----------------
static uint8_t hid_service_buffer[700];
static uint8_t pnp_service_buffer[200];

static btstack_packet_callback_registration_t hci_events;
static btstack_timer_source_t stats_timer;
static btstack_timer_source_t usb_timer;
static btstack_timer_source_t empty_timer;
static btstack_timer_source_t reconnect_timer;

/* Death-B恒久対策の可観測性: PRIMASKをBCONに出す (0=IRQ開)。store側guardと対。 */
static uint32_t pm_rd(void)
{
    uint32_t r;
    __asm volatile ("mrs %0, primask" : "=r" (r) ::);
    return r;
}

static void stats_print(void) {
    char line[224];
    usb_wired_stats_t ws;
    bcon_shared_t cp;
    mutex_enter_blocking(&g_m);
    cp = g_s;
    mutex_exit(&g_m);
    usb_wired_get_stats(&ws);
    snprintf(line, sizeof(line),
             "BCON t=%lus hs=%d mnt=%d cid=%u wired=%d "
             "cap=%d/%d/%d res=%d err=%02x rum=%lu+%lu "
              "ibdrop=%lu obdrop=%u frames=%lu crc=%lu drop=%lu ovr=%lu "
              "rx80=%lu tx81=%lu tx21=%lu in30=%lu iters=%lu pm=%lu "
              "baud=%u%c",
             (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
             usb_wired_handshake_done() ? 1 : 0,
             usb_wired_is_configured() ? 1 : 0,
             (unsigned)probe_hid_cid, s_wired ? 1 : 0,
             probe_cap_valid ? 1 : 0, probe_scanning ? 1 : 0,
             probe_beacon ? 1 : 0,
             g_vs.result, g_vs.errcode,
             (unsigned long)bcon_usb_rumble_n,
             (unsigned long)bcon_bt_rumble_n,
             (unsigned long)cp.ib_drop, g_vs.ob_dropped,
             (unsigned long)cp.frames, (unsigned long)cp.crc_err,
             (unsigned long)cp.drop_ev, (unsigned long)cp.overruns,
              (unsigned long)ws.rx80, (unsigned long)ws.tx81,
              (unsigned long)ws.tx21, (unsigned long)ws.in30,
              (unsigned long)cp.iters, (unsigned long)pm_rd(),
              (unsigned)cp.baud_idx, cp.baud_locked ? 'L' : 'H');
    printf("%s\n", line);
    if (g_vs.auto_status) {
        send_status();
    }
}

static void stats_handler(btstack_timer_source_t *ts) {
    stats_print();
    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

static void usb_handler(btstack_timer_source_t *ts) {
    poll_tick(to_ms_since_boot(get_absolute_time()));
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);
}

static void empty_handler(btstack_timer_source_t *ts) {
    if (probe_hid_cid != 0u) {
        // 送信ゲート試験は終了 (無罪確定のため復帰)。TLV停止のみ継続。
        hid_device_request_can_send_now_event(probe_hid_cid);
    }
    btstack_run_loop_set_timer(ts, probe_send_interval_ms());
    btstack_run_loop_add_timer(ts);
}

// ---------------- Core0: BT packet handlers (wakecon同等) ----------------
static void handle_bt_ready(uint8_t *packet) {
    char msg[96];
    (void)packet;
    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
        link_note_bt_working(false);
        return;
    }
    link_note_bt_working(true);
    probe_line("BT READY");
    snprintf(msg, sizeof(msg), "link keys=%d", link_key_count());
    probe_line(msg);
    if (probe_host_known) {
        /* remove-before-add: idempotent, no-op when unscheduled */
        btstack_run_loop_remove_timer(&reconnect_timer);
        btstack_run_loop_set_timer(&reconnect_timer, 2000);
        btstack_run_loop_add_timer(&reconnect_timer);
    } else {
        probe_line("no host. pair from Switch Change-Grip screen");
    }
    probe_line(probe_cap_valid ? "cap saved. send BEACON_START" :
                                 "cap none. send CAPTURE_START");
}

static void handle_conn_complete(uint8_t *packet) {
    char msg[96];
    uint8_t cst = hci_event_connection_complete_get_status(packet);
    snprintf(msg, sizeof(msg), "conn status=0x%02x%s", cst,
             (cst == ERROR_CODE_SUCCESS) ? " ok" : " FAIL");
    probe_line(msg);
    if (cst == 0x04u) {
        probe_line("0x04=Page Timeout (peer silent)");
    }
    if (cst != ERROR_CODE_SUCCESS) {
        return;
    }
    probe_connected_at_ms = to_ms_since_boot(get_absolute_time());
    probe_ssp_count = 0u;
}

static void handle_disc(uint8_t *packet) {
    char msg[96];
    uint8_t reason =
        hci_event_disconnection_complete_get_reason(packet);
    uint32_t held_ms = to_ms_since_boot(get_absolute_time()) -
        probe_connected_at_ms;
    snprintf(msg, sizeof(msg), "disc reason=0x%02x held=%lums",
             reason, (unsigned long)held_ms);
    probe_line(msg);
    if (reason == 0x05u && probe_ssp_count == 0u && held_ms < 200u) {
        probe_line("console stall (no SSP). power OFF Switch, or K + re-pair");
    } else if (probe_ssp_count > 0u) {
        snprintf(msg, sizeof(msg), "ssp=%u ok", probe_ssp_count);
        probe_line(msg);
    }
    probe_hid_cid = 0u;
    link_note_disconnected();
    probe_line("reconnect armed");
}

static void handle_hid_meta(uint8_t *packet) {
    char msg[96];
    uint8_t sub = hci_event_hid_meta_get_subevent_code(packet);
    switch (sub) {
        case HID_SUBEVENT_CONNECTION_OPENED: {
            uint8_t st =
                hid_subevent_connection_opened_get_status(packet);
            if (st != ERROR_CODE_SUCCESS) {
                snprintf(msg, sizeof(msg),
                         "hid open FAIL status=0x%02x", st);
                probe_line(msg);
                if (st == 0x66u || st == 0x6au) {
                    probe_line("0x66/0x6a=refused security. "
                               "delete pairing on BOTH sides + re-pair");
                }
                probe_hid_cid = 0u;
                link_note_disconnected();
                break;
            }
            probe_hid_cid =
                hid_subevent_connection_opened_get_hid_cid(packet);
            probe_hid_reset();
            {
                bd_addr_t a;
                hid_subevent_connection_opened_get_bd_addr(packet, a);
                store_host(a);
                snprintf(msg, sizeof(msg), "hid open. host %s saved",
                         bd_addr_to_str(a));
                probe_line(msg);
            }
            link_mark_connected();
            break;
        }
        case HID_SUBEVENT_CONNECTION_CLOSED:
            probe_hid_cid = 0u;
            link_note_disconnected();
            probe_line("hid closed");
            break;
        case HID_SUBEVENT_CAN_SEND_NOW:
            probe_can_send_now();
            break;
        default:
            snprintf(msg, sizeof(msg), "hid sub=0x%02x", sub);
            probe_line(msg);
            break;
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    uint8_t ev;
    (void)channel;
    (void)size;
    ev = hci_event_packet_get_type(packet);
    // LE 切断は Classic 側の処理へ落とさない。
    if (ev == HCI_EVENT_DISCONNECTION_COMPLETE &&
        link_is_le(hci_event_disconnection_complete_get_connection_handle(
            packet))) {
        link_le_packet(packet_type, packet, size);
        return;
    }
    link_le_packet(packet_type, packet, size);
    // SSP/pairing可視化 (wakecon同等。鍵の秘密は出さない)。
    // 0x31/0x36=SSP begin/end、0x32/0x33もSSP。0x0e=BDADDR応答。
    if (ev == 0x31u || ev == 0x32u || ev == 0x33u || ev == 0x36u) {
        if (probe_ssp_count < 255u) {
            probe_ssp_count++;
        }
    }
    if (ev == 0x0eu && size >= 12u && packet[3] == 0x01u &&
        packet[4] == 0x09u && packet[5] == 0x10u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "BDADDR=%02x%02x%02x%02x%02x%02x",
                 packet[11], packet[10], packet[9],
                 packet[8], packet[7], packet[6]);
        probe_line(msg);
    }
    switch (ev) {
        case BTSTACK_EVENT_STATE:
            handle_bt_ready(packet);
            break;
        case HCI_EVENT_CONNECTION_REQUEST: {
            // 接続要求元の直接記録 (行為不変)。
            // peer BD_ADDR程度は可 (秘密は出さない)。
            bd_addr_t a;
            char msg[64];
            hci_event_connection_request_get_bd_addr(packet, a);
            snprintf(msg, sizeof(msg), "conn request peer=%02x%02x%02x%02x%02x%02x lt=%u",
                     a[0], a[1], a[2], a[3], a[4], a[5],
                     (unsigned)hci_event_connection_request_get_link_type(packet));
            probe_line(msg);
            break;
        }
        case HCI_EVENT_CONNECTION_COMPLETE:
            handle_conn_complete(packet);
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            handle_disc(packet);
            break;
        case HCI_EVENT_HID_META:
            handle_hid_meta(packet);
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
            if (packet_type == HCI_EVENT_PACKET) {
                link_cap_report(packet);
            }
            break;
        // 認証交換の直接記録 (行為不変)。鍵の秘密は出さない。
        case HCI_EVENT_LINK_KEY_REQUEST: {
            bd_addr_t a;
            char msg[64];
            hci_event_link_key_request_get_bd_addr(packet, a);
            snprintf(msg, sizeof(msg), "linkkey req peer=%02x%02x%02x%02x%02x%02x",
                     a[0], a[1], a[2], a[3], a[4], a[5]);
            probe_line(msg);
            break;
        }
        case HCI_EVENT_AUTHENTICATION_COMPLETE: {
            char msg[64];
            uint8_t auth_status =
                hci_event_authentication_complete_get_status(packet);
            snprintf(msg, sizeof(msg), "auth complete status=0x%02x",
                     auth_status);
            probe_line(msg);
            if (auth_status != 0) {
                /* Stale key: forget it so the next attempt re-pairs cleanly. */
                gap_delete_all_link_keys();
                probe_line("auth fail: keys dropped, re-pair");
            }
            break;
        }
        case HCI_EVENT_ENCRYPTION_CHANGE: {
            char msg[64];
            snprintf(msg, sizeof(msg), "encrypt change status=0x%02x en=%u",
                     hci_event_encryption_change_get_status(packet),
                     (unsigned)hci_event_encryption_change_get_encryption_enabled(packet));
            probe_line(msg);
            break;
        }
        default:
            break;
    }
}

// 有線起動のCore0 loop (BTstackなし・CYW43なし)。
// CYW43給電中はSwitch 2ドックがUSB列挙しない実測のため、有線時は
// 無線一式を上げない。1ms tick＋1秒log。無線への切替は再起動適用。
static void wired_loop(void) {
    uint32_t last_ms = 0u, last_log = 0u;
#if POKECON_INPUT
    printf("ready. feed UART1 GP4/5 %lu baud pokecon lines. log=UART0 115200\n",
           (unsigned long)s_boot_baud);
#else
    printf("ready. feed UART1 GP4/5 %lu baud v3 frames. log=UART0 115200\n",
           (unsigned long)s_boot_baud);
#endif
    watchdog_enable(2000, 1);
    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now == last_ms) {
            tight_loop_contents();
            continue;
        }
        last_ms = now;
        poll_tick(now);
        if (now - last_log >= 1000u) {
            last_log = now;
            stats_print();
        }
    }
}

// ---------------- Core0 main ----------------
int main(void) {
    hid_sdp_record_t hid_params = {
        SWITCH_CLASS_OF_DEVICE,
        33, 1, 1, 1, 0, 0, 0xFFFF, 0xFFFF, 3200,
        switch_bt_report_descriptor,
        sizeof(switch_bt_report_descriptor),
        SWITCH_HID_NAME,
    };
    stdio_init_all();
    uart_init(LOG_UART, LOG_BAUD);
    gpio_set_function(LOG_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LOG_RX_PIN, GPIO_FUNC_UART);

    printf("\n=== pico-bcon ===\n");
    // 正規手段でHardFault報告を掛ける。
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hardfault_reporter);
    // 自コアのMSPLIM (Core1側はcore1_entryで)。
    stack_guard_install(&__StackBottom);
    // UDF疎通確認はrevert済み (動作確認OK)。

    // WDT復帰の記録 (STATUS bit3)。enable前の値を読む。
    s_wdt_recovered = watchdog_caused_reboot();

    // 自MAC: OUI 7C:BB:8A＋unique末尾 (wakecon link_initと同一)。
    link_init();
    {
        int k;
        for (k = 0; k < 6; k++) bcon_mac[k] = probe_addr[5 - k];
        printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n",
               probe_addr[0], probe_addr[1], probe_addr[2],
               probe_addr[3], probe_addr[4], probe_addr[5]);
    }

    // TLV永続化の初期化 (host/color/cap/wired＋Classicリンク鍵)。
    // btstack_tlv_get_instance は設定した者が勝ち。未設定だと全store系が
    // サイレント無効になる (wakeconからの継承漏れ対策)。
    // Core1起動前の純RAM設定のためFlash保護は不要。
    {
        static btstack_tlv_flash_bank_t tlv_bank_ctx;
        const btstack_tlv_t *tlv_impl;
        tlv_impl = btstack_tlv_flash_bank_init_instance(
            &tlv_bank_ctx, pico_flash_bank_instance(), NULL);
        btstack_tlv_set_instance(tlv_impl, &tlv_bank_ctx);
        hci_set_link_key_db(
            btstack_link_key_db_tlv_get_instance(tlv_impl, &tlv_bank_ctx));
        printf("tlv=%d\n", (tlv_impl != NULL) ? 1 : 0);
    }

    // TLV読込はCore1起動前に済ませる (XIP直読のため保護不要)。
    store_color_load();
    store_host_load();
    store_cap_load();
    s_wired = store_wired_load_def(WIRED_DEFAULT != 0); // 未保存時の既定
    printf("wired=%d(wdef=%d) host=%d cap=%d wdt=%d\n", s_wired ? 1 : 0, WIRED_DEFAULT,
           probe_host_known ? 1 : 0, probe_cap_valid ? 1 : 0,
           s_wdt_recovered ? 1 : 0);

    mutex_init(&g_m);
    memset(&g_s, 0, sizeof(g_s));
    g_s.state.lx = g_s.state.ly = 0x800u;
    g_s.state.rx = g_s.state.ry = 0x800u;
    g_s.state_accept = true;
    // hunt boot (B-0 Layer 0): BCBR保存値をsweep slot0に (TLV読込は
    // Core1起動前に済ませる規則に従う)。単slot構成は確定済みで起動し
    // 従来挙動 (即TX) を保つ。
    {
        uint8_t order[BAUD_N];
        uint8_t saved = 0xFFu, sb = 0u, k;
        if (store_baud_load(&sb)) {
            saved = sb;
        }
        g_s.baud_nslots =
            baud_sweep_order(saved, (uint32_t)POC_DATA_BAUD, order);
        if (g_s.baud_nslots == 0u) {
            order[0] = 0u; // 非常口 (max<115200): 115200に駐機
            g_s.baud_nslots = 1u;
        }
        for (k = 0u; k < g_s.baud_nslots; k++) {
            g_s.baud_slots[k] = order[k];
        }
        g_s.baud_idx = order[0];
        g_s.baud_locked = (g_s.baud_nslots == 1u);
        g_s.bt_session = false;
        g_s.baud_save_req = false;
        s_baud_tx_ok = g_s.baud_locked;
        s_baud_was_locked = g_s.baud_locked;
        s_boot_baud = baud_bps(order[0]);
        printf("data baud=%lu%s\n", (unsigned long)s_boot_baud,
               (g_s.baud_nslots == 1u) ? " (fixed)" :
               ((saved != 0xFFu) ? " (saved first)" : " (sweep)"));
    }
    v3_session_init(&g_vs);
    multicore_launch_core1(core1_entry);

    {
        uint32_t w0 = to_ms_since_boot(get_absolute_time());
        while (!g_core1_booted) {
            if (to_ms_since_boot(get_absolute_time()) - w0 > 2000) {
                printf("NG: core1 boot timeout\n");
                while (1) tight_loop_contents();
            }
        }
    }
    printf("core1 boot=%lu detail=%lu victim=%d\n",
           (unsigned long)g_s.boot_code, (unsigned long)g_s.boot_detail,
           (int)multicore_lockout_victim_is_initialized(1));

    // 有線起動ではBTより先にUSB (列挙前電流制限にかからないよう)。
    usb_wired_init();
    usb_wired_set_enabled(s_wired);

    if (s_wired) {
        wired_loop(); // 戻らない。無線一式は上げない (列挙干渉のため)。
    }
    // 以下は無線起動のみ。BTstack＋CYW43＋電波あり。

    if (cyw43_arch_init() != 0) {
        printf("NG: cyw43_arch_init\n");
        while (1) tight_loop_contents();
    }
    printf("cyw43 ok\n");
    usb_wired_pump();
    link_init();
    // link_initはMAC再生成のためbcon_macを掛け直す。
    {
        int k;
        for (k = 0; k < 6; k++) bcon_mac[k] = probe_addr[5 - k];
    }

    gap_discoverable_control(1);
    gap_connectable_control(1);
    gap_set_class_of_device(SWITCH_CLASS_OF_DEVICE);
    gap_set_local_name(SWITCH_GAP_NAME);
    // Switch 2 requires SNIFF acceptance: without it the console never sends
    // SUB after HID open and drops the link with 0x13 after ~1s (AB1 proven).
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(true);

    usb_wired_pump();
    l2cap_init();
    sdp_init();

    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
    hid_create_sdp_record(hid_service_buffer,
                          sdp_create_service_record_handle(), &hid_params);
    btstack_assert(de_get_len(hid_service_buffer) <= sizeof(hid_service_buffer));
    sdp_register_service(hid_service_buffer);

    memset(pnp_service_buffer, 0, sizeof(pnp_service_buffer));
    device_id_create_sdp_record(pnp_service_buffer,
                                sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_USB,
                                SWITCH_VENDOR_ID, SWITCH_PRODUCT_ID,
                                SWITCH_PRODUCT_VERSION);
    btstack_assert(de_get_len(pnp_service_buffer) <= sizeof(pnp_service_buffer));
    sdp_register_service(pnp_service_buffer);

    hid_device_init(1, sizeof(switch_bt_report_descriptor),
                    switch_bt_report_descriptor);
    hid_device_accept_truncated_hid_reports(true);
    hid_device_register_report_data_callback(&probe_report_handler);
    hid_device_register_packet_handler(&packet_handler);

    hci_events.callback = &packet_handler;
    hci_add_event_handler(&hci_events);

    hci_set_bd_addr(probe_addr);
    usb_wired_pump();

    // 有線/無線の初期反映 (電波・待ち受け・USB再列挙の決定)。
    // 無線起動のため s_wired==false のはず。BT呼び出しはここからのみ。
    link_apply_wired_mode(s_wired);
    s_bt_init = true;

    btstack_run_loop_set_timer_handler(&stats_timer, &stats_handler);
    btstack_run_loop_set_timer(&stats_timer, 1000);
    btstack_run_loop_add_timer(&stats_timer);

    btstack_run_loop_set_timer_handler(&usb_timer, &usb_handler);
    btstack_run_loop_set_timer(&usb_timer, 1);
    btstack_run_loop_add_timer(&usb_timer);

    btstack_run_loop_set_timer_handler(&empty_timer, &empty_handler);
    btstack_run_loop_set_timer(&empty_timer, 100);
    btstack_run_loop_add_timer(&empty_timer);

    btstack_run_loop_set_timer_handler(&reconnect_timer,
                                       &link_reconnect_handler);

#if POKECON_INPUT
    printf("ready. feed UART1 GP4/5 %lu baud pokecon lines. log=UART0 115200\n",
           (unsigned long)s_boot_baud);
#else
    printf("ready. feed UART1 GP4/5 %lu baud v3 frames. log=UART0 115200\n",
           (unsigned long)s_boot_baud);
#endif

    // 生存WDT 2s (spec §8)。usb tick (1ms) で更新。debug中は停止。
    watchdog_enable(2000, 1);
    btstack_run_loop_execute();

    while (1) tight_loop_contents();
    return 0;
}
