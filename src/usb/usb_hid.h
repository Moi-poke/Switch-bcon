#ifndef BCON_USB_HID_H
#define BCON_USB_HID_H
/* 移植元: pico-wakecon src/usb_hid.h/c。応答バイトは同一。
 * 出典継承: 0x01系雛形は knflrpn/2wiCC (MIT) 実働値。記述子はToadKing写し。
 * 変更点: 入力btn[3]は ctrl_pack_btn3() (src/proto/pack.c) の出力
 * (Nintendo順3B) を受ける。u32→3B変換自体はここでは行わない。 */
#include <stdbool.h>
#include <stdint.h>
#include "dispatch.h" /* v3_session_t (純粋型のみ。tusb/btstack非依存は維持) */
#ifdef __cplusplus
extern "C" {
#endif
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type);
bool usb_req_is_handshake(const uint8_t *req, int req_len);

/* 0x01 サブコマンド応答 (64B の 0x21 レポート) の入力文脈。
 * btn は pack済み 3B、stick は 12bit 値（中央 0x800。LEN=8受信は取込時に<<4済み）、
 * mac は自アドレス。
 * role は EMUL_ROLE_* (src/proto/protocol.h)。0=ProCon 既定。
 * ゼロ初期化した ctx は role=0 (ProCon) として扱う。範囲外値は ProCon 扱い。 */
typedef struct {
    uint8_t btn[3];
    uint16_t lx, ly, rx, ry;
    uint8_t timer;
    uint8_t mac[6];
    uint8_t player;
    uint8_t role;
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

/* エミュレーション role の伝達路 (起動時に1回だけ呼ぶ。USB init/列挙の前)。
 * main.c (T4所有) が store_emulate_load_def() の値をここへ渡す想定。
 * usb_set_personality(const personality_t *) ではなく uint8_t を受ける理由:
 * personality.c は test_usb にも firmware target にもリンクされていないため、
 * USB 側から personality_* 関数を呼べない (T4 が build 配線を持つ)。
 * 値は PERSONALITY_TABLE (src/bt/personality.c) の写しで、USB 単体で閉じる。
 * 未呼出・NULL相当・範囲外 (>2) は ProCon (0) に倒す。 */
void usb_set_role(uint8_t role);
uint8_t usb_get_role(void);
/* role 越えの固定値参照 (いずれも範囲外 role は ProCon 値を返す)。
 * PID・製品名は PERSONALITY_TABLE の usb_pid / usb_product の写し。
 * dev_type は 0x02 機器情報応答・0x81 応答用 (usb 側は ProCon=0x03)。 */
uint16_t usb_pid_for_role(uint8_t role);
const char *usb_product_for_role(uint8_t role);
uint8_t usb_devtype_for_role(uint8_t role);
/* ProCon pack済み3B -> role 輸送3B (joy_pack_btn3 と同値のはず; T9 cross-check
 * が担保する host-test 用 export。FW 動作は変えない)。 */
void usb_role_pack_btn3(const uint8_t procon3[3], uint8_t role,
                        uint8_t out3[3]);
/* mount時81 01先制通知の可否判定 (Joy役のみ)。条件を純粋関数に切り出し、
 * host test で固定する。FW 動作は usb_wired_task 側がこの戻りで決める。 */
bool usb_kick81_due(uint8_t role, bool wired_en, bool mounted,
                    bool kick_done, bool pend_valid);
/* 有線プレイヤーランプ配管 (無線 probe_player_id/seen の鏡像)。
 * 0x30 受信値を溜めるだけだった dead-end (usb_wired.c) を host-testable な
 * 出入口で外へ出す。save は tud_hid_set_report_cb (0x30受信) が呼び、
 * get は 0x31 読戻し (ctx.player) が読む。feed は wired_loop 側が
 * 1ms tick 毎に呼び、格納値を v3_session へ移して v3_player_tick で
 * ACT_SEND_PLAYER_INFO を queue する。いずれも純粋な格納・転記のみ
 * (printf/BT/CYW43/Flash なし。Core1安全・parser路可)。 */
void usb_wired_player_save(uint8_t id);
uint8_t usb_wired_player_get(void);
void usb_wired_feed_player(v3_session_t *s);
#ifdef __cplusplus
}
#endif
#endif
