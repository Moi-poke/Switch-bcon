/* 移植元: pico-wakecon src/usb_hid.c (応答バイト同一。帰属は usb_hid.h 参照)。
 * 変更点: util_pack_stick_12bit → pack_stick_12bit (src/proto/pack.c)。
 * 日本語コメントは残す。でたらめ値は返さない。 */
/* [PABot-ref]
 * 一部のUSB応答はPABotBase2で観測された応答を参考に同等機能を実装している。
 * 互換製品であることや完全一致を主張するものではない。
 * 文中の [PABot-ref] はこの注記への参照。 */
#include <string.h>
#include "usb_hid.h"
#include "spi.h"
#include "pack.h"

/* 起動時 role (EMUL_ROLE_* in proto/protocol.h, pack.h 経由で可視)。
 * 既定 0=ProCon。usb_set_role() で起動時に1回だけ上書きする。 */
static uint8_t s_usb_role = 0u;

/* Plan A (separate PID wave): USB は 057E:2009 ProCon のまま列挙し、
 * Joy らしさは応答内容 (dev_type/btn/stick/battery/SPI/kick81) で表す。
 * PABotBase2-Pico2W 実測で USB 記述子は 2009 単一のため。
 * PERSONALITY_TABLE (src/bt/personality.c) の BT 側 usb_pid/usb_product
 * (2006/2007・Joy-Con 文字列) は無線用プレースホルダとして残し、
 * USB 列挙には使わない。OOB (>2) は従来通り ProCon 値。
 * 関数を呼ばず値だけ写す理由は usb_hid.h の usb_set_role 註記を参照。 */
static const uint16_t USB_PID_BY_ROLE[3] = { 0x2009u, 0x2009u, 0x2009u };
static const char *const USB_PRODUCT_BY_ROLE[3] = {
    "Pro Controller", "Pro Controller", "Pro Controller",
};
static const uint8_t USB_DEVTYPE_BY_ROLE[3] = { 0x03u, 0x01u, 0x02u };

/* Blocker 4, role latch proof by construction: EMULATE 変更は再起動適用
 * (dispatch が FX_EMULATE_MODE 意図を返すだけ — test_config.c [22][23] で
 * pin。main.c exec_fx が Flash 永続化 + s_reboot_at 武装 (+500ms)。
 * 本関数は T4 配線で USB init/列挙より前に1回だけ呼ばれる)。
 * よって session 中の persona 切替は起きず、s_usb_role は安定ラッチ。
 * 実行時ガードは不要。範囲外 (>2) は ProCon (0) に倒す。 */
void usb_set_role(uint8_t role)
{
    s_usb_role = (role <= 2u) ? role : 0u;
}

uint8_t usb_get_role(void)
{
    return s_usb_role;
}

uint16_t usb_pid_for_role(uint8_t role)
{
    if (role > 2u) {
        role = 0u;
    }
    return USB_PID_BY_ROLE[role];
}

const char *usb_product_for_role(uint8_t role)
{
    if (role > 2u) {
        role = 0u;
    }
    return USB_PRODUCT_BY_ROLE[role];
}

uint8_t usb_devtype_for_role(uint8_t role)
{
    if (role > 2u) {
        role = 0u;
    }
    return USB_DEVTYPE_BY_ROLE[role];
}

/* ProCon pack済み3B -> role 輸送3B (joy_pack_btn3 と同値の写し)。
 * 同値の根拠: Joy 側が残す bit は ProCon pack 後も別位置で残る
 * (R/ZR/L/ZL・十字・Minus/Plus/Home/Capture/押込は固有bit。
 * GR/GL/C/予約は両方で落とす)。直接 joy_pack_btn3 を呼ばない理由は
 * usb_hid.h の usb_set_role 註記を参照 (link できない)。
 * 非static: host test (T9 cross-check) から直接呼ぶため export。 */
void usb_role_pack_btn3(const uint8_t procon3[3], uint8_t role,
                        uint8_t out3[3])
{
    if (role == 1u) {
        out3[0] = 0u;
        out3[1] = (uint8_t)(procon3[1] & 0x29u);
        out3[2] = (uint8_t)((procon3[2] & 0xCFu) |
                            ((procon3[0] & 0x80u) >> 3) |
                            ((procon3[0] & 0x40u) >> 1));
        return;
    }
    if (role == 2u) {
        out3[0] = (uint8_t)((procon3[0] & 0xCFu) |
                            ((procon3[2] & 0x80u) >> 3) |
                            ((procon3[2] & 0x40u) >> 1));
        out3[1] = (uint8_t)(procon3[1] & 0x16u);
        out3[2] = 0u;
        return;
    }
    out3[0] = procon3[0];
    out3[1] = procon3[1];
    out3[2] = procon3[2];
}

