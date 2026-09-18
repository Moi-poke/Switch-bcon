/* 入力状態・HID 応答・送信。Switch 1 Pro Controller 仕様。
 * 出典: dekuNukem/Nintendo_Switch_Reverse_Engineering /
 *   GP2040-CE SwitchProDriver / nxbt Example Pairing Session。
 * バイト値の意味は資料通り。でたらめ値は送らない。 */

#include <stdio.h>
#include <string.h>

#include "hid.h"
#include "spi.h"
#include "link.h"
#include "store.h"
#include "bt_compat.h"
#include "pack.h"
#include "rumble.h"

#define HID_REPLY_WANT (2u + 48u)

/* ---- 入力状態 ---- */
uint8_t probe_btn[3];
uint16_t probe_lx = 0x800, probe_ly = 0x800;
uint16_t probe_rx = 0x800, probe_ry = 0x800;
uint32_t probe_btn_press_count;
bool probe_btn_was_down;

void probe_input_reset(void)
{
    probe_btn[0] = 0u;
    probe_btn[1] = 0u;
    probe_btn[2] = 0u;
    probe_lx = 0x800u;
    probe_ly = 0x800u;
    probe_rx = 0x800u;
    probe_ry = 0x800u;
    probe_btn_was_down = false;
}

/* ---- 送信状態 ---- */
uint16_t probe_hid_cid;
bool probe_full_mode;
uint8_t probe_report_timer;
uint32_t probe_empty_sent;
uint32_t probe_reply_sent;
uint32_t probe_state_sent;
uint32_t probe_out_report_count;
uint8_t probe_subcmd_seen[24];
uint8_t probe_subcmd_seen_n;
uint16_t probe_out_len_min = 0xFFFFu;
uint16_t probe_out_len_max;
uint8_t probe_player_id;
bool probe_player_seen = false;
uint8_t probe_rumble_l, probe_rumble_r;
bool probe_rumble_seen = false;
bool probe_imu_enabled;
bool probe_vibration_enabled;
uint8_t probe_input_mode = 0x3Fu;
uint8_t probe_hci_state_arg = 0xFFu;
uint32_t probe_hci_state_count;
bool probe_send_now_wanted;

#define REPLY_MAX 64u
static uint8_t reply_buf[REPLY_MAX];
static uint16_t reply_len;
uint32_t probe_color_set_count;

/* 200ms 無通信で中立化する。PC 断でも押しっぱなしを残さない最後の砦。
 * 既に中立なら何もしない。解除時は WD を1行返し PC が時刻を測れる。 */
#define WATCHDOG_MS 200u
static uint32_t watch_last_ms;
static bool watch_fed;

void probe_watchdog_feed(uint32_t now_ms)
{
    watch_last_ms = now_ms;
    watch_fed = true;
}

void probe_watchdog_poll(uint32_t now_ms)
{
    if (!watch_fed) {
        return;
    }
    if ((int32_t)(now_ms - watch_last_ms) < (int32_t)WATCHDOG_MS) {
        return;
    }
    if (probe_btn[0] == 0u && probe_btn[1] == 0u && probe_btn[2] == 0u &&
        probe_lx == 0x800u && probe_ly == 0x800u &&
        probe_rx == 0x800u && probe_ry == 0x800u) {
        return;
    }
    probe_input_reset();
    probe_request_send();
    probe_line("WD");
}

void probe_hid_reset(void)
{
    probe_out_report_count = 0u;
    probe_subcmd_seen_n = 0u;
    probe_out_len_min = 0xFFFFu;
    probe_out_len_max = 0u;
    probe_report_timer = 0u;
    probe_empty_sent = 0u;
    probe_reply_sent = 0u;
    reply_len = 0u;
    probe_full_mode = false;
    probe_state_sent = 0u;
    probe_player_id = 0u;
    probe_player_seen = false;
    probe_rumble_l = 0u;
    probe_rumble_r = 0u;
    probe_rumble_seen = false;
    probe_imu_enabled = false;
    probe_vibration_enabled = false;
    probe_input_mode = 0x3Fu;
    probe_hci_state_arg = 0xFFu;
    probe_btn_press_count = 0u;
    probe_input_reset();
}

