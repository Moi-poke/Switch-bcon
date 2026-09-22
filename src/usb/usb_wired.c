/* 移植元: pico-wakecon src/usb_wired.c (状態機・応答経路は同一)。
 * 有線 Pro Controller の状態機と入力レポート送信 (TinyUSB glue)。
 * 応答バイトは usb_build_81_reply() (usb_hid.h) が唯一の源。
 * 80 04 完了前は入力を送らない。80 05・抜線で handshake を落とす。
 * 変更点: 入力状態は main.c の bcon_* (bcon_btn は pack済み3B)。
 * bcon_mac は応答順 (main.cで反転済み) のためここでは反転しない。 */

#include <string.h>

#include "pico/time.h"
#include "tusb.h"

#include <stdio.h>

#include "usb_wired.h"
#include "usb_hid.h"

void probe_line(const char *s);

/* 入力状態の所有元は main.c。読みのみ。 */
extern uint8_t bcon_btn[3];
extern uint16_t bcon_lx, bcon_ly, bcon_rx, bcon_ry;
extern uint8_t bcon_mac[6]; /* 応答順 (main.cで反転済み) */
extern uint32_t bcon_usb_rumble_n; /* 所有元main.c。USB振動0x10受信累計 */

#define USB_WIRED_REPORT_ID_INPUT 0x30u
#define USB_WIRED_INPUT_LEN 64u
#define USB_WIRED_INPUT_PAYLOAD_LEN 63u
#define USB_WIRED_REPORT_ID_REPLY 0x81u
/* 送信周期8ms (bInterval 8写しに合わせる。到着即更新はCore0のpull側)。 */
#define USB_WIRED_INPUT_INTERVAL_MS 8u
/* 81 01 応答の機種別 type は起動時 role 由来 (ProCon=0x03)。
 * 2wiCC の kUsbDeviceTypeProController で確認した ProCon 値が既定。 */

static bool wired_enabled;
static bool handshake_done; /* 80 04 受信で true。80 05・抜線で false に戻す。 */
static bool wired_inited;
static bool kick81_done; /* mount毎1回: Joy役の81 01先制通知済み */
static uint32_t last_input_ms;
static usb_wired_stats_t wired_stats;

void usb_wired_init(void)
{
    wired_enabled = false;
    handshake_done = false;
    kick81_done = false;
    last_input_ms = 0u;
    (void)tud_init(BOARD_TUD_RHPORT);
    /* SOF 計数はバス生存の証拠 (ホストがフレームを回しているか)。
     * 1ms 毎に tud_sof_cb が来る。 */
    tud_sof_cb_enable(true);
    wired_inited = true;
}

void usb_wired_set_enabled(bool en)
{
    wired_enabled = en;
    if (!en) {
        handshake_done = false;
    }
}

bool usb_wired_is_enabled(void)
{
    return wired_enabled;
}

bool usb_wired_is_configured(void)
{
    return wired_inited && tud_mounted();
}

bool usb_wired_handshake_done(void)
{
    return handshake_done;
}

void usb_wired_get_stats(usb_wired_stats_t *st)
{
    if (st == NULL) {
        return;
    }
    *st = wired_stats;
}

/* 有線 Input 0x30 は実機配置 (2wiCC ControllerData 互換)。
 * 12B 状態 + 36B IMU(無効のため 0) + 15B 埋め。mac/player は使わない。 */
static void build_input_report(uint8_t out[USB_WIRED_INPUT_LEN])
{
    usb_sub_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.btn[0] = bcon_btn[0];
    ctx.btn[1] = bcon_btn[1];
    ctx.btn[2] = bcon_btn[2];
    ctx.lx = bcon_lx;
    ctx.ly = bcon_ly;
    ctx.rx = bcon_rx;
    ctx.ry = bcon_ry;
    ctx.timer =
        (uint8_t)(to_ms_since_boot(get_absolute_time()) >> 5);
    /* role は起動時1回だけ決まる (usb_set_role)。入力 pack (ボタン・
     * 親指側選択) と 0x02 機器情報は usb_hid 側が ctx.role で適用する。 */
    ctx.role = usb_get_role();
    usb_build_30_report(&ctx, out);
}

void usb_wired_pump(void)
{
    tud_task();
}

/* 応答の遅延送出用 (2wiCC 方式)。コールバック内では積むだけにし、
 * 送信はタスク側で行う。コールバック内送信は control 転送の完了と
 * 競合しうるため。単一スロット (ホストは stop-and-wait のため十分)。 */
static uint8_t pend_resp[64];
static uint8_t pend_resp_id;
static bool pend_resp_valid;
static uint8_t usb_player;

/* ホストに再列挙させる。自己切断では tud_umount_cb が来ないため、
 * セッション状態 (hs・保留応答) をここで明示的に落とす。
 * 落とさないと未接続のままゲートが開いた幽霊状態になる。
 * 切断は 500ms (50ms ではホストが除去を認識しない実測)。
 * 稀な操作からのみ呼ぶ (約0.5秒止まる)。 */