/* Joy役のみ true。ProCon (0) は実績経路のため対象外。範囲外 role は
 * ProCon 扱いのため false。 */
bool usb_kick81_due(uint8_t role, bool wired_en, bool mounted,
                    bool kick_done, bool pend_valid)
{
    if (role == 0u || role > 2u) {
        return false;
    }
    return (bool)(!kick_done && wired_en && mounted && !pend_valid);
}

bool usb_req_is_handshake(const uint8_t *req, int req_len)
{
    return req != NULL && req_len >= 2 && req[0] == 0x80u;
}

/* 80 xx への応答は (80 04 を除き) 常に 64B ゼロパディングで返す。
 * 2wiCC (実働) が全応答を 64B (ID+63) で送る作りのため。
 * 短縮応答では Switch 2 が先に進まない実測 (last=02 で停止)。
 * 91/92・未知サブコマンドも 81 <sub> + 0 埋めで返す。
 * 無応答にするとホストが止まる (唯一の例外が 80 04 の実機沈黙 quirk)。 */
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
            /* 81 01 00 <type> <mac6> + 0 埋め。type は全 role 0x03 [PABot-ref]。
             * dev_type 引数は 0x01 系では使わない (0x02 応答・kick 判定側は従来通り)。 */
            out[0] = 0x81u; out[1] = 0x01u; out[2] = 0x00u;
            out[3] = 0x03u;
            (void)dev_type;
            memcpy(&out[4], mac, 6);
            return 64;
        case 0x04u:
            /* Change B: 実機は 80 04 に沈黙する (known FW quirk)。
             * handshake_done=true は受信路 (usb_wired.c) が 80 04 で立てる
             * (本 builder の戻りとは独立)。受信路は戻り 64 のときのみ pend
             * するため、0 が沈黙化する。80 05 は応答を保つ。 */
            return 0;
        case 0x02u:
        case 0x03u:
        case 0x05u:
        case 0x06u:
            /* 05/06 の応答有無は 2wiCC の実働で確認 (R-T1-1 解消)。 */
            out[0] = 0x81u; out[1] = sub;
            return 64;
        default:
            out[0] = 0x81u; out[1] = sub;
            return 64;
    }
}

/* 0x21 応答の共通 12B (2wiCC ControllerData 互換)。
 * timer・電池・接続・姿勢は ctx から。電池は充電中+満充電固定。
 * role 適用: ボタンは role 輸送3B、片手 Joy の無い側スティックは中央埋め。
 * role=0 は従来通り (両スティック live・ボタン素通し)。 */
void usb_pack_controller_data(uint8_t out12[12], const usb_sub_ctx_t *ctx)
{
    uint8_t b3[3];
    uint16_t lx, ly, rx, ry;
    uint8_t role = (ctx->role <= 2u) ? ctx->role : 0u;
    usb_role_pack_btn3(ctx->btn, role, b3);
    lx = (role == 2u) ? 0x800u : ctx->lx;
    ly = (role == 2u) ? 0x800u : ctx->ly;
    rx = (role == 1u) ? 0x800u : ctx->rx;
    ry = (role == 1u) ? 0x800u : ctx->ry;
    out12[0] = ctx->timer;
    /* 電池・接続バイト: 全 role 0x91 [PABot-ref]。 */
    out12[1] = 0x91u;
    out12[2] = b3[0];
    /* Joy は b3[1] 素通し [PABot-ref]。ProCon は従来通り |0x80 (Golden で pin 留め)。 */
    out12[3] = (role == 0u) ? (uint8_t)(b3[1] | 0x80u) : b3[1];
    /* B2 マスクは role 条件付き: JoyL (role1) の輸送 B2 は SL/SR (b4/b5 =
     * 0x30) を含むため素通し。role0/2 は従来通り &0xCF で role0 バイト同一。
     * (role2 の輸送 B2 は常に 0 のためマスク有無で不変。) */
    out12[4] = (role == 1u) ? b3[2] : (uint8_t)(b3[2] & 0xCFu);
    pack_stick_12bit(lx, ly, &out12[5]);
    pack_stick_12bit(rx, ry, &out12[8]);
    out12[11] = 0x09u;
}

