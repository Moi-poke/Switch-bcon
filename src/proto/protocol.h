// protocol.h -- pico-bcon v3 binary serial protocol (PROTO_VER=3).
// SSOT: spec/protocol_v3.md. Frame: [SYNC=0xAB][TYPE][LEN][PAYLOAD][SEQ][CRC8].
// CRC-8/SMBUS over TYPE..SEQ. Buttons: u32-LE VIIPER order (22 bits used).
#ifndef PICO_BCON_PROTOCOL_H
#define PICO_BCON_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PROTO_VER          0x04
#define PROTO_SYNC         0xAB

#define PROTO_MAX_PAYLOAD  32
#define PROTO_FRAME_MAX    (PROTO_MAX_PAYLOAD + 5)
#define PROTO_ACC_SIZE     96

enum {
    T_STATE         = 0x01, // PC->Pico LEN=8 BTN u32 + LX LY RX RY (u8x4, center 0x80)
                             //        or LEN=12 BTN u32 + LX LY RX RY (u16LEx4, center 0x0800)
    T_NEUTRAL       = 0x02, // PC->Pico LEN=0
    T_PING          = 0x03, // PC->Pico LEN=0
    T_HELLO         = 0x10, // PC->Pico LEN=2 ver, flags
    T_HELLO_ACK     = 0x11, // Pico->PC LEN=4 ver, major, minor, result
    T_STATUS        = 0x20, // Pico->PC LEN=7 flags, last, crc16, drop16, err
    T_PONG          = 0x21, // Pico->PC LEN=1 echo of PING seq
    T_RUMBLE        = 0x22, // Pico->PC LEN=2 amp L/R (reserved v1)
    T_PLAYER_INFO   = 0x23, // Pico->PC LEN=2 lamp+flags
    T_CAPTURE_START = 0x30, // PC->Pico LEN=1 seconds 1-60
    T_BEACON_START  = 0x31, // PC->Pico LEN=0
    T_COLOR_SET     = 0x32, // PC->Pico LEN=12 RGB x4
    T_KEY_DELETE    = 0x33, // PC->Pico LEN=0
    T_WIRED_MODE    = 0x34, // PC->Pico LEN=1 0/1
    T_STATUS_REQ    = 0x35, // PC->Pico LEN=0
    T_BAUD_SET      = 0x36, // PC->Pico LEN=1 rate index (B §3)
};

enum {
    RESULT_OK              = 0x00,
    RESULT_VER_DOWNGRADED  = 0x01,
    RESULT_VER_UNSUPPORTED = 0x02,
};

enum {
    ERR_OK          = 0x00,
    ERR_BAD_LEN     = 0x01,
    ERR_BAD_CRC     = 0x02,
    ERR_SEQ_GAP     = 0x03,
    ERR_UNSUPPORTED = 0x04,
    ERR_OVERRUN     = 0x05,
    ERR_OVERFLOW    = 0x06,
};

// BTN u32 bits (VIIPER order, 1=pressed). Bits 22-31 reserved (send 0).
enum {
    BTN_B        = 1u << 0,
    BTN_A        = 1u << 1,
    BTN_Y        = 1u << 2,
    BTN_X        = 1u << 3,
    BTN_R        = 1u << 4,
    BTN_ZR       = 1u << 5,
    BTN_PLUS     = 1u << 6,
    BTN_RSTICK   = 1u << 7,
    BTN_DOWN     = 1u << 8,
    BTN_RIGHT    = 1u << 9,
    BTN_LEFT     = 1u << 10,
    BTN_UP       = 1u << 11,
    BTN_L        = 1u << 12,
    BTN_ZL       = 1u << 13,
    BTN_MINUS    = 1u << 14,
    BTN_LSTICK   = 1u << 15,
    BTN_HOME     = 1u << 16,
    BTN_CAPTURE  = 1u << 17,
    BTN_GR       = 1u << 18, // Switch 2 (ignored on Switch 1)
    BTN_GL       = 1u << 19, // Switch 2 (ignored on Switch 1)
    BTN_C        = 1u << 20, // Switch 2 chat (ignored on Switch 1)
    BTN_HEADSET  = 1u << 21,
};

#define BTN_RESERVED_MASK 0xFFC00000u

// STATUS flags byte 0.
enum {
    ST_USB_MOUNTED     = 1u << 0,
    ST_SWITCH_READY    = 1u << 1,
    ST_TIMEOUT_NEUTRAL = 1u << 2,
    ST_WDT_RECOVERED   = 1u << 3,
    ST_UART_OVERRUN    = 1u << 4,
    ST_WIRED_MODE      = 1u << 5,
    ST_BT_CONNECTED    = 1u << 6,
    ST_RUMBLE_SEEN     = 1u << 7, // 前回STATUS以降の振動受信あり
};

typedef struct {
    uint32_t buttons; // BTN_* bitmap
    uint16_t lx, ly, rx, ry; // 0..4095, center 0x800 (LEN=8 ingest is u8<<4)
} ctrl_state_t;

typedef struct {
    uint8_t  last_seq;
    uint16_t err_crc;  // saturating: CRC/format/overflow rejects
    uint16_t err_drop; // saturating: seq gap events (mod-256)
    uint8_t  errcode;  // last error reason (ERR_*)
    bool     have_seq;
} link_stats_t;

uint8_t crc8(const uint8_t *data, size_t len);

// Expected payload length for a known type, or -1 if variable/unknown.
int proto_expected_len(uint8_t type);

// T_STATE accepts LEN 8 (legacy u8x4 sticks) or LEN 12 (u16LEx4 sticks).
// The canonical length reported by proto_expected_len stays 8.
bool proto_state_len_ok(uint8_t plen);

typedef void (*frame_cb_t)(uint8_t type, const uint8_t *payload, uint8_t len,
                           uint8_t seq, void *user);

typedef struct {
    uint8_t  acc[PROTO_ACC_SIZE];
    uint16_t len;
    frame_cb_t cb;
    void *user;
    link_stats_t *stats;
} parser_t;

void parser_init(parser_t *p, frame_cb_t cb, void *user, link_stats_t *stats);
void parser_feed(parser_t *p, uint8_t byte);
void parser_feed_buf(parser_t *p, const uint8_t *data, size_t n);

// Writes a full frame into out (must be >= (size_t)len+5, len<=32).
// Returns total length, or 0 if len exceeds PROTO_MAX_PAYLOAD.
size_t frame_build(uint8_t *out, uint8_t type, const uint8_t *payload,
                   uint8_t len, uint8_t seq);

#endif