void probe_request_send(void)
{
    if (probe_hid_cid != 0u && probe_full_mode) {
        hid_device_request_can_send_now_event(probe_hid_cid);
        probe_send_now_wanted = true;
    }
}

uint32_t probe_send_interval_ms(void)
{
    return probe_full_mode ? 7u : 100u;
}

/* 応答の共通部 16B を作り、中身の書込位置を返す。
 * [3] 以降は 0x30 と同じ「今の姿勢」を載せる（固定値にしない）。 */
uint16_t probe_build_reply(uint8_t ack, uint8_t subcmd)
{
    reply_buf[0] = 0xA1u;
    reply_buf[1] = 0x21u;
    reply_buf[2] = probe_report_timer++;
    reply_buf[3] = 0x80u;
    reply_buf[4] = probe_btn[0];
    reply_buf[5] = probe_btn[1];
    reply_buf[6] = probe_btn[2];
    pack_stick_12bit(probe_lx, probe_ly, &reply_buf[7]);
    pack_stick_12bit(probe_rx, probe_ry, &reply_buf[10]);
    reply_buf[13] = 0x08u;
    reply_buf[14] = ack;
    reply_buf[15] = subcmd;
    return 16u;
}

/* 送信 3 種の優先度: 応答 > 0x30(14B, IMU 無し) > 空(3B)。 */
void probe_can_send_now(void)
{
    if (reply_len > 0u) {
        hid_device_send_interrupt_message(probe_hid_cid, reply_buf,
                                          reply_len);
        probe_reply_sent++;
        reply_len = 0u;
    } else if (probe_full_mode) {
        uint8_t f[14];
        f[0] = 0xA1u;
        f[1] = 0x30u;
        f[2] = probe_report_timer++;
        f[3] = 0x80u;
        f[4] = probe_btn[0];
        f[5] = probe_btn[1];
        f[6] = probe_btn[2];
        pack_stick_12bit(probe_lx, probe_ly, &f[7]);
        pack_stick_12bit(probe_rx, probe_ry, &f[10]);
        f[13] = 0x08u;
        hid_device_send_interrupt_message(probe_hid_cid, f, sizeof(f));
        probe_state_sent++;
    } else {
        uint8_t f[3];
        /* ペア前は 100ms の空レポートで生かす（資料の手順通り）。 */
        f[0] = 0xA1u;
        f[1] = 0x00u;
        f[2] = probe_report_timer++;
        hid_device_send_interrupt_message(probe_hid_cid, f, sizeof(f));
        probe_empty_sent++;
    }
    probe_send_now_wanted = false;
}

/* ---- 出力レポート受信 ----
 * BTstack はレポート ID を外して渡す。sub=report[9]、addr=report[10..11]。
 * 宣言 48B より短い 0x01 を受けるため truncated 受理が前提。 */

static void note_subcmd(uint8_t sub)
{
    uint8_t k;
    for (k = 0u; k < probe_subcmd_seen_n; k++) {
        if (probe_subcmd_seen[k] == sub) {
            return;
        }
    }
    if (probe_subcmd_seen_n < (uint8_t)sizeof(probe_subcmd_seen)) {
        probe_subcmd_seen[probe_subcmd_seen_n++] = sub;
    }
}

/* ack 上位ニブルは中身の予告: 80 無し / 82 機器 / 83 トリガ / 90 SPI /
 * 81 ペア / B0 灯 / C0 IMU / D0 電圧。実物(GP2040)通り。 */

/* 機器情報: fw 03 8B(実測値) / 種別 03(Pro) / 自アドレス。 */
static void reply_device_info(uint16_t *p)
{
    int k;
    *p = probe_build_reply(0x82u, 0x02u);
    reply_buf[(*p)++] = 0x03u;
    reply_buf[(*p)++] = 0x8Bu;
    reply_buf[(*p)++] = 0x03u;
    reply_buf[(*p)++] = 0x02u;
    for (k = 0; k < 6; k++) {
        reply_buf[(*p)++] = probe_addr[k];
    }
    reply_buf[(*p)++] = 0x01u;
    reply_buf[(*p)++] = 0x02u;  /* 0x601B と同義: グリップ色まで使う */
}

