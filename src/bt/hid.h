#ifndef BCON_BT_HID_H
#define BCON_BT_HID_H

/* 移植元: pico-wakecon src/hid.h/c。
 * Switch 1 Pro Controller 入力・HID 応答・送信。
 * 変更点: ASCII行パーサ (S/O) を削除 (バイナリv3が継承)。
 * 出力レポート 0x10 (振動) の受信計数は bcon_bt_rumble_n (main.c所有)。
 * probe_line は互換shim (bt_compat.h。main.cでprintf実装)。 */

#include <stdbool.h>
#include <stdint.h>

#include "btstack.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t probe_btn[3];
extern uint16_t probe_lx, probe_ly, probe_rx, probe_ry;
extern uint32_t probe_btn_press_count;
extern bool probe_btn_was_down;
void probe_input_reset(void);

extern uint16_t probe_hid_cid;
extern bool probe_full_mode;
extern uint8_t probe_report_timer;
extern uint32_t probe_empty_sent;
extern uint32_t probe_reply_sent;
extern uint32_t probe_state_sent;
extern uint32_t probe_out_report_count;
extern uint8_t probe_subcmd_seen[];
extern uint8_t probe_subcmd_seen_n;
extern uint16_t probe_out_len_min;
extern uint16_t probe_out_len_max;
extern uint8_t probe_player_id;
extern bool probe_player_seen;
extern bool probe_imu_enabled;
extern bool probe_vibration_enabled;
extern uint8_t probe_input_mode;
extern uint8_t probe_hci_state_arg;
extern uint32_t probe_hci_state_count;
extern bool probe_send_now_wanted;
extern uint32_t probe_color_set_count;

uint16_t probe_build_reply(uint8_t ack, uint8_t subcmd);
void probe_report_handler(uint16_t cid, hid_report_type_t report_type,
                          uint16_t report_id, int report_size,
                          uint8_t *report);
void probe_hid_reset(void);
void probe_request_send(void);
void probe_can_send_now(void);
uint32_t probe_send_interval_ms(void);
/* 200ms 無通信で中立化する番犬。不正行でも線は生きているため時刻は進める。
 * 注意: bconはmain.c側で共有u32を一元中立化するため poll は呼ばない。
 * 関数自体は移植維持 (単体利用の互換のため)。 */
void probe_watchdog_feed(uint32_t now_ms);
void probe_watchdog_poll(uint32_t now_ms);

/* Switch振動出力 (BT 0x10) の受信累計。ACK＋破棄し計数のみ残す (Step 2)。
 * 所有元は main.c。STATUS bit7 の源。 */
extern uint32_t bcon_bt_rumble_n;

#ifdef __cplusplus
}
#endif

#endif