void usb_wired_reconnect(void)
{
    handshake_done = false;
    pend_resp_valid = false;
    kick81_done = false;
    tud_disconnect();
    sleep_ms(500);
    tud_connect();
}

void usb_wired_disconnect(void)
{
    tud_disconnect();
}

void usb_wired_task(uint32_t now_ms)
{
    uint8_t report[USB_WIRED_INPUT_LEN];
    tud_task();
    /* Joy役の81 01先制通知 (espp式kick-start)。ドックは2006/2007に
     * 80 01を送ってこない実測のため、mount直後にこちらから
     * 81 01 00<type><mac>を出し80 02を誘う。ProConは実績経路のため
     * 対象外 (usb_kick81_due が条件を固定)。mount毎1回・送信は
     * pend経路のみ (コールバック内送信禁止)。 */
    if (usb_kick81_due(usb_get_role(), wired_enabled, tud_mounted(),
                       kick81_done, pend_resp_valid)) {
        uint8_t kick81[2] = { 0x80u, 0x01u };
        int n = usb_build_81_reply(kick81, 2, pend_resp,
                                   (int)sizeof(pend_resp), bcon_mac,
                                   usb_devtype_for_role(usb_get_role()));
        if (n > 0) {
            pend_resp_id = USB_WIRED_REPORT_ID_REPLY;
            pend_resp_valid = true;
            kick81_done = true;
        }
    }
    /* 保留中の応答を先に送る (2wiCC の special first 相当)。
     * 送れなければ次 tick に持ち越す。 */
    if (pend_resp_valid) {
        if (tud_hid_ready() &&
            tud_hid_report(pend_resp_id, &pend_resp[1],
                           (uint16_t)(sizeof(pend_resp) - 1u))) {
            pend_resp_valid = false;
            {
                char ul[24];
                snprintf(ul, sizeof(ul), "UTX id=%02x", pend_resp_id);
                probe_line(ul);
            }
            if (pend_resp_id == USB_WIRED_REPORT_ID_REPLY) {
                wired_stats.tx81++;
            } else {
                wired_stats.tx21++;
            }
        }
        return;
    }
    /* 80 04 完了前は送らない。 */
    if (!wired_enabled || !wired_inited || !tud_mounted() || !handshake_done) {
        return;
    }
    if ((uint32_t)(now_ms - last_input_ms) < USB_WIRED_INPUT_INTERVAL_MS) {
        return;
    }
    if (!tud_hid_ready()) {
        return;
    }
    build_input_report(report);
    if (tud_hid_report(USB_WIRED_REPORT_ID_INPUT, &report[1],
                       USB_WIRED_INPUT_PAYLOAD_LEN)) {
        last_input_ms = now_ms;
        wired_stats.in30++;
    }
}

