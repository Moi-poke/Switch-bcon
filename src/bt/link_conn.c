/* 接続管理・再接続。Classic HID の身元保全が責務。 */

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hid.h"
#include "link.h"
#include "switch_hid.h"
#include "bt_compat.h"
#include "usb_wired.h"
#include "reconnect_policy.h"

/* 接続1回あたりの能動page予算。0 になったら受動待機へ永久移行する。
 * 前の実装は giveup が probe_outgoing_tried を戻すだけで予算が無く、
 * 5 秒周期で接続し続けて本体側の「使用するコントローラのボタンを押して
 * ください」を招いていた (docs/handoff_bt_20260914.md §8-9)。 */
static uint32_t s_page_budget;
/* 連続する拒否回数。Switch が短時間で 0x13 切断した回数。 */
static uint32_t s_rejects;
/* HID open 済みで、まだ切断が片付いていないセッションか。 */
static bool s_session_active;

bd_addr_t probe_addr;

/* Device identity is fixed: OUI + unique board ID, never bumped (SNIFF-on + stable MAC is the proven combination). */
void link_init(void)
{
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    probe_addr[0] = SWITCH_OUI_0;
    probe_addr[1] = SWITCH_OUI_1;
    probe_addr[2] = SWITCH_OUI_2;
    probe_addr[3] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 3];
    probe_addr[4] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2];
    probe_addr[5] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1];
    s_page_budget = RECONNECT_PAGE_BUDGET;
}


bd_addr_t probe_host_addr;
bool probe_host_known;
bool probe_outgoing_tried;
uint32_t probe_reconnect_tries;
uint8_t probe_reconnect_report;
bool probe_reconnect_pending;
uint32_t probe_giveup_count;
uint32_t probe_connected_at_ms;
uint8_t probe_ssp_count;

#define RECONNECT_RETRY_MS 5000u
#define RECONNECT_GIVEUP_MS 15000u
static uint32_t outgoing_at_ms;

void link_reconnect_handler(btstack_timer_source_t *ts)
{
    /* 有線モード中は Classic に出ていかない。二重認識防止。
     * タイマだけ繋ぎ直して周期を保つ (W 0 で通常輪に戻る)。 */
    if (usb_wired_is_enabled()) {
        btstack_run_loop_remove_timer(ts);
        btstack_run_loop_set_timer(ts, RECONNECT_RETRY_MS);
        btstack_run_loop_add_timer(ts);
        return;
    }
    if (probe_outgoing_tried && probe_hid_cid == 0u && outgoing_at_ms != 0u) {
        uint32_t waited =
            to_ms_since_boot(get_absolute_time()) - outgoing_at_ms;
        if (waited > RECONNECT_GIVEUP_MS) {
            probe_outgoing_tried = false;
            outgoing_at_ms = 0u;
            probe_giveup_count++;
        }
    }
    /* 再生中は繋ぎに行かない。偽装 MAC で名乗るのを防ぐ。 */
    if (probe_beacon) {
        btstack_run_loop_remove_timer(ts);
        btstack_run_loop_set_timer(ts, 1000);
        btstack_run_loop_add_timer(ts);
        return;
    }
    {
        reconnect_state_t st;
        st.wired = false; /* 有線は上で return 済み */
        st.beacon = probe_beacon;
        st.hid_up = (probe_hid_cid != 0u);
        st.host_known = probe_host_known;
        st.outgoing_tried = probe_outgoing_tried;
        st.budget_left = s_page_budget;
        st.rejects = s_rejects;
        if (reconnect_should_page(&st)) {
            uint16_t cid = 0u;
            uint8_t rst;
            probe_reconnect_tries++;
            probe_outgoing_tried = true;
            rst = hid_device_connect(probe_host_addr, &cid);
            if (rst != ERROR_CODE_SUCCESS) {
                probe_outgoing_tried = false;
                outgoing_at_ms = 0u;
            } else {
                outgoing_at_ms = to_ms_since_boot(get_absolute_time());
            }
            probe_reconnect_report = rst;
            probe_reconnect_pending = true;
            reconnect_consume_page(&s_page_budget);
            if (s_page_budget == 0u || s_rejects >= RECONNECT_REJECT_LIMIT) {
                probe_line("page budget spent. passive wait for Switch");
            }
        }
    }
    btstack_run_loop_remove_timer(ts);
    btstack_run_loop_set_timer(ts, RECONNECT_RETRY_MS);
    btstack_run_loop_add_timer(ts);
}

