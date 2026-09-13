#ifndef BCON_USB_HID_H
#define BCON_USB_HID_H
/* 移植元: pico-wakecon src/usb_hid.h/c。応答バイトは同一。
 * 出典継承: 0x01系雛形は knflrpn/2wiCC (MIT) 実働値。記述子はToadKing写し。
 * 変更点: 入力btn[3]は ctrl_pack_btn3() (src/proto/pack.c) の出力
 * (Nintendo順3B) を受ける。u32→3B変換自体はここでは行わない。 */
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type);
bool usb_req_is_handshake(const uint8_t *req, int req_len);

/* 0x01 サブコマンド応答 (64B の 0x21 レポート) の入力文脈。
 * btn は pack済み 3B、stick は PC 側 8bit 値、mac は自アドレス。 */
typedef struct {
    uint8_t btn[3];
    uint8_t lx, ly, rx, ry;
    uint8_t timer;
    uint8_t mac[6];
    uint8_t player;
} usb_sub_ctx_t;

/* 0x01 xx → 64B の 0x21 応答。02/03/10/30/40/48 と既定 ack。
 * 成功時 64、不正時 0。Pico/BTstack 非依存。 */
int usb_build_21_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const usb_sub_ctx_t *ctx);
/* 12B 状態部 (2wiCC ControllerData 互換: timer・電池・3B ボタン・
 * 6B スティック・振動)。0x21 接頭と 0x30 本体で共有する。 */
void usb_pack_controller_data(uint8_t out12[12], const usb_sub_ctx_t *ctx);
/* 0x30 入力レポート 64B (ID + 12B 状態 + 36B IMU(0) + 15B 埋め)。
 * 成功時 64。IMU 無効時は 0 のまま (2wiCC 通り)。 */
int usb_build_30_report(const usb_sub_ctx_t *ctx, uint8_t out64[64]);
#ifdef __cplusplus
}
#endif
#endif
