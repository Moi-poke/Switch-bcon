// protocol.c -- CRC8 + sliding-window parser (v3). See spec/protocol_v3.md.
#include "protocol.h"
#include <string.h>

static const uint8_t CRC8_TABLE[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++)
        crc = CRC8_TABLE[crc ^ data[i]];
    return crc;
}

int proto_expected_len(uint8_t type) {
    switch (type) {
        case T_STATE:         return 8;
        case T_NEUTRAL:       return 0;
        case T_PING:          return 0;
        case T_HELLO:         return 2;
        case T_HELLO_ACK:     return 4;
        case T_STATUS:        return 7;
        case T_PONG:          return 1;
        case T_RUMBLE:        return 2;
        case T_PLAYER_INFO:   return 2;
        case T_CAPTURE_START: return 1;
        case T_BEACON_START:  return 0;
        case T_COLOR_SET:     return 12;
        case T_KEY_DELETE:    return 0;
        case T_WIRED_MODE:    return 1;
        case T_STATUS_REQ:    return 0;
        case T_BAUD_SET:      return 1;
        case T_BOOTSEL:       return 1;
        case T_EMULATE_MODE:  return 1;
        case T_COLOR_GET:      return 0;
        case T_COLOR_INFO:     return 12;
        default:              return -1;
    }
}

bool proto_state_len_ok(uint8_t plen) {
    return plen == 8u || plen == 12u;
}

void parser_init(parser_t *p, frame_cb_t cb, void *user, link_stats_t *stats) {
    p->len   = 0;
    p->cb    = cb;
    p->user  = user;
    p->stats = stats;
}

static inline void sat_inc16(uint16_t *v) { if (*v != 0xFFFF) (*v)++; }

static inline void acc_drop(parser_t *p, uint16_t n) {
    if (n >= p->len) { p->len = 0; return; }
    memmove(p->acc, p->acc + n, p->len - n);
    p->len -= n;
}

static inline void note_err(parser_t *p, uint8_t code) {
    if (!p->stats) return;
    sat_inc16(&p->stats->err_crc);
    p->stats->errcode = code;
}

static void parser_run(parser_t *p) {
    for (;;) {
        uint16_t i = 0;
        while (i < p->len && p->acc[i] != PROTO_SYNC) i++;
        if (i > 0) acc_drop(p, i);
        if (p->len < 3) return;

        uint8_t type = p->acc[1];
        uint8_t plen = p->acc[2];

        int exp = proto_expected_len(type);
        bool len_bad;
        if (type == T_STATE) {
            len_bad = !proto_state_len_ok(plen); /* LEN 8 legacy / LEN 12 u16 */
        } else {
            len_bad = (exp >= 0) ? (plen != (uint8_t)exp)
                                 : (plen > PROTO_MAX_PAYLOAD);
        }
        if (len_bad) {
            note_err(p, ERR_BAD_LEN);
            acc_drop(p, 1);
            continue;
        }

        uint16_t total = (uint16_t)plen + 5;
        if (p->len < total) return;

        uint8_t rx_crc = p->acc[total - 1];
        uint8_t calc   = crc8(&p->acc[1], (size_t)(plen + 3));
        if (calc != rx_crc) {
            note_err(p, ERR_BAD_CRC);
            acc_drop(p, 1);
            continue;
        }

        uint8_t seq = p->acc[3 + plen];
        if (p->stats) {
            if (p->stats->have_seq) {
                uint8_t expect = (uint8_t)(p->stats->last_seq + 1);
                if (seq != expect) {
                    sat_inc16(&p->stats->err_drop);
                    p->stats->errcode = ERR_SEQ_GAP;
                } else if (p->stats->errcode == ERR_SEQ_GAP) {
                    p->stats->errcode = ERR_OK;
                }
            }
            p->stats->last_seq = seq;
            p->stats->have_seq = true;
        }
        if (p->cb) p->cb(type, &p->acc[3], plen, seq, p->user);

        acc_drop(p, total);
    }
}

void parser_feed(parser_t *p, uint8_t byte) {
    if (p->len >= PROTO_ACC_SIZE) {
        note_err(p, ERR_OVERFLOW);
        acc_drop(p, 1);
    }
    p->acc[p->len++] = byte;
    parser_run(p);
}

void parser_feed_buf(parser_t *p, const uint8_t *data, size_t n) {
    for (size_t i = 0; i < n; i++) parser_feed(p, data[i]);
}

size_t frame_build(uint8_t *out, uint8_t type, const uint8_t *payload,
                   uint8_t len, uint8_t seq) {
    if (len > PROTO_MAX_PAYLOAD) return 0;
    size_t i = 0;
    out[i++] = PROTO_SYNC;
    out[i++] = type;
    out[i++] = len;
    for (uint8_t k = 0; k < len; k++) out[i++] = payload[k];
    out[i++] = seq;
    out[i] = crc8(&out[1], (size_t)(len + 3));
    return (size_t)(len + 5);
}
