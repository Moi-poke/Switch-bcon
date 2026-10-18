// main_mini.c -- gyro-mini: ジャイロ再現の最小FW。
// USB有線ProCon列挙 + 80/01ハンドシェイク + 0x30周期送信 + IMU波形のみ。
// 削る: Core1/UART1/BT/CYW43/Flash/TLV/v3/ボタン入力/CDC-data。
// 応答バイトは本線 usb_* と同一 (同一ソースをリンクする)。
// mini固有: B自動押下 (Switchの認識用)。timer/周期/静止1gは本線と同一。
//
// 使い方: ビルド→UF2書込→SwitchドックUSBへ接続。
//   0x40(1)+0x03(0x30) を受けると SINE波形が 0x30 IMU領域に載る。
//   UART0ログ (GP0/1 115200) に起動行・ハンドシェイク・STEP_triggerを出す。
//   STEP再トリガは LOG入力ではなく BOOTSEL長押し代替: UART0-RXへ "step" 行。
//   (USB接続中もUART0-RXは空いているため、FTDI等で送れる)

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "usb_wired.h"
#include "usb_hid.h"
#include "usb_cdc.h"
#include "imu_dummy.h"

#define LOG_UART uart0
#define LOG_BAUD 115200
#define LOG_TX_PIN 0
#define LOG_RX_PIN 1

// usb_wired.c が読む入力状態。miniは固定中立 (ボタン離し・中央)。
uint8_t bcon_btn[3] = { 0u, 0u, 0u };
uint16_t bcon_lx = 0x800u, bcon_ly = 0x800u;
uint16_t bcon_rx = 0x800u, bcon_ry = 0x800u;
uint8_t bcon_mac[6];
uint32_t bcon_usb_rumble_n;
// miniはPC由来IMUなし (IMU_DUMMY生成器のみ)。valid=false固定で静止値路へ。
int16_t bcon_imu[6] = { 0, 0, 0, 0, 0, 0 };
bool bcon_imu_valid = false;

void probe_line(const char *s)
{
    if (s != NULL) {
        printf("%s\n", s);
    }
}

// LOG入力1行バッファ ("step" 行でSTEP再トリガ。SINE時は無害)。
static char s_logline[32];
static unsigned s_logline_n;

static void log_poll(uint32_t now_ms)
{
    // uart_is_readable で有無を見てから読む (uart_getc は空でblockする)。
    while (uart_is_readable(LOG_UART)) {
        int c = (int)uart_getc(LOG_UART);
        if (c == '\r' || c == '\n') {
            if (s_logline_n > 0u) {
                char imud_line[128];
                if (imu_dummy_is_step_line(s_logline, s_logline_n)) {
                    imu_dummy_step_trigger(now_ms, imud_line, sizeof(imud_line));
                    probe_line(imud_line);
                }
                s_logline_n = 0u;
            }
        } else if (s_logline_n < sizeof(s_logline)) {
            s_logline[s_logline_n++] = (char)c;
        }
    }
}

int main(void)
{
    pico_unique_board_id_t uid;
    char line[128];
    uint32_t last_ms = 0u, last_log = 0u;
    int k;

    stdio_init_all();
    uart_init(LOG_UART, LOG_BAUD);
    gpio_set_function(LOG_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LOG_RX_PIN, GPIO_FUNC_UART);
    uart_set_fifo_enabled(LOG_UART, true);

    printf("\n=== gyro-mini ===\n");

    // 自MAC: OUI 7C:BB:8A + unique末尾 (本線 link_init と同一式)。
    // bcon_mac は応答順 (反転済み)。
    pico_get_unique_board_id(&uid);
    {
        uint8_t addr[6];
        addr[0] = 0x7cu;
        addr[1] = 0xBBu;
        addr[2] = 0x8Au;
        addr[3] = uid.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3];
        addr[4] = uid.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2];
        addr[5] = uid.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1];
        for (k = 0; k < 6; k++) {
            bcon_mac[k] = addr[5 - k];
        }
        printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n",
               addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    }

    // HID-only profile → role ProCon(0) → USB init → 有効化。
    usb_cdc_set_profile(true);
    usb_set_role(0u);
    usb_wired_init();
    usb_wired_set_enabled(true);
    probe_line("mini ready. connect to Switch dock USB.");
    // B自動押下: handshake完了後に3回 (各120ms押下/120ms離し)。
    // Switchはボタン入力があるまでコントローラを認識しない個体があるため。
    // B=輸送b0 bit2 (0x04)。完了後は中立に戻す。
    bool b_auto_done = false;
    uint32_t b_auto_t0 = 0u;
    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now == last_ms) {
            tight_loop_contents();
            continue;
        }
        last_ms = now;
        log_poll(now);
        if (!b_auto_done && usb_wired_handshake_done()) {
            if (b_auto_t0 == 0u) {
                b_auto_t0 = now;
                probe_line("mini B-auto start");
            }
            {
                uint32_t el = now - b_auto_t0;
                // 720msで3往復 (120押/120離 x3)。以降は中立+完了。
                if (el < 720u) {
                    bcon_btn[0] = ((el % 240u) < 120u) ? 0x04u : 0x00u;
                } else {
                    bcon_btn[0] = 0x00u;
                    b_auto_done = true;
                    probe_line("mini B-auto done");
                }
            }
        }
        usb_wired_task(now);
#if IMU_DUMMY_DIAG
        // §1診断行は usb_wired_task 内で出す (1秒毎)。
        (void)line;
        (void)last_log;
#else
        if (now - last_log >= 1000u) {
            usb_wired_stats_t st;
            last_log = now;
            usb_wired_get_stats(&st);
            snprintf(line, sizeof(line),
                     "mini mount=%lu rx80=%lu last80=%02x tx81=%lu tx21=%lu in30=%lu sof=%lu",
                     (unsigned long)st.mount, (unsigned long)st.rx80,
                     (unsigned)st.last80, (unsigned long)st.tx81,
                     (unsigned long)st.tx21, (unsigned long)st.in30,
                     (unsigned long)st.sof);
            probe_line(line);
        }
#endif
    }
}
