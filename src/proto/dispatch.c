// dispatch.c -- spec §5.4/§5.5/§5.6 の実装。
#include <string.h>

#include "dispatch.h"
#include "baud.h"

void v3_session_init(v3_session_t *s) {
    memset(s, 0, sizeof(*s));
    s->result = RESULT_OK;
    s->state_accept = true; // HELLO前・OK・DOWNGRADEDは受付
    s->player_valid = false;
    s->player_ever_sent = false;
}

static void ob_push(v3_session_t *s, v3_act_t act, uint8_t arg) {
    if (s->ob_n >= V3_OB_N) {
        s->ob_dropped++;
        return;
    }
    s->ob[s->ob_n].act = act;
    s->ob[s->ob_n].arg = arg;
    s->ob_n++;
}

// CONFIG拒否コード: 0x10 + TYPE下位 (spec §5.5)。
static uint8_t cfg_err(uint8_t type) {
    return (uint8_t)(0x10u | (type & 0x0Fu));
}

v3_live_t v3_on_frame(v3_session_t *s, uint8_t type,
                      const uint8_t *payload, uint8_t len, uint8_t seq) {
    (void)seq; // SEQ欠番検出はparser統計 (link_stats_t) が担う
    s->fx = FX_NONE;
    switch (type) {
        case T_STATE:
            if (!proto_state_len_ok(len)) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            if (!s->state_accept) { s->errcode = ERR_UNSUPPORTED; return V3_IGNORE; }
            return V3_APPLY_STATE;
        case T_NEUTRAL:
            return V3_APPLY_NEUTRAL; // UNSUPPORTED下でも安全停止は通す
        case T_PING:
            ob_push(s, ACT_SEND_PONG, seq);
            return V3_IGNORE;
        case T_HELLO: {
            uint8_t ver, flags;
            if (len != 2 || payload == NULL) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            ver = payload[0];
            flags = payload[1];
            s->hello_seen = true;
            s->hello_ver = ver;
            s->hello_flags = flags;
            s->auto_status = (flags & 0x01u) != 0u;
            if (ver == PROTO_VER) {
                s->result = RESULT_OK;
                s->state_accept = true;
            } else {
                s->result = RESULT_VER_UNSUPPORTED;
                s->state_accept = false;
            }
            ob_push(s, ACT_SEND_HELLO_ACK, 0);
            return V3_IGNORE;
        }
        case T_CAPTURE_START: {
            uint8_t sec;
            if (len != 1 || payload == NULL) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            sec = payload[0];
            if (sec < 1u || sec > 60u) { s->errcode = cfg_err(type); return V3_IGNORE; }
            s->cap_seconds = sec;
            s->fx = FX_CAPTURE_START;
            s->fx_arg = sec;
            return V3_IGNORE;
        }
        case T_BEACON_START:
            if (!s->cap_valid) { s->errcode = cfg_err(type); return V3_IGNORE; }
            s->fx = FX_BEACON_START;
            return V3_IGNORE;
        case T_COLOR_SET:
            if (len != 12 || payload == NULL) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            memcpy(s->color, payload, 12);
            s->fx = FX_COLOR_SET;
            return V3_IGNORE;
        case T_KEY_DELETE:
            s->fx = FX_KEY_DELETE;
            return V3_IGNORE;
        case T_WIRED_MODE: {
            uint8_t v;
            if (len != 1 || payload == NULL) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            v = payload[0];
            if (v > 1u) { s->errcode = cfg_err(type); return V3_IGNORE; }
            s->wired_val = v;
            s->fx = FX_WIRED_MODE;
            s->fx_arg = v;
            return V3_IGNORE;
        }
        case T_STATUS_REQ:
            ob_push(s, ACT_SEND_STATUS, 0);
            ob_push(s, ACT_SEND_PLAYER_INFO, 0);
            return V3_IGNORE;
        case T_BAUD_SET: {
            uint8_t v;
            if (len != 1 || payload == NULL) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            v = payload[0];
            if (!baud_idx_valid(v)) { s->errcode = cfg_err(type); return V3_IGNORE; }
            s->fx = FX_BAUD_SET;
            s->fx_arg = v;
            // B §3: 旧rateのままACK相当 (STATUS即時)。切替はguard後に双方。
            ob_push(s, ACT_SEND_STATUS, 0);
            return V3_IGNORE;
        }
        case T_BOOTSEL: {
            uint8_t v;
            if (len != 1 || payload == NULL) { s->errcode = ERR_BAD_LEN; return V3_IGNORE; }
            v = payload[0];
            if (v != 0x5Au) { s->errcode = cfg_err(type); return V3_IGNORE; }
            s->fx = FX_BOOTSEL;
            s->fx_arg = v;
            // WIRED/BAUD_SETと同型: 旧rateのままACK相当 (STATUS即時)。
            // 発火は500ms後の再起動で適用する (main.cが実行)。
            ob_push(s, ACT_SEND_STATUS, 0);
            return V3_IGNORE;
        }
        default:
            return V3_IGNORE;
    }
}

void v3_player_tick(v3_session_t *s) {
    if (!s->player_valid) return;
    if (s->player_ever_sent &&
        s->player_sent_lamp == s->player_lamp &&
        s->player_sent_flags == s->player_flags) {
        return; // 変化なし: 送出しない
    }
    ob_push(s, ACT_SEND_PLAYER_INFO, 0);
    s->player_sent_lamp = s->player_lamp;
    s->player_sent_flags = s->player_flags;
    s->player_ever_sent = true;
}

void v3_rumble_tick(v3_session_t *s) {
    uint8_t n0;
    if (!s->rumble_valid) return;
    if (s->rumble_ever_sent &&
        s->rumble_sent_l == s->rumble_l &&
        s->rumble_sent_r == s->rumble_r) {
        return; // 変化なし: 送出しない
    }
    n0 = s->ob_n;
    ob_push(s, ACT_SEND_RUMBLE, 0);
    if (s->ob_n == n0) return; // 満杯drop: sent_*凍結・次tick再試行
    s->rumble_sent_l = s->rumble_l;
    s->rumble_sent_r = s->rumble_r;
    s->rumble_ever_sent = true;
}

void v3_pack_status(uint8_t flags, uint8_t last_seq, uint16_t err_crc,
                    uint16_t err_drop, uint8_t errcode, uint8_t out[7]) {
    out[0] = flags;
    out[1] = last_seq;
    out[2] = (uint8_t)(err_crc & 0xFFu);
    out[3] = (uint8_t)((err_crc >> 8) & 0xFFu);
    out[4] = (uint8_t)(err_drop & 0xFFu);
    out[5] = (uint8_t)((err_drop >> 8) & 0xFFu);
    out[6] = errcode;
}