/* OUT EP (80 xx ハンドシェイク) の受口。usb_descriptors.c から分離。
 * 応答バイトは usb_build_81_reply() のみが作る。0 返却は「送らない」。 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    uint8_t req[65];
    uint16_t req_len;
    uint8_t sub;
    int n;
    (void)instance;
    (void)report_type;
    if (buffer == NULL || bufsize == 0u) {
        return;
    }
    /* 受信経路の内訳 (80 xx 以外も数える。0x01 系の有無も見える)。 */
    if (report_id != 0u) {
        wired_stats.ctl_rx++;
    } else {
        wired_stats.ep_rx++;
    }
    /* OUT EP 経路は先頭 1B が生の report ID (0x80)。
     * control 経路は report_id 引数に分離される。両方受ける。 */
    if (report_id != 0u) {
        if (bufsize > (uint16_t)sizeof(req) - 1u) {
            return;
        }
        req[0] = report_id;
        memcpy(&req[1], buffer, bufsize);
        req_len = (uint16_t)(bufsize + 1u);
    } else {
        if (bufsize > (uint16_t)sizeof(req)) {
            return;
        }
        memcpy(req, buffer, bufsize);
        req_len = bufsize;
    }
    if (req_len < 2u) {
        wired_stats.short_n++;
        return;
    }
    /* bcon_mac は応答順のため反転不要。 */
    if (req[0] == 0x80u) {
        sub = req[1];
        wired_stats.rx80++;
        wired_stats.last80 = sub;
        wired_stats.hist[0] = wired_stats.hist[1];
        wired_stats.hist[1] = wired_stats.hist[2];
        wired_stats.hist[2] = wired_stats.hist[3];
        wired_stats.hist[3] = sub;
        if (wired_enabled && sub == 0x04u) {
            handshake_done = true;
        } else if (wired_enabled && sub == 0x05u) {
            handshake_done = false;
        }
        {
            char ul[32];
            snprintf(ul, sizeof(ul), "U80 s=%02x hs=%d", sub,
                     handshake_done ? 1 : 0);
            probe_line(ul);
        }
        /* 応答は積むだけにする。送信は usb_wired_task 側で行う。 */
        n = usb_build_81_reply(req, (int)req_len, pend_resp,
                               (int)sizeof(pend_resp), bcon_mac,
                               usb_devtype_for_role(usb_get_role()));
        if (n <= 0) {
            /* 送らない: 不正入力 or 80 04 実機沈黙 (Change B。hs=true は
             * 上で立て済み)。応答は常に 64B (ID+63)。 */
            return;
        }
        pend_resp_id = USB_WIRED_REPORT_ID_REPLY;
        pend_resp_valid = true;
    } else if (req[0] == 0x01u) {
        /* 0x01 サブコマンド。
         * 注意: report ID 0x10 (振動のみ・無応答) と sub 0x10 (SPI 読出・
         * 要応答) は別物。混同して落とすと Switch が再送を繰り返す。 */
        usb_sub_ctx_t ctx;
        if (req_len < 11) {
            return;
        }
        uint8_t k;
        bool seen = false;
        sub = req[10];
        if (req_len >= 12) {
            wired_stats.subd = req[11];
        }
        wired_stats.hist01[0] = wired_stats.hist01[1];
        wired_stats.hist01[1] = wired_stats.hist01[2];
        wired_stats.hist01[2] = wired_stats.hist01[3];
        wired_stats.hist01[3] = sub;
        /* 初出順を保持する (上書きなし)。 */
        for (k = 0u; k < wired_stats.first8_n && k < 8u; k++) {
            if (wired_stats.first8[k] == sub) {
                seen = true;
                break;
            }
        }
        if (!seen && wired_stats.first8_n < 8u) {
            wired_stats.first8[wired_stats.first8_n++] = sub;
        }
        if (sub == 0x10u) {
            /* 読出番地を残す (応答内容の当否判定用)。
             * 応答自体は下の共用経路で返す (早期 return しない)。 */
            if (req_len >= 16) {
                wired_stats.spi_a =
                    (uint16_t)((uint16_t)req[11] |
                               ((uint16_t)req[12] << 8));
                wired_stats.spi_n = req[15];
                wired_stats.spi_c++;
            }
        }
        /* 0x03 mode 0x30 も full 開始合図にする。
         * 80 04 が来ないホストへの備え。 */
        if (wired_enabled && sub == 0x03u && req_len >= 12 &&
            req[11] == 0x30u) {
            handshake_done = true;
        }
        {
            char ul[40];
            uint8_t sd = (req_len >= 12u) ? req[11] : 0u;
            snprintf(ul, sizeof(ul), "U01 s=%02x d=%02x hs=%d", sub, sd,
                     handshake_done ? 1 : 0);
            probe_line(ul);
        }
        if (sub == 0x30u && req_len >= 12) {
            usb_player = req[11];
        }
        ctx.btn[0] = bcon_btn[0];
        ctx.btn[1] = bcon_btn[1];
        ctx.btn[2] = bcon_btn[2];
        ctx.lx = bcon_lx;
        ctx.ly = bcon_ly;
        ctx.rx = bcon_rx;
        ctx.ry = bcon_ry;
        ctx.timer =
            (uint8_t)(to_ms_since_boot(get_absolute_time()) >> 5);
        memcpy(ctx.mac, bcon_mac, 6);
        ctx.player = usb_player;
        ctx.role = usb_get_role();
        n = usb_build_21_reply(req, (int)req_len, pend_resp,
                               (int)sizeof(pend_resp), &ctx);
        if (n <= 0) {
            return;
        }
        pend_resp_id = 0x21u;
        pend_resp_valid = true;
    } else if (req[0] == 0x10u) {
        /* 振動のみ (Step 2: ACK＋破棄し計数のみ。v1はPCへ転送しない)。
         * TinyUSBがACKするためここでは何も返さない。STATUS bit7の源。 */
        bcon_usb_rumble_n++;
    } else {
        /* 0x80/0x01/0x10 以外は応答なし。未知 ID は計数だけ残す。 */
        wired_stats.unk_id = req[0];
        wired_stats.unk_len = (uint8_t)(req_len > 255 ? 255 : req_len);
        wired_stats.unk_n++;
    }
}

/* 装着で計数。tud_mounted() が立つ直前の bus reset/configure 由来。 */
void tud_mount_cb(void)
{
    wired_stats.mount++;
    probe_line("MMOUNT");
}

/* 抜線で handshake を落とす。次セッションは 80 04 からやり直し。
 * 送信ゲートが閉じるため古い入力は出ない (unmount中立化)。
 * 共有u32の陳腐化は timeout-neutral が扱う。 */
void tud_umount_cb(void)
{
    wired_stats.unmount++;
    handshake_done = false;
    kick81_done = false;
    probe_line("UUMOUNT");
}

/* SOF 到達 = ホストがバスにフレームを流している (列挙前でも進む)。
 * サスペンド中は止まる。いずれも TinyUSB の weak 既定の上書き。 */
void tud_sof_cb(uint32_t frame_count)
{
    (void)frame_count;
    wired_stats.sof++;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    wired_stats.susp++;
}

void tud_resume_cb(void)
{
    wired_stats.resm++;
}
