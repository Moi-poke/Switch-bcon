// main.c -- pico-bcon dual-core feasibility PoC (Task 2 Step 1).
//
// 目的: Core0 (BTstack Classic+BLE + TinyUSB + CYW43。wakecon と同一ライブラリ・
// 同一初期化順) が、Core1 (UART1 1Mbps DMA排出 + v3 parser + mutex push) と
// Flash周期書込の同時負荷でも止まらない事を実機で確認する。
//
// 役割分担 (本PoC・最終FW共通の鉄則):
//   Core0: BTstack run loop + TinyUSB pump + CYW43 + flash_safe_execute + log。
//   Core1: UART1 DMA排出 + parser + mutex push のみ。
//          USB/BT/CYW43/Flash/printf を Core1 で呼ばない (共有メモリの旗のみ)。
//
// 必須条件 (計画書): Core1 で multicore_lockout_victim_init()。
//   実装は flash_safe_execute_core_init() を呼び、内部で victim init される。
//   さらに multicore_lockout_victim_is_initialized() で明示確認する。
//
// USB注意: このPoCのUSB記述子はスタブ (VID 0xCAFE・汎用キーボード・
// "not a gamepad")。公式ProCon写しは Task 3 の仕事。Switchに挿しても
// ProCon動作はしない (列挙の有無だけ見る)。
//
// 合否判定は docs/poc_dualcore_result.md (Task 2 Step 3) に記録する。

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/flash.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "btstack.h"
#include "poc_usb.h"

#include "protocol.h"

// ---- 前提ピン・速度 (spec/protocol_v3.md: 既定 GP4/5・UART1・1Mbps) ----
#define LOG_UART      uart0
#define LOG_BAUD      115200
#define LOG_TX_PIN    0
#define LOG_RX_PIN    1

#define DATA_UART     uart1
#define DATA_BAUD     1000000
#define DATA_TX_PIN   4
#define DATA_RX_PIN   5

// PoC試験用baud上書き (-DPOC_DATA_BAUD=... で変更可。既定はspec通り1Mbps)。
// 115200上限アダプタでのderated試験用。commit物の既定値は変えない。
#ifndef POC_DATA_BAUD
#define POC_DATA_BAUD DATA_BAUD
#endif

// ---- DMA ring: 16KB。Flash消去stall (~100ms) 中も 1Mbps をこぼさない ----
// 1Mbps ~= 100KB/s。100ms stallで約10KB。到着はDMAハードが継続するので、
// ringがstall時間×帯域を上回れば欠落しない。16KBで約160ms分を確保。
#define RING_BITS 14
#define RING_SIZE (1u << RING_BITS)
static uint8_t dma_ring[RING_SIZE] __attribute__((aligned(RING_SIZE)));

// ---- Flash試験: 最終4Kセクタ。FW本体と重なれば試験をskipする ----
#define POC_FLASH_OFFS (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define POC_FLASH_MAGIC "BCONPOC1"
extern char __flash_binary_end[];

// ---- Core1 -> Core0 共有状態 (mutex保護。Core1はpushのみ) ----
typedef struct {
    ctrl_state_t last_state;
    uint8_t  last_seq;
    uint8_t  last_type;
    uint32_t frames;      // parser受理フレーム累計
    uint32_t crc_err;     // parser err_crc mirror
    uint32_t drop_ev;     // parser err_drop mirror
    uint32_t iters;       // Core1 drain loop反復 (生存証明)
    uint32_t overruns;    // UART1 overrun検出回数
    uint32_t boot_code;   // 0=boot中 1=selftest PASS 2=selftest FAIL 3=走行中
    uint32_t boot_detail; // FAIL時の内訳
} poc_shared_t;

static mutex_t g_m;
static poc_shared_t g_s;
static volatile bool g_core1_booted = false;

static int g_dma_ch = -1;
static uint32_t g_rd = 0; // Core1のみ触る読位置

// ---------------- Core1: parser callback (mutex pushのみ) ----------------
static link_stats_t s_pst;
static parser_t s_parser;