/* 電源指示: 00 寝ろ / 01 再接続 / 02 ペア / 04 再接続(HOME)。 */
static void reply_power(const uint8_t *report, int report_size)
{
    char msg[96];
    const char *what;
    if (report_size > 10) {
        probe_hci_state_arg = report[10];
    }
    probe_hci_state_count++;
    if (probe_hci_state_arg == 0x00u) {
        what = "sleep";
    } else if (probe_hci_state_arg == 0x01u) {
        what = "reconnect";
    } else if (probe_hci_state_arg == 0x02u) {
        what = "pair";
    } else if (probe_hci_state_arg == 0x04u) {
        what = "reconnect(HOME)";
    } else {
        what = "unknown";
    }
    snprintf(msg, sizeof(msg), "  0x06 power=0x%02x (%s)",
             (unsigned)probe_hci_state_arg, what);
    probe_line(msg);
    reply_len = probe_build_reply(0x80u, 0x06u);
}

static void reply_spi(const uint8_t *report, int report_size)
{
    char msg[96];
    uint16_t addr;
    uint8_t want;
    const spi_entry_t *hit;
    uint16_t p;
    if (report_size < 15) {
        reply_len = 0u;
        return;
    }
    addr = (uint16_t)report[10] | ((uint16_t)report[11] << 8);
    want = report[14];
    hit = spi_find(addr);
    if (hit == NULL || want > hit->size) {
        /* 未知・不足は答えない。でたらめ校正値は渡さない。 */
        snprintf(msg, sizeof(msg),
                 "  SPI unknown addr=0x%04x size=%u (no reply)",
                 (unsigned)addr, (unsigned)want);
        probe_line(msg);
        reply_len = 0u;
        return;
    }
    p = probe_build_reply(0x90u, 0x10u);
    reply_buf[p++] = (uint8_t)(addr & 0xFFu);
    reply_buf[p++] = (uint8_t)(addr >> 8);
    reply_buf[p++] = 0x00u;
    reply_buf[p++] = 0x00u;
    reply_buf[p++] = want;
    memcpy(&reply_buf[p], hit->data, want);
    p = (uint16_t)(p + want);
    reply_len = p;
}

static void answer_subcmd(uint8_t sub, const uint8_t *report, int report_size)
{
    uint16_t p;
    switch (sub) {
        case 0x00:
            p = probe_build_reply(0x80u, 0x00u);
            reply_buf[p++] = 0x03u;
            reply_len = p;
            break;
        case 0x01:
            p = probe_build_reply(0x81u, 0x01u);
            reply_buf[p++] = 0x03u;
            reply_len = p;
            break;
        case 0x02:
            reply_device_info(&p);
            reply_len = p;
            break;
        case 0x03:
            probe_full_mode = true;
            if (report_size > 10) {
                probe_input_mode = report[10];
            }
            probe_line("  0x30 mode start");
            p = probe_build_reply(0x80u, 0x03u);
            reply_buf[p++] = probe_input_mode;
            reply_len = p;
            break;
        case 0x04:
            /* トリガ経過: uint16×7=14B(未押下=0)。 */
            p = probe_build_reply(0x83u, 0x04u);
            memset(&reply_buf[p], 0, 14);
            reply_len = p + 14u;
            break;
        case 0x05:
            /* ホスト記憶の有無。覚えていれば 0x01。嘘はつかない。 */
            p = probe_build_reply(0x80u, 0x05u);
            reply_buf[p++] = probe_host_known ? 0x01u : 0x00u;
            reply_len = p;
            break;
        case 0x06:
            reply_power(report, report_size);
            break;
        case 0x10:
            reply_spi(report, report_size);
            break;
        case 0x21:
            p = probe_build_reply(0x80u, 0x21u);
            memset(&reply_buf[p], 0, 34);
            reply_len = p + 34u;
            break;
        case 0x30:
            if (report_size > 10) {
                probe_player_id = report[10];
                probe_player_seen = true;
            }
            reply_len = probe_build_reply(0x80u, 0x30u);
            break;
        case 0x31:
            p = probe_build_reply(0xB0u, 0x31u);
            reply_buf[p++] = probe_player_id;
            reply_len = p;
            break;
        case 0x33:
            p = probe_build_reply(0x80u, 0x33u);
            reply_buf[p++] = 0x03u;
            reply_len = p;
            break;
        case 0x40:
            if (report_size > 10) {
                probe_imu_enabled = (report[10] != 0u);
            }
            p = probe_build_reply(0x80u, 0x40u);
            reply_buf[p++] = 0x00u;
            reply_len = p;
            break;
        case 0x43:
            p = probe_build_reply(0xC0u, 0x43u);
            reply_buf[p++] = (report_size > 10) ? report[10] : 0u;
            reply_buf[p++] = (report_size > 11) ? report[11] : 0u;
            reply_len = p;
            break;
        case 0x48:
            if (report_size > 10) {
                probe_vibration_enabled = (report[10] != 0u);
            }
            p = probe_build_reply(0x80u, 0x48u);
            reply_buf[p++] = 0x00u;
            reply_len = p;
            break;
        case 0x50:
            /* 電圧。電池無しのため満充電固定値。 */
            p = probe_build_reply(0xD0u, 0x50u);
            reply_buf[p++] = 0x83u;
            reply_buf[p++] = 0x06u;
            reply_len = p;
            break;
        default:
            reply_len = probe_build_reply(0x80u, sub);
            break;
    }
}