int usb_build_30_report(const usb_sub_ctx_t *ctx, uint8_t out64[64])
{
    int k;
    uint8_t role;
    if (ctx == NULL || out64 == NULL) {
        return 0;
    }
    role = (ctx->role <= 2u) ? ctx->role : 0u;
    memset(out64, 0, 64);
    out64[0] = 0x30u;
    usb_pack_controller_data(&out64[1], ctx);
    if (role != 0u) {
        /* Joy 0x30 は vib [12]=0x00・IMU 36B ゼロ [PABot-ref]。報告長・形状は不変。 */
        out64[12] = 0x00u;
        return 64;
    }
    /* IMU 36B は静止時 1g の固定値 (3 試料 x 12B = accXYZ LE16 + gyroXYZ LE16)。
     * ジャイロ XYZ=0・加速度 X/Y=0・Z=0x4000 (16384 = 1g) を全試料に書く。
     * 全ゼロ + 0x40 IMU-enable ACK は矛盾するため (host fault 検出の恐れ)。
     * 報告長・形状は不変 (ID + 12B ControllerData + 36B IMU + 15B 埋め)。
     * ProCon のみ (Joy は上記ゼロ)。 */
    for (k = 0; k < 3; k++) {
        int base = 13 + k * 12;
        out64[base + 0] = 0x00u;
        out64[base + 1] = 0x00u;
        out64[base + 2] = 0x00u;
        out64[base + 3] = 0x00u;
        out64[base + 4] = 0x00u;
        out64[base + 5] = 0x40u;
    }
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

/* 0x02 機器情報本文。role0 は従来バイトと同一 (fw 03 48・straight MAC・
 * byte11 0x02。BT の 03 8B ではない)。
 * Joy (role!=0) は fw 04 33・MAC バイト反転・byte11 0x01 [PABot-ref]
 * (live Joy-L bytes: 82 02 04 33 01 02 <mac6-reversed> 01 01 00)。
 * dev_type (out[17]) のみ全 role で role 依存のまま (03/01/02)。 */
static void build_device_info_response(uint8_t *out, uint8_t role,
                                       const uint8_t mac[6])
{
    int k;
    out[13] = 0x82u;
    out[14] = 0x02u;
    if (role != 0u) {
        out[15] = 0x04u;
        out[16] = 0x33u;
    } else {
        out[15] = 0x03u;
        out[16] = 0x48u;
    }
    out[17] = usb_devtype_for_role(role);
    out[18] = 0x02u;
    if (role != 0u) {
        for (k = 0; k < 6; k++) {
            out[19 + k] = mac[5 - k];
        }
    } else {
        memcpy(&out[19], mac, 6);
    }
    out[25] = 0x01u;
    if (role != 0u) {
        out[26] = 0x01u;
    } else {
        /* 末尾は BT 応答と同値の 0x02 にする。0x01 では Switch が
         * SPI 色を使わない (有線の色が反映されない実測)。
         * 無線 (hid.c reply_device_info) の実働値に合わせる。 */
        out[26] = 0x02u;
    }
}

/* ProCon SPI 応答 (Golden で pin 留め。バイト同一を保つ)。
 * header framing (out[13..19]) 済みを前提とし、out[20..] を埋める。
 * 成功時 64、miss 時 0。 */
static int build_spi_response_procon(uint16_t addr, uint8_t want, uint8_t *out)
{
    const spi_entry_t *hit;
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

/* Joy SPI 応答 (provisional)。header framing 済みを前提とし out[20..] を埋める。
 * 判定順: 6050/601B exact 例外 → 0x6000 常に 0xFF serial-none (両 role。
 * 0x6000 は表エントリでもあるため table-hit より先) → spi_find hit かつ
 * want<=size は表値 (Change A で NEW) → 0x6086 は 0xFF ([PABot-ref]。
 * 共有表に加えず Joy 側のみ) → in-range blank ゼロ → else FF+ACK。
 * USB-transport divergence (BT Joy は full-blank のまま): 有線 init は
 * 6020/6080/603D/6086 を厳密に読み、ゼロは検証落ちの恐れがある
 * (ProCon+表値は通過実績)。USB はホストを止めない既定のため、応答内容で
 * 通る値を返す必要がある。表値は仕様値の写し (2wiCC 実働値)。
 * Joy 実機値待ちの provisional。旧 virtual-cal shadow (spi_virtual_joy)
 * は表値に置換され廃止 (scopeB CAL の NOT-zeros 契約は表値で満たす)。
 * Request pattern: ホストは色一式を 0x6050/want13 で、色情報フラグを
 * 0x601B/want1 単独または 0x6010 帯 bulk (0x601B を含む want16) で読む。
 * そのため exact-match を blank より先に判定する: 6050 → spi_color_6050
 * (13B、want へゼロ pad)、601B → 0x01 (1B、want へゼロ pad)。暫定ゼロで
 * 真っ黒になるのを避け、COLOR_SET 設定値を生かす。
 * Echo/capacity (旧USB実装互換): out[19] は want をそのまま返す
 * (旧実装通り)。spi_joy_blank の容量は 32 のため、want<=32 は blen==want、
 * 33..44 は blen=32 にゼロ pad (6050/601B の pad 形式と同型)。report 長は
 * 常に 64 (want44 でも out[20..63] に収まる)。validation (want==0/>44・
 * req_len<16) の return 0 は呼出側で先に済ませている。
 * 範囲判定は減算形 want <= 0x9000u - addr (addr<0x9000 のため underflow
 * なし。加算オーバーフロー検査より短い)。 */
static int build_spi_response_joy(uint16_t addr, uint8_t want, uint8_t *out)
{
    if (addr == 0x6050u) {
        uint8_t n = (want > 13u) ? 13u : want;
        memcpy(&out[20], spi_color_6050, n);
        if (want > n) {
            memset(&out[20 + n], 0, (size_t)(want - n));
        }
        return 64;
    }
    if (addr == 0x601Bu) {
        static const uint8_t joy_601b = 0x01u;
        uint8_t n = (want > 1u) ? 1u : want;
        memcpy(&out[20], &joy_601b, n);
        if (want > n) {
            memset(&out[20 + n], 0, (size_t)(want - n));
        }
        return 64;
    }
    if (addr == 0x6000u) {
        /* シリアル域は常に空 (0xFF serial-none)。両 role 共通。
         * 実機シリアル風の値は Switch 2 を 2162-0002 で落とす実測のため
         * (ProCon 側と同一理由。2wiCC も serial none 運用)。 */
        memset(&out[20], 0xFF, want);
        return 64;
    }
    {
        /* Change A: 既知 cal 番地は表値を出す (BT Joy は blank のまま。
         * 上の divergence 註記を参照)。want>size は表の断片を出さず
         * blank へ落とす (ProCon の不足時無応答とは異なる USB 既定)。 */
        const spi_entry_t *hit = spi_find(addr);
        if (hit != NULL && want <= hit->size) {
            memcpy(&out[20], hit->data, want);
            return 64;
        }
    }
    if (addr == 0x6086u) {
        /* 6086 は 0xFF fill [PABot-ref]。
         * 共有表には加えない (BT 影響回避。BT Joy は blank のまま)。 */
        memset(&out[20], 0xFF, want);
        return 64;
    }
    if (addr >= 0x6000u && addr < 0x9000u && want <= 0x9000u - addr) {
        uint8_t blen = 0u;
        const uint8_t *blank = spi_joy_blank(want, &blen);
        if (blank == NULL || blen == 0u) {
            return 0;
        }
        memcpy(&out[20], blank, blen);
        if (want > blen) {
            memset(&out[20 + blen], 0, (size_t)(want - blen));
        }
        return 64;
    }
    /* USB transport-default: 0x6000 未満・0x9000 以上・straddle のいずれも
     * 0xFF fill + ACK で 64 を返す (return 0 しない。ホストを止めない)。
     * BT は同域を無応答にする (transport 既定が異なる。hid.c:292-299)。 */
    memset(&out[20], 0xFF, want);
    return 64;
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
    /* Joy 0x21 の vib バイトは 0x00 [PABot-ref]。
     * 共有 12B ヘッダは 0x09 のまま (0x30 側は build_30_report が Joy を
     * 0x00 化済み)。ProCon は 0x09 のまま。 */
    if (((ctx->role <= 2u) ? ctx->role : 0u) != 0u) {
        out[12] = 0x00u;
    }
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
            build_device_info_response(out,
                (ctx->role <= 2u) ? ctx->role : 0u, ctx->mac);
            return 64;
        case 0x03u:
            out[13] = 0x80u;
            out[14] = 0x03u;
            return 64;
        case 0x10u: {
            uint16_t addr;
            uint8_t want;
            uint8_t role;
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
            role = (ctx->role <= 2u) ? ctx->role : 0u;
            if (role != 0u) {
                return build_spi_response_joy(addr, want, out);
            }
            return build_spi_response_procon(addr, want, out);
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