void link_note_disconnected(void)
{
    /* HID open 後と disc reason の二重経路から 1 セッション 2 回呼ばれる。
     * 拒否回数は 1 セッションにつき 1 回数える。 */
    if (s_session_active) {
        uint32_t held_ms;
        s_session_active = false;
        held_ms = to_ms_since_boot(get_absolute_time()) -
                  probe_connected_at_ms;
        if (reconnect_is_rejection(held_ms)) {
            s_rejects++;
            if (s_rejects >= RECONNECT_REJECT_LIMIT) {
                probe_line("switch rejected us repeatedly. "
                           "stop paging, wait for Switch");
            }
        } else {
            /* 実際に使われたセッション。拒否回数を消して予算を戻す。 */
            s_rejects = 0u;
            s_page_budget = RECONNECT_PAGE_BUDGET;
        }
        if (s_rejects >= RECONNECT_REJECT_LIMIT) {
            s_page_budget = 0u; /* 予算を枯渇扱いにして page を止める */
        }
    }
    probe_hid_cid = 0u;
    probe_outgoing_tried = false;
    outgoing_at_ms = 0u;
}

void link_mark_connected(void)
{
    probe_outgoing_tried = true;
    outgoing_at_ms = 0u;
    s_session_active = true;
    /* 予算はここでは戻さない。2026-10-01 実測で、open 成功直後に 0x13 で
     * 切られると拒否されるたびに予算が補充され、永久に止まらないことを
     * 確認した。戻すのは健全なセッションが終わったときだけ。 */
}

/* ホストからの明示要求 (T_RECONNECT) で待機状態を解除する。
 * 「実コントローラーのボタンを押す」相当。時間経過では自動では起きない。 */
void link_rearm_reconnect(void)
{
    bool was_parked = reconnect_is_parked(s_page_budget, s_rejects);
    s_rejects = 0u;
    s_page_budget = RECONNECT_PAGE_BUDGET;
    s_session_active = false;
    if (was_parked) {
        probe_line("re-arm requested. try Switch again");
    }
}

/* 有線モード保持。BT 電源の二重切替を避けるための現在値。
 * 電源操作は link_radio_update に一元化するため初期値は false
 * (起動時の link_apply_wired_mode が必要に応じて ON する)。 */
static bool link_wired;
static bool bt_powered;
/* BTstack 報告の実動作状態。電源要求 (bt_powered) とは別。
 * 起動完了前の偽装・広告投入を避けるために beacon 開始判定で使う。 */
static bool bt_working;

void link_note_bt_working(bool working)
{
    bt_working = working;
}

bool link_bt_working(void)
{
    return bt_working;
}

void link_apply_discoverable(void)
{
    /* 登録画面から見つけてもらうのは未ペアのときだけ。ペア済みは
     * connectable が立っていれば Switch から page される。
     * 常時 1 のままだと本体側の候補に追加され続け、接続確認が出る。 */
    bool quiet = link_wired && !probe_scanning && !probe_beacon;
    gap_discoverable_control(
        reconnect_should_be_discoverable(probe_host_known, quiet) ? 1u : 0u);
}


void link_radio_update(void)
{
    /* 有線中は電波を止める。無線チップが動いていると Switch 側が
     * USB を列挙しない (実測)。取込・再生中は電波が要るので戻す。 */
    bool quiet = link_wired && !probe_scanning && !probe_beacon;
    bool want_on = !quiet;
    if (want_on != bt_powered) {
        hci_power_control(want_on ? HCI_POWER_ON : HCI_POWER_OFF);
        bt_powered = want_on;
        if (want_on) {
            /* 電源再投入で transport の MAC が既定に戻るため掛け直す。
             * 掛けないと別機器扱い (88:A2:9E...) になり再接続できない。 */
            hci_set_bd_addr(probe_addr);
        }
    }
    gap_connectable_control(
        reconnect_should_be_connectable(quiet) ? 1u : 0u);
    link_apply_discoverable();
    /* 電波を止めたら USB を挿し直したのと同じ状態に戻す。
     * 未列挙のときだけ蹴る (健全なセッションは churn しない)。 */
    if (quiet && !usb_wired_is_configured()) {
        usb_wired_reconnect();
    }
}

void link_apply_wired_mode(bool wired)
{
    link_wired = wired;
    if (wired) {
        /* 接続中なら先に切る。切断完了は HID_SUBEVENT_CONNECTION_CLOSED 経由で
         * handle_hid_meta が始末する (cid=0・link_note_disconnected)。 */
        if (probe_hid_cid != 0u) {
            hid_device_disconnect(probe_hid_cid);
        }
    } else {
        /* 無線に戻すときは USB 側を外す。黙ったまま残すと二重認識になる。 */
        usb_wired_disconnect();
    }
    /* Switch からの呼び直し (着信 page) 対策と電波停止は update に集約。
     * LE 広告・スキャンの要否は update が見る。 */
    link_radio_update();
}

int link_key_count(void)
{
    int count = 0;
    bd_addr_t addr;
    link_key_t key;
    link_key_type_t type;
    btstack_link_key_iterator_t it;
    if (gap_link_key_iterator_init(&it) == 0) {
        return -1;
    }
    while (gap_link_key_iterator_get_next(&it, addr, key, &type)) {
        count++;
    }
    gap_link_key_iterator_done(&it);
    return count;
}