void probe_report_handler(uint16_t cid, hid_report_type_t report_type,
                          uint16_t report_id, int report_size,
                          uint8_t *report)
{
    char msg[96];
    (void)cid;
    (void)report_type;
    probe_out_report_count++;
    if (report_size > 0) {
        uint16_t len = (uint16_t)report_size;
        if (len < probe_out_len_min) {
            probe_out_len_min = len;
        }
        if (len > probe_out_len_max) {
            probe_out_len_max = len;
        }
    }
    if (report_id == 0x01u && report_size > 9) {
        uint8_t sub = report[9];
        note_subcmd(sub);
        if (sub == 0x10u && report_size > 11) {
            snprintf(msg, sizeof(msg),
                     "  SUB=0x%02x raw=%02x %02x %02x %02x %02x len=%d",
                     sub, report[10], report[11], report[12], report[13],
                     report[14], report_size);
        } else {
            snprintf(msg, sizeof(msg), "  SUB=0x%02x len=%d kinds=%u",
                     sub, report_size, probe_subcmd_seen_n);
        }
        probe_line(msg);
        answer_subcmd(sub, report, report_size);
        /* A1+ID+本体48=50B へ 0 埋めで揃える。 */
        if (reply_len > 0u) {
            if (reply_len < HID_REPLY_WANT) {
                memset(&reply_buf[reply_len], 0, HID_REPLY_WANT - reply_len);
                reply_len = HID_REPLY_WANT;
            }
            if (reply_len > REPLY_MAX) {
                reply_len = REPLY_MAX;
            }
        }
        if (reply_len > 0u && probe_hid_cid != 0u) {
            hid_device_request_can_send_now_event(probe_hid_cid);
        }
    } else if (report_id == 0x10u) {
        /* 振動出力: L2CAP層がACKするためここでは返さない。最新ampを復号し
         * 保持する (report[0]=counterのため &report[1] + size>=9)。
         * 毎回logすると60Hz spamになるため初回のみ出す。 */
        uint8_t rl = 0u, rr = 0u;
        if (rumble_decode_010(report, report_size, &rl, &rr)) {
            probe_rumble_l = rl;
            probe_rumble_r = rr;
            probe_rumble_seen = true;
        }
        if (bcon_bt_rumble_n == 0u) {
            probe_line("  RUMBLE 0x10 intake (ack+decode, counted)");
        }
        bcon_bt_rumble_n++;
    } else {
        snprintf(msg, sizeof(msg), "  OUT id=0x%02x len=%d",
                 (unsigned)report_id, report_size);
        probe_line(msg);
    }
}
