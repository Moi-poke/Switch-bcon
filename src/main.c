// main.c -- pico-bcon 有線FW (Task 3)。
// PC →(UART1)→ Pico →(USB-HID公式ProCon)→ Switch 1/2ドック。
//
// 構成 (spec §12): 無線 (CYW43/BTstack) は上げない。
// CYW43/BT動作中はSwitch 2ドックがUSB列挙しない実測 (wakecon知見) のため。
// 無線の復帰は Task 4 (WIRED_MODE管理) で行う。
//
// 役割分担 (PoC確定): Core1＝UART1 DMA＋parser＋mutex pushのみ。
// Core0＝1ms poll (共有u32→pack→bcon_*→usb_wired_task)＋log＋Flash試験。

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/flash.h"
#include "pico/unique_id.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "protocol.h"
#include "pack.h"
#include "usb_wired.h"

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

// ---- DMA ring: 16KB (PoC確定。Flash stall ~39msに対し十分な余裕) ----
#define RING_BITS 14
#define RING_SIZE (1u << RING_BITS)
static uint8_t dma_ring[RING_SIZE] __attribute__((aligned(RING_SIZE)));

// ---- Flash試験: 最終4Kセクタ (PoCと同一。FW本体と重なればskip) ----
#define BCON_FLASH_OFFS (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define BCON_FLASH_MAGIC "BCONPOC1"
extern char __flash_binary_end[];

// ---- Core1 -> Core0 共有状態 (mutex保護。Core1はpushのみ) ----
typedef struct {
    ctrl_state_t state;   // 最新STATE (u32＋sticks)
    uint8_t  last_seq;
    uint8_t  last_type;
    uint32_t frames;
    uint32_t crc_err;
    uint32_t drop_ev;
    uint32_t iters;       // Core1 drain loop反復 (生存証明)
    uint32_t overruns;
    uint32_t boot_code;   // 0=boot中 1=selftest PASS 2=FAIL 3=走行中
    uint32_t boot_detail;
} bcon_shared_t;

static mutex_t g_m;
static bcon_shared_t g_s;
static volatile bool g_core1_booted = false;

// ---- USB層への供給 (所有元はCore0。usb_wired.cは読みのみ) ----
uint8_t bcon_btn[3];
uint8_t bcon_lx = 0x80u, bcon_ly = 0x80u, bcon_rx = 0x80u, bcon_ry = 0x80u;
uint8_t bcon_mac[6]; // 応答順 (初期化で反転済み)

// ---------------- Core1: parser callback (mutex pushのみ) ----------------
static link_stats_t s_pst;
static parser_t s_parser;

static void bcon_frame_cb(uint8_t type, const uint8_t *p, uint8_t len,
                          uint8_t seq, void *user) {
    (void)user;
    mutex_enter_blocking(&g_m);
    if (type == T_STATE && len == 8) {
        g_s.state.buttons =
            (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
            ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        g_s.state.lx = p[4];
        g_s.state.ly = p[5];
        g_s.state.rx = p[6];
        g_s.state.ry = p[7];
    } else if (type == T_NEUTRAL && len == 0) {
        g_s.state.buttons = 0u;
        g_s.state.lx = g_s.state.ly = 0x80u;
        g_s.state.rx = g_s.state.ry = 0x80u;
    }
    g_s.last_seq = seq;
    g_s.last_type = type;
    g_s.frames++;
    g_s.crc_err = s_pst.err_crc;
    g_s.drop_ev = s_pst.err_drop;
    mutex_exit(&g_m);
}

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
    // NEUTRAL適用の確認 (全中立に戻っている事)。
    if (g_s.state.buttons != 0u || g_s.state.lx != 0x80u) return 0x40;
    return 0;
}

static int g_dma_ch = -1;
static uint32_t g_rd = 0;

