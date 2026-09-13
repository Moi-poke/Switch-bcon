#ifndef BCON_USB_WIRED_H
#define BCON_USB_WIRED_H

/* 移植元: pico-wakecon src/usb_wired.h/c (状態機・統計は同一)。
 * 有線 Pro Controller の TinyUSB glue 公開 IF (有効化・状態・タスク)。
 * 変更点: 入力状態の所有元が main.c の bcon_* (hid/linkのprobe_*ではない)。
 * BTstack依存は持たない (tusb.h と btstack.h の衝突回避)。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_wired_init(void);
void usb_wired_task(uint32_t now_ms);
/* TinyUSB のポンプ単体。BTstack のタイマ外から呼ぶための窓口。 */
void usb_wired_pump(void);
/* ホストに再列挙させる (pull-up の off/on)。 */
void usb_wired_reconnect(void);
/* USB デバイスを外す (給電は続く)。 */
void usb_wired_disconnect(void);
void usb_wired_set_enabled(bool en);
bool usb_wired_is_enabled(void);
bool usb_wired_is_configured(void);
bool usb_wired_handshake_done(void);

/* USB バス側の診断計数。列挙〜応答のどこで止まるかを見る。
 * mount/unmount はホストの bus reset/configure 由来。rx80/last80/tx81 は
 * 80 xx 受信と 81 応答。in30 は入力レポート送出。 */
typedef struct {
    uint32_t mount;
    uint32_t unmount;
    uint32_t rx80;
    uint8_t last80;
    uint32_t tx81;
    uint32_t in30;
    /* バス生存の証拠。sof はホストの 1ms フレーム数 (0 のままなら電気的に無音)。
     * susp/rsm はホストのサスペンド/レジューム。 */
    uint32_t sof;
    uint32_t susp;
    uint32_t resm;
    /* ホスト→デバイス受信の経路内訳。ep_rx=OUT EP 経路、
     * ctl_rx=control 経路。hist は直近 4 件の 80 sub、hist01 は直近 4 件の
     * 01 sub ([3] が最新)。tx21 は 0x21 応答の送出数。 */
    uint32_t ep_rx;
    uint32_t ctl_rx;
    uint8_t hist[4];
    uint8_t hist01[4];
    uint32_t tx21;
    /* 未知レポート ID の最終値・長さ・回数。 */
    uint8_t unk_id;
    uint8_t unk_len;
    uint32_t unk_n;
    /* 0x01 サブコマンドの初出順 (重複なし、最大8件で打切り)。 */
    uint8_t first8[8];
    uint8_t first8_n;
    /* 直近の 0x10 SPI 読出の番地・長さ・回数。 */
    uint16_t spi_a;
    uint8_t spi_n;
    uint16_t spi_c;
    /* 直近の 0x01 要求のデータバイト (sub の次。種別・モード等の判別用)。 */
    uint8_t subd;
    /* 2B 未満の短い受信の回数。 */
    uint16_t short_n;
} usb_wired_stats_t;
void usb_wired_get_stats(usb_wired_stats_t *st);

#ifdef __cplusplus
}
#endif

#endif
