#include <stddef.h>
#include "usb_cdc.h"

/* TinyUSB CDC glue 実体。本ファイルのみ tusb.h を include してよい。
 * usb_cdc.h は DEP-FREE (tusb.h 非 include) を保つ。
 * コールバック内処理なし: poll 駆動のみ。DTR/RTS は見ない。 */
/* USB profile (実行時選択・起動時1回。既定true=HID-only)。tusb非依存のため */
/* profile storageは条件分岐の外に置く。記述子cb (usb_descriptors.c) が参照。 */
static bool s_wired_profile = true;
void usb_cdc_set_profile(bool wired) { s_wired_profile = wired; }
bool usb_cdc_is_wired_profile(void) { return s_wired_profile; }

#include "tusb.h"

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
    uint32_t avail = tud_cdc_available();
    if (avail == 0) {
        return;
    }
    if (avail > 64) {
        avail = 64;
    }
    uint32_t n = tud_cdc_read(rx_out, avail);
    *rx_n = (int)n;
}

bool usb_cdc_connected(void)
{
    /* DTR/RTS 無視: tud_cdc_connected() は DTR アクティブを見るため使わず、 */
    /* enumerate 済みのみ見る tud_cdc_ready() を使う。HID profile時は */
    /* CDC I/Fが列挙されないため available=0・ready=false 相当で不応答。 */
    if (usb_cdc_is_wired_profile()) {
        return false;
    }
    return tud_cdc_ready();
}

uint32_t usb_cdc_write_available(void)
{
    if (!tud_ready()) {
        return 0;
    }
    return tud_cdc_write_available();
}

uint32_t usb_cdc_write_bytes(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    if (!tud_ready()) {
        return 0;
    }
    return tud_cdc_write(data, len);
}

void usb_cdc_write_flush(void)
{
    if (!tud_ready()) {
        return;
    }
    (void)tud_cdc_write_flush();
}