static void core1_entry(void) {
    // PoC確定: lockout victim初期化 (flash_safe_executeの相手側)。
    bool ok_init = flash_safe_execute_core_init();
    (void)ok_init;

    memset(&s_pst, 0, sizeof(s_pst));
    parser_init(&s_parser, bcon_frame_cb, NULL, &s_pst);
    g_s.state.lx = g_s.state.ly = 0x80u;
    g_s.state.rx = g_s.state.ry = 0x80u;

    uint32_t st = core1_selftest();
    mutex_enter_blocking(&g_m);
    g_s.boot_code = (st == 0) ? 1u : 2u;
    g_s.boot_detail = st;
    mutex_exit(&g_m);

    uart_init(DATA_UART, POC_DATA_BAUD);
    gpio_set_function(DATA_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(DATA_RX_PIN, GPIO_FUNC_UART);
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

// ---------------- Core0: Flash試験 (PoCと同一・5秒周期) ----------------
static uint32_t s_fok = 0, s_ffail = 0, s_fmax_us = 0, s_fseq = 0;
static bool s_flash_ok = false;
static uint8_t s_flash_page[FLASH_PAGE_SIZE];

static void bcon_flash_erase_fn(void *param) {
    flash_range_erase(*(const uint32_t *)param, FLASH_SECTOR_SIZE);
}

typedef struct {
    uint32_t offs;
    const uint8_t *data;
} bcon_prog_arg_t;

static void bcon_flash_prog_fn(void *param) {
    const bcon_prog_arg_t *a = (const bcon_prog_arg_t *)param;
    flash_range_program(a->offs, a->data, FLASH_PAGE_SIZE);
}

static void bcon_flash_test(void) {
    if (!s_flash_ok) return;
    uint32_t t0 = time_us_32();
    s_fseq++;
    memset(s_flash_page, 0, sizeof(s_flash_page));
    memcpy(s_flash_page, BCON_FLASH_MAGIC, 8);
    s_flash_page[8] = (uint8_t)(s_fseq & 0xFFu);
    s_flash_page[9] = (uint8_t)((s_fseq >> 8) & 0xFFu);
    s_flash_page[10] = (uint8_t)((s_fseq >> 16) & 0xFFu);
    s_flash_page[11] = (uint8_t)((s_fseq >> 24) & 0xFFu);
    for (size_t i = 16; i < sizeof(s_flash_page); i++) {
        s_flash_page[i] = (uint8_t)((i ^ s_fseq) & 0xFFu);
    }
    uint32_t offs = BCON_FLASH_OFFS;
    int rc1 = flash_safe_execute(bcon_flash_erase_fn, &offs, UINT32_MAX);
    bcon_prog_arg_t arg = { offs, s_flash_page };
    int rc2 = flash_safe_execute(bcon_flash_prog_fn, &arg, UINT32_MAX);
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

// ---------------- Core0 main ----------------
int main(void) {
    stdio_init_all();
    uart_init(LOG_UART, LOG_BAUD);
    gpio_set_function(LOG_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LOG_RX_PIN, GPIO_FUNC_UART);

    printf("\n=== pico-bcon wired (Task 3) ===\n");

    // 自MAC: unique ID由来 (応答順に反転して保持)。Task 4のlink層が継承する。
    {
        pico_unique_board_id_t uid;
        uint8_t mb[6];
        pico_get_unique_board_id(&uid);
        for (int k = 0; k < 6; k++) mb[k] = uid.id[2 + k];
        mb[0] |= 0x02u; // locally administered
        for (int k = 0; k < 6; k++) bcon_mac[k] = mb[5 - k];
        printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n",
               mb[0], mb[1], mb[2], mb[3], mb[4], mb[5]);
    }

    uintptr_t bend = (uintptr_t)&__flash_binary_end;
    if (bend < (uintptr_t)(XIP_BASE + BCON_FLASH_OFFS)) {
        s_flash_ok = true;
        printf("flash test region ok (end=0x%08lx)\n", (unsigned long)bend);
    } else {
        printf("flash test region NG: binary too big, flash test skipped\n");
    }

    mutex_init(&g_m);
    memset(&g_s, 0, sizeof(g_s));
    g_s.state.lx = g_s.state.ly = 0x80u;
    g_s.state.rx = g_s.state.ry = 0x80u;
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

    // 有線FWは無線を上げない (spec §12)。ドック列挙のため。
    usb_wired_init();
    usb_wired_set_enabled(true);
    printf("ready. feed UART1 GP4/5 %lu baud v3 frames. log=UART0 115200\n",
           (unsigned long)POC_DATA_BAUD);

    uint32_t last_ms = 0u, last_log = 0u, last_flash = 0u;
    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now == last_ms) {
            tight_loop_contents();
            continue;
        }
        last_ms = now;

        // 共有u32 → pack → bcon_* (Core0単独。1ms tick)。
        {
            bcon_shared_t cp;
            uint8_t pb[3];
            mutex_enter_blocking(&g_m);
            cp = g_s;
            mutex_exit(&g_m);
            ctrl_pack_btn3(&cp.state, pb);
            bcon_btn[0] = pb[0];
            bcon_btn[1] = pb[1];
            bcon_btn[2] = pb[2];
            bcon_lx = cp.state.lx;
            bcon_ly = cp.state.ly;
            bcon_rx = cp.state.rx;
            bcon_ry = cp.state.ry;
        }

        usb_wired_task(now);

        if (now - last_flash >= 5000u) {
            last_flash = now;
            bcon_flash_test();
        }

        if (now - last_log >= 1000u) {
            usb_wired_stats_t ws;
            bcon_shared_t cp;
            last_log = now;
            usb_wired_get_stats(&ws);
            mutex_enter_blocking(&g_m);
            cp = g_s;
            mutex_exit(&g_m);
            printf("BCON t=%lus hs=%d mnt=%d rx80=%lu last=%02x tx81=%lu "
                   "tx21=%lu in30=%lu sof=%lu f01=%02x%02x%02x%02x "
                   "spi=%04x:%u frames=%lu crc=%lu drop=%lu ovr=%lu "
                   "fok=%lu ffail=%lu\n",
                   (unsigned long)(now / 1000),
                   usb_wired_handshake_done() ? 1 : 0,
                   usb_wired_is_configured() ? 1 : 0,
                   (unsigned long)ws.rx80, ws.last80,
                   (unsigned long)ws.tx81, (unsigned long)ws.tx21,
                   (unsigned long)ws.in30, (unsigned long)ws.sof,
                   ws.first8[0], ws.first8[1], ws.first8[2], ws.first8[3],
                   ws.spi_a, ws.spi_n,
                   (unsigned long)cp.frames, (unsigned long)cp.crc_err,
                   (unsigned long)cp.drop_ev, (unsigned long)cp.overruns,
                   (unsigned long)s_fok, (unsigned long)s_ffail);
        }
    }
    return 0;
}
