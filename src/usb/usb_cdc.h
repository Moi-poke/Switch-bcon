#ifndef BCON_USB_CDC_H
#define BCON_USB_CDC_H

/* 無線時 CDC-only / 有線 HID-only 切替用の TinyUSB CDC glue 公開 IF。
 * DEP-FREE: tusb.h を include しない (btstack.h との同TU衝突回避)。
 * bool/uint32_t のみで宣言する。実体は src/usb/usb_cdc.c が持つ。
 *
 * 注記:
 * - 無線profileは data(itf0) と log(itf1) の2系統。有線HID profileはCDCなし。
 * - RX は最大 64B/tick (task_poll が1回に汲み上げる上限)。
 * - TX は nonblock (write_bytes は実際に積めた分だけ返し、待たない)。
 * - log は probe_line の写しをリングに積み、1ms tick で送る (行単位で破棄)。
 * - DTR/RTS は無視 (connected は mount のみ見る)。
 * - コールバック内処理なし (rx/tx コールバックを置かず poll 駆動)。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CDC 受信を汲む (最大 64B/tick)。未接続・無データ時は *rx_n=0。 */
/* rx_out は 64B 以上、rx_n は必須。いずれか NULL なら何もしない。 */
void usb_cdc_task_poll(uint8_t *rx_out, int *rx_n);
/* USB profile選択 (起動時1回・tud_init前に呼ぶ。reboot適用)。
 * wired=trueでHID-only、falseでCDC-only。記述子cbが参照する。 */
void usb_cdc_set_profile(bool wired);
/* 現profile (既定true=HID-only)。未呼出時はHID-only。 */
bool usb_cdc_is_wired_profile(void);
/* CDC 接続状態 (mount のみ。DTR/RTS は見ない)。 */
bool usb_cdc_connected(void);
/* TX に積める残量 (nonblock 送信用)。 */
uint32_t usb_cdc_write_available(void);
/* TX へ積む。戻値は実際に積めた B 数 (待たない)。 */
uint32_t usb_cdc_write_bytes(const uint8_t *data, uint32_t len);
/* 積み分を送出する。 */
void usb_cdc_write_flush(void);

/* 無線profileのlog CDC (itf 1) へ1行を積む。HID profile時は何もしない。
 * 改行は本関数が付ける。非ブロッキング (満杯時は行単位で破棄)。 */
void usb_cdc_log_line(const char *s);
/* 積んだログをlog CDCへ送る (Core0の1ms tickから呼ぶ)。 */
void usb_cdc_log_task(void);
/* log CDCが列挙済みか (診断用)。 */
bool usb_cdc_log_ready(void);
/* 破棄した行数 (診断用)。 */
uint32_t usb_cdc_log_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* BCON_USB_CDC_H */
