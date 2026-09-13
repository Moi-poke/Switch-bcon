/* 移植元: pico-wakecon src/usb_hid.c (応答バイト同一。帰属は usb_hid.h 参照)。
 * 変更点: util_pack_stick_12bit → pack_stick_12bit (src/proto/pack.c)。
 * 日本語コメントは残す。でたらめ値は返さない。 */
#include <string.h>
#include "usb_hid.h"
#include "spi.h"
#include "pack.h"

bool usb_req_is_handshake(const uint8_t *req, int req_len)
{
    return req != NULL && req_len >= 2 && req[0] == 0x80u;
}

/* 80 xx への応答は常に 64B ゼロパディングで返す。
 * 2wiCC (実働) が全応答を 64B (ID+63) で送る作りのため。
 * 短縮応答では Switch 2 が先に進まない実測 (last=02 で停止)。
 * 91/92・未知サブコマンドも 81 <sub> + 0 埋めで返す。
 * 無応答にするとホストが止まる。 */
int usb_build_81_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const uint8_t mac[6], uint8_t dev_type)
{
    uint8_t sub;
    if (req == NULL || out == NULL || mac == NULL) {
        return 0;
    }
    if (!usb_req_is_handshake(req, req_len)) {
        return 0;
    }
    if (out_max < 64) {
        return 0;
    }
    sub = req[1];
    memset(out, 0, 64);
    switch (sub) {
        case 0x01u:
            /* 81 01 00 <type> <mac6> + 0 埋め。
             * Pro=0x03 は 2wiCC の kUsbDeviceTypeProController で確認。 */
            out[0] = 0x81u; out[1] = 0x01u; out[2] = 0x00u;
            out[3] = dev_type;
            memcpy(&out[4], mac, 6);
            return 64;
        case 0x02u:
        case 0x03u:
        case 0x04u:
        case 0x05u:
        case 0x06u:
            /* 04/05/06 の応答有無は 2wiCC の実働で確認 (R-T1-1 解消)。 */
            out[0] = 0x81u; out[1] = sub;
            return 64;
        default:
            out[0] = 0x81u; out[1] = sub;
            return 64;
    }
}

/* 0x21 応答の共通 12B (2wiCC ControllerData 互換)。
 * timer・電池・接続・姿勢は ctx から。電池は充電中+満充電固定。 */
void usb_pack_controller_data(uint8_t out12[12], const usb_sub_ctx_t *ctx)
{
    out12[0] = ctx->timer;
    out12[1] = 0x91u;
    out12[2] = ctx->btn[0];
    out12[3] = (uint8_t)(ctx->btn[1] | 0x80u);
    out12[4] = (uint8_t)(ctx->btn[2] & 0xCFu);
    pack_stick_12bit(ctx->lx, ctx->ly, &out12[5]);
    pack_stick_12bit(ctx->rx, ctx->ry, &out12[8]);
    out12[11] = 0x09u;
}

int usb_build_30_report(const usb_sub_ctx_t *ctx, uint8_t out64[64])
{
    if (ctx == NULL || out64 == NULL) {
        return 0;
    }
    memset(out64, 0, 64);
    out64[0] = 0x30u;
    usb_pack_controller_data(&out64[1], ctx);
    return 64;
}

/* 0x01-0x01 (BT ペアリング) の応答雛形。2wiCC (MIT) の実働値。
 * 出典: knflrpn/2wiCC src/procon_data.c (bt_data_01/02/03)。 */
