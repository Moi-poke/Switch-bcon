#include <stddef.h>
#include <string.h>
#include "usb_cdc.h"

/* TinyUSB CDC glue 実体。本ファイルのみ tusb.h を include してよい。
 * usb_cdc.h は DEP-FREE (tusb.h 非include) を保つ。
 * コールバック内処理なし: poll 駆動のみ。DTR/RTS は見ない。
 * 無線profileは data(itf0) + log(itf1) の2系統。data側の TinyUSB 呼出は
 * instance 0 を明示する。log は probe_line の写しをリングに積み tick で送る。 */
/* USB profile (実行時選択・起動時1回。既定true=HID-only)。tusb非依存のため */
/* profile storageは条件分岐の外に置く。記述子cb (usb_descriptors.c) が参照。 */
static bool s_wired_profile = true;
void usb_cdc_set_profile(bool wired) { s_wired_profile = wired; }
bool usb_cdc_is_wired_profile(void) { return s_wired_profile; }

#include "tusb.h"

/* ---- data CDC (itf 0) ---- */

void usb_cdc_task_poll(uint8_t *rx_out, int *rx_n)
{
    if (rx_n == NULL) {
        return;
    }
    *rx_n = 0;
    if (rx_out == NULL) {
        return;
    }
    if (!tud_ready()) {
        return;
    }
    uint32_t avail = tud_cdc_n_available(0u);
    if (avail == 0) {
        return;
    }
    if (avail > 64) {
        avail = 64;
    }
    uint32_t n = tud_cdc_n_read(0u, rx_out, avail);
    *rx_n = (int)n;
}

bool usb_cdc_connected(void)
{
    /* DTR/RTS 無視: tud_cdc_n_connected() は DTR アクティブを見るため使わず、 */
    /* enumerate 済みのみ見る tud_cdc_n_ready() を使う。HID profile時は */
    /* CDC I/Fが列挙されないため available=0・ready=false 相当で不応答。 */
    if (usb_cdc_is_wired_profile()) {
        return false;
    }
    return tud_cdc_n_ready(0u);
}

uint32_t usb_cdc_write_available(void)
{
    if (!tud_ready()) {
        return 0;
    }
    return tud_cdc_n_write_available(0u);
}

uint32_t usb_cdc_write_bytes(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    if (!tud_ready()) {
        return 0;
    }
    return tud_cdc_n_write(0u, data, len);
}

void usb_cdc_write_flush(void)
{
    if (!tud_ready()) {
        return;
    }
    (void)tud_cdc_n_write_flush(0u);
}

/* ---- log CDC (itf 1) ---- */
/* probe_line は BT コールバック (Core0) からも呼ばれるため、ここでは TinyUSB を
 * 触らずリングに積むだけにする。送出は tick の usb_cdc_log_task が行う。 */

#define CDC_LOG_RING 2048u
#define CDC_LOG_MASK (CDC_LOG_RING - 1u)
#define CDC_LOG_LINE_MAX 200u

static char s_log_ring[CDC_LOG_RING];
static uint16_t s_log_r;
static uint16_t s_log_w;
static uint32_t s_log_drop;

void usb_cdc_log_line(const char *s)
{
    size_t n;
    uint16_t used;
    size_t i;
    if (s == NULL || usb_cdc_is_wired_profile()) {
        return;
    }
    n = strlen(s);
    if (n > CDC_LOG_LINE_MAX) {
        n = CDC_LOG_LINE_MAX;
    }
    used = (uint16_t)(s_log_w - s_log_r);
    if ((uint32_t)used + (uint32_t)n + 1u > CDC_LOG_RING) {
        s_log_drop++;
        return;
    }
    for (i = 0u; i < n; i++) {
        s_log_ring[s_log_w & CDC_LOG_MASK] = s[i];
        s_log_w++;
    }
    s_log_ring[s_log_w & CDC_LOG_MASK] = '\n';
    s_log_w++;
}

bool usb_cdc_log_ready(void)
{
    if (usb_cdc_is_wired_profile()) {
        return false;
    }
    return tud_cdc_n_ready(1u);
}

uint32_t usb_cdc_log_dropped(void)
{
    return s_log_drop;
}

void usb_cdc_log_task(void)
{
    if (!usb_cdc_log_ready()) {
        return;
    }
    /* log側のRXは使わない。未読のままFIFOが詰まるとホストの書込を
     * NAKし続けるため、届いた分は捨てる。 */
    while (tud_cdc_n_available(1u) > 0u) {
        uint8_t dump[32];
        (void)tud_cdc_n_read(1u, dump, sizeof(dump));
    }
    while (s_log_r != s_log_w) {
        uint32_t avail = tud_cdc_n_write_available(1u);
        uint16_t idx = (uint16_t)(s_log_r & CDC_LOG_MASK);
        uint32_t pend = (uint32_t)(uint16_t)(s_log_w - s_log_r);
        uint32_t chunk = CDC_LOG_RING - (uint32_t)idx;
        uint32_t put;
        if (avail == 0u) {
            break;
        }
        if (chunk > pend) {
            chunk = pend;
        }
        if (chunk > avail) {
            chunk = avail;
        }
        put = tud_cdc_n_write(1u, &s_log_ring[idx], chunk);
        if (put == 0u) {
            break;
        }
        s_log_r = (uint16_t)(s_log_r + put);
    }
    (void)tud_cdc_n_write_flush(1u);
}