static void poc_frame_cb(uint8_t type, const uint8_t *p, uint8_t len,
                         uint8_t seq, void *user) {
    (void)user;
    mutex_enter_blocking(&g_m);
    if (type == T_STATE && len == 8) {
        g_s.last_state.buttons =
            (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        g_s.last_state.lx = p[4];
        g_s.last_state.ly = p[5];
        g_s.last_state.rx = p[6];
        g_s.last_state.ry = p[7];
    }
    g_s.last_seq = seq;
    g_s.last_type = type;
    g_s.frames++;
    g_s.crc_err = s_pst.err_crc;
    g_s.drop_ev = s_pst.err_drop;
    mutex_exit(&g_m);
}

// ---------------- Core1: 起動時selftest (合成3フレーム) ----------------
static uint32_t core1_selftest(void) {
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
    return 0; // PASS
}

// ---------------- Core1 entry ----------------
static void core1_entry(void) {
    // 必須: lockout victim初期化。flash_safe_execute_core_init()が内部で
    // multicore_lockout_victim_init()を呼ぶ。明示確認も行う。
    bool ok_init = flash_safe_execute_core_init();
    bool victim = multicore_lockout_victim_is_initialized(get_core_num());
    (void)ok_init;

    memset(&s_pst, 0, sizeof(s_pst));
    parser_init(&s_parser, poc_frame_cb, NULL, &s_pst);

    uint32_t st = core1_selftest();
    mutex_enter_blocking(&g_m);
    g_s.boot_code = (st == 0) ? 1u : 2u;
    g_s.boot_detail = st;
    mutex_exit(&g_m);
    (void)victim;

    // UART1 (GP4/5)。Core0のLOG_UARTとは独立。
    uart_init(DATA_UART, POC_DATA_BAUD);
    gpio_set_function(DATA_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(DATA_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(DATA_UART, false, false);
    uart_set_format(DATA_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(DATA_UART, true);
    while (uart_is_readable(DATA_UART)) (void)uart_getc(DATA_UART);

    // UART1 RX -> DMA ring (8bit・読固定・書wrap・DREQ)。完了しない転送。
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
    if (g_s.boot_code == 1) g_s.boot_code = 3u; // PASSのまま走行へ
    mutex_exit(&g_m);
    g_core1_booted = true;

    for (;;) {
        uint32_t wa = dma_hw->ch[g_dma_ch].write_addr;
        uint32_t idx = (wa - (uint32_t)dma_ring) & (RING_SIZE - 1);
        if (idx != g_rd) {
            if (idx > g_rd) {
                parser_feed_buf(&s_parser, &dma_ring[g_rd], idx - g_rd);
            } else {
                parser_feed_buf(&s_parser, &dma_ring[g_rd], RING_SIZE - g_rd);
                if (idx > 0) parser_feed_buf(&s_parser, &dma_ring[0], idx);
            }
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
            mutex_exit(&g_m);
        }
    }
}

// ---------------- Core0: BTstack最小 (wakeconと同lib・同順序・HID省略) ----------------
static bool s_bt_ready = false;

static void poc_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != BTSTACK_EVENT_STATE) return;
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
        s_bt_ready = true;
    }
}

static btstack_packet_callback_registration_t s_hci_cb;
static btstack_timer_source_t s_stats_timer;
static btstack_timer_source_t s_tud_timer;
static btstack_timer_source_t s_flash_timer;

static uint32_t s_fok = 0, s_ffail = 0, s_fmax_us = 0, s_fseq = 0;
static uint32_t s_keys = 0;
static bool s_flash_ok = false; // FW範囲検査の結果
static uint8_t s_flash_page[FLASH_PAGE_SIZE];

static void poc_flash_erase_fn(void *param) {
    flash_range_erase(*(const uint32_t *)param, FLASH_SECTOR_SIZE);
}

typedef struct {
    uint32_t offs;
    const uint8_t *data;
} poc_prog_arg_t;

static void poc_flash_prog_fn(void *param) {
    const poc_prog_arg_t *a = (const poc_prog_arg_t *)param;
    flash_range_program(a->offs, a->data, FLASH_PAGE_SIZE);
}

static void poc_flash_test(void) {
    if (!s_flash_ok) return;
    uint32_t t0 = time_us_32();
    s_fseq++;
    memset(s_flash_page, 0, sizeof(s_flash_page));
    memcpy(s_flash_page, POC_FLASH_MAGIC, 8);
    s_flash_page[8] = (uint8_t)(s_fseq & 0xFFu);
    s_flash_page[9] = (uint8_t)((s_fseq >> 8) & 0xFFu);
    s_flash_page[10] = (uint8_t)((s_fseq >> 16) & 0xFFu);
    s_flash_page[11] = (uint8_t)((s_fseq >> 24) & 0xFFu);
    for (size_t i = 16; i < sizeof(s_flash_page); i++) {
        s_flash_page[i] = (uint8_t)((i ^ s_fseq) & 0xFFu);
    }
    uint32_t offs = POC_FLASH_OFFS;
    int rc1 = flash_safe_execute(poc_flash_erase_fn, &offs, UINT32_MAX);
    poc_prog_arg_t arg = { offs, s_flash_page };
    int rc2 = flash_safe_execute(poc_flash_prog_fn, &arg, UINT32_MAX);
    uint32_t dt = time_us_32() - t0;
    if (dt > s_fmax_us) s_fmax_us = dt;
    const uint8_t *rb = (const uint8_t *)(XIP_BASE + offs);
    if (rc1 == PICO_OK && rc2 == PICO_OK &&
        memcmp(rb, s_flash_page, sizeof(s_flash_page)) == 0) {
        s_fok++;
    } else {
        s_ffail++;
    }
}

static void stats_handler(btstack_timer_source_t *ts) {
    poc_shared_t cp;
    mutex_enter_blocking(&g_m);
    cp = g_s;
    mutex_exit(&g_m);
    // 単一UARTアダプタ運用のための生存表示。
    // framesが進んでいる間 (UART1受信中) はLED点灯固定、無受信時は1Hz点滅。
    // blind負荷中の「受信+parse進行中」を目視で確認するためのみ。
    static bool led = false;
    static uint32_t last_f = 0;
    bool active = (cp.frames != last_f);
    last_f = cp.frames;
    if (active) {
        led = true;
    } else {
        led = !led;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led ? 1 : 0);
    printf("POC t=%lus bt=%d usb=%d boot=%lu frames=%lu crc=%lu drop=%lu "
           "ovr=%lu iters=%lu fok=%lu ffail=%lu fmax_us=%lu keys=%lu\n",
           (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
           s_bt_ready ? 1 : 0, poc_usb_mounted() ? 1 : 0,
           (unsigned long)cp.boot_code,
           (unsigned long)cp.frames, (unsigned long)cp.crc_err,
           (unsigned long)cp.drop_ev, (unsigned long)cp.overruns,
           (unsigned long)cp.iters,
           (unsigned long)s_fok, (unsigned long)s_ffail,
           (unsigned long)s_fmax_us, (unsigned long)s_keys);
    if (poc_usb_send_neutral()) {
        s_keys++;
    }
    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

static void tud_handler(btstack_timer_source_t *ts) {
    poc_usb_pump();
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);
}

static void flash_handler(btstack_timer_source_t *ts) {
    poc_flash_test();
    btstack_run_loop_set_timer(ts, 5000);
    btstack_run_loop_add_timer(ts);
}

int main(void) {
    stdio_init_all();
    uart_init(LOG_UART, LOG_BAUD);
    gpio_set_function(LOG_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LOG_RX_PIN, GPIO_FUNC_UART);

    printf("\n=== pico-bcon dual-core PoC ===\n");

    // Flash試験域がFW本体と重ならない事を確認 (重なれば試験skip)。
    uintptr_t bend = (uintptr_t)&__flash_binary_end;
    if (bend < (uintptr_t)(XIP_BASE + POC_FLASH_OFFS)) {
        s_flash_ok = true;
        printf("flash test region ok (end=0x%08lx)\n", (unsigned long)bend);
    } else {
        printf("flash test region NG: binary too big, flash test skipped\n");
    }

    mutex_init(&g_m);
    memset(&g_s, 0, sizeof(g_s));
    multicore_launch_core1(core1_entry);

    uint32_t w0 = to_ms_since_boot(get_absolute_time());
    while (!g_core1_booted) {
        if (to_ms_since_boot(get_absolute_time()) - w0 > 2000) {
            printf("NG: core1 boot timeout\n");
            while (1) tight_loop_contents();
        }
    }
    printf("core1 boot=%lu detail=%lu victim=%d\n",
           (unsigned long)g_s.boot_code, (unsigned long)g_s.boot_detail,
           (int)multicore_lockout_victim_is_initialized(1));

    // wakeconと同一順序: CYW43 -> (USB pump相当) -> BTstack素子。
    if (cyw43_arch_init() != 0) {
        printf("NG: cyw43_arch_init\n");
        while (1) tight_loop_contents();
    }
    printf("cyw43 ok\n");

    poc_usb_init();

    l2cap_init();
    sdp_init();
    gap_discoverable_control(1);
    gap_connectable_control(1);
    gap_set_local_name("pico-bcon PoC");
    s_hci_cb.callback = &poc_packet_handler;
    hci_add_event_handler(&s_hci_cb);

    // BTstackは自発給電しない。wakecon (link_radio_update) 同様、
    // 明示 power ON が要る。無いと BTSTACK_EVENT_STATE が来ない (bt=0)。
    hci_power_control(HCI_POWER_ON);

    btstack_run_loop_set_timer_handler(&s_stats_timer, &stats_handler);
    btstack_run_loop_set_timer(&s_stats_timer, 1000);
    btstack_run_loop_add_timer(&s_stats_timer);

    btstack_run_loop_set_timer_handler(&s_tud_timer, &tud_handler);
    btstack_run_loop_set_timer(&s_tud_timer, 1);
    btstack_run_loop_add_timer(&s_tud_timer);

    btstack_run_loop_set_timer_handler(&s_flash_timer, &flash_handler);
    btstack_run_loop_set_timer(&s_flash_timer, 5000);
    btstack_run_loop_add_timer(&s_flash_timer);

    printf("ready. feed UART1 GP4/5 %lu baud v3 frames. log=UART0 115200\n",
           (unsigned long)POC_DATA_BAUD);
    btstack_run_loop_execute();

    while (1) tight_loop_contents();
    return 0;
}