static const uint8_t usb_bt_data_01[49] = {
    0x01,
    0xc1, 0xc9, 0x3e, 0xe9, 0xb6, 0x98, 0x00, 0x25,
    0x08, 0x50, 0x72, 0x6f, 0x20, 0x43, 0x6f, 0x6e,
    0x74, 0x72, 0x6f, 0x6c, 0x6c, 0x66, 0x72, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t usb_bt_data_02[49] = {
    0x02,
    0xe5, 0xc8, 0xe4, 0x92, 0x05, 0xff, 0xc9, 0x8a,
    0x7d, 0xea, 0x15, 0xf6, 0x19, 0xba, 0x82, 0x13,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t usb_bt_data_03[49] = {
    0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void usb_mac_ascii(char *dst12, const uint8_t mac[6])
{
    static const char *hexd = "0123456789ABCDEF";
    int k;
    for (k = 0; k < 6; k++) {
        dst12[k * 2 + 0] = hexd[(mac[k] >> 4) & 0x0Fu];
        dst12[k * 2 + 1] = hexd[mac[k] & 0x0Fu];
    }
}

/* 0x01 xx → 64B の 0x21 応答。01/02/03/10/30/40/48 と既定 ack。
 * サブコマンド部の並びは BT 応答と同値 (輸送非依存のため)。
 * 本文は 2wiCC の実働値 (機器情報の fw 03 48 等) に合わせる。 */
int usb_build_21_reply(const uint8_t *req, int req_len, uint8_t *out,
                       int out_max, const usb_sub_ctx_t *ctx)
{
    uint8_t sub;
    if (req == NULL || out == NULL || ctx == NULL) {
        return 0;
    }
    if (req_len < 11 || req[0] != 0x01u) {
        return 0;
    }
    if (out_max < 64) {
        return 0;
    }
    sub = req[10];
    memset(out, 0, 64);
    out[0] = 0x21u;
    usb_pack_controller_data(&out[1], ctx);
    switch (sub) {
        case 0x01u: {
            /* BT ペアリング。種別で雛形を選び、自 MAC を ASCII で埋める
             * (2wiCC 通り。type 1 のみ MAC 埋込み)。 */
            uint8_t ptype = (req_len >= 12) ? req[11] : 3u;
            const uint8_t *tpl = usb_bt_data_03;
            if (ptype == 1u) {
                tpl = usb_bt_data_01;
            } else if (ptype == 2u) {
                tpl = usb_bt_data_02;
            }
            out[13] = 0x81u;
            out[14] = 0x01u;
            memcpy(&out[15], tpl, 31);
            if (ptype == 1u) {
                usb_mac_ascii((char *)&out[16], ctx->mac);
            }
            return 64;
        }
        case 0x02u:
            out[13] = 0x82u;
            out[14] = 0x02u;
            out[15] = 0x03u;
            out[16] = 0x48u;
            out[17] = 0x03u;
            out[18] = 0x02u;
            memcpy(&out[19], ctx->mac, 6);
            out[25] = 0x01u;
            /* 末尾は BT 応答と同値の 0x02 にする。0x01 では Switch が
             * SPI 色を使わない (有線の色が反映されない実測)。
             * 無線 (hid.c reply_device_info) の実働値に合わせる。 */
            out[26] = 0x02u;
            return 64;
        case 0x03u:
            out[13] = 0x80u;
            out[14] = 0x03u;
            return 64;
        case 0x10u: {
            uint16_t addr;
            uint8_t want;
            const spi_entry_t *hit;
            if (req_len < 16) {
                return 0;
            }
            addr = (uint16_t)req[11] | ((uint16_t)req[12] << 8);
            want = req[15];
            if (want == 0u || want > 44u) {
                return 0;
            }
            out[13] = 0x90u;
            out[14] = 0x10u;
            out[15] = (uint8_t)(addr & 0xFFu);
            out[16] = (uint8_t)(addr >> 8);
            out[17] = 0x00u;
            out[18] = 0x00u;
            out[19] = want;
            hit = spi_find(addr);
            if (addr == 0x6000u) {
                /* シリアル域は空 (0xFF) で答える。実機シリアル風の値を
                 * 返すと Switch 2 が 2162-0002 で落ちる実測のため。
                 * 2wiCC も serial none (0xFF) で運用している。
                 * BT 側の表は変えない (Switch 1 無線は現状で動作中のため)。 */
                memset(&out[20], 0xFF, want);
            } else if (hit != NULL && want <= hit->size) {
                memcpy(&out[20], hit->data, want);
            } else if (addr >= 0x6000u && addr < 0x6100u &&
                       (uint32_t)addr + want <= 0x6100u) {
                /* 表にない 0x60xx は 0xFF 埋めで答える (2wiCC 通り)。
                 * 無応答にするとホストが止まる。範囲外は答えない。 */
                memset(&out[20], 0xFF, want);
            } else {
                return 0;
            }
            return 64;
        }
        case 0x30u:
        case 0x40u:
        case 0x48u:
            out[13] = 0x80u;
            out[14] = sub;
            return 64;
        default:
            out[13] = 0x80u;
            out[14] = sub;
            return 64;
    }
}
