#ifndef LINK_H
#define LINK_H

/* 接続管理・再接続・キャプチャ・ビーコン再生。
 * Classic HID と BLE スキャン/広告は時期で使い分ける。同時使用しない。 */

#include <stdbool.h>
#include <stdint.h>

#include "btstack.h"
#include "cap.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bd_addr_t probe_host_addr;
extern bool probe_host_known;
extern bool probe_outgoing_tried;
extern uint32_t probe_reconnect_tries;
extern uint8_t probe_reconnect_report;
extern bool probe_reconnect_pending;
extern uint32_t probe_giveup_count;
extern uint32_t probe_connected_at_ms;
extern uint8_t probe_ssp_count;
extern bd_addr_t probe_addr;

extern cap_table_t probe_cap_table;
extern cap_saved_t probe_cap_saved;
extern bool probe_cap_valid;
extern bool probe_scanning;
extern bool probe_beacon;

void link_init(void);
void link_reconnect_handler(btstack_timer_source_t *ts);
void link_note_disconnected(void);
void link_mark_connected(void);
/* 有線/無線の切替に伴う Classic 側の始末。wired=true で接続中なら能動切断し
 * 待ち受けも止める (着信再接続の防止)。false で待ち受けに戻す。 */
void link_apply_wired_mode(bool wired);
/* 電波の要否を現在の取込・再生・有線状態から決めて電源・待ち受けへ反映する。
 * 取込・再生の開始時と終了時に呼ぶ。 */
void link_radio_update(void);
/* BTstack の動作状態を link 層へ伝える。main の BTSTACK_EVENT_STATE で呼ぶ。
 * beacon の広告開始は WORKING まで遅らせるために使う。 */
void link_note_bt_working(bool working);
bool link_bt_working(void);
int link_key_count(void);

/* 取込。LE 接続中・Classic 接続中は不可。 */
bool link_cap_start(uint8_t seconds);
void link_cap_report(const uint8_t *packet);
void link_cap_tick(uint32_t now_ms);
uint8_t link_cap_used(void);
const cap_entry_t *link_cap_entry(uint8_t i);
void link_cap_clear(void);

/* 再生。保存済み必須。1.5 秒出して元に戻す。 */
bool link_beacon_start(void);
void link_beacon_tick(uint32_t now_ms);
void link_poll(uint32_t now_ms);  /* 取込期限・再生期限の番 */

/* LE 接続の追跡(再生中の接続切り用)。 */
void link_le_packet(uint8_t packet_type, uint8_t *packet, uint16_t size);
bool link_is_le(hci_con_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
