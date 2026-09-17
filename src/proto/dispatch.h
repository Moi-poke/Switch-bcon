// dispatch.h -- v3 PC->Pico フレームの意味処理 (Steps 1+2)。
// Pico/BTstack非依存・host test可。HW副作用は持たず、効果 (FX_*) と
// 送信箱 (ACT_*) を返す。実行 (Flash/TLV/BT/UART-TX) は main.c が行う。
// LEN/CRC検証は parser (protocol.c) が済ませた前提。範囲検証はここで行う。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#define FW_MAJOR 0u
#define FW_MINOR 1u

// Pico->PC 送信箱の動作 (main.cがflush時に新鮮なHW状態で実行する)。
typedef enum {
    ACT_NONE = 0,
    ACT_SEND_STATUS,    // STATUS即時 (STATUS_REQ・周期分はmainが直接作る)
    ACT_SEND_PONG,      // arg = PINGのSEQをエコー
    ACT_SEND_HELLO_ACK, // sessionの採用版・RESULTで作る
    ACT_SEND_PLAYER_INFO, // lamp+flags (変化時・STATUS_REQ付随)
} v3_act_t;

typedef struct {
    v3_act_t act;
    uint8_t arg;
} v3_ob_t;

#define V3_OB_N 8

// CONFIG受理時の効果 (main.cが実行する)。
// 末尾追加のみ (挿入はACT/FX値のrenumbering hazardのため禁止)。
typedef enum {
    FX_NONE = 0,
    FX_CAPTURE_START, // arg = 秒数1-60
    FX_BEACON_START,
    FX_COLOR_SET,     // session.color[12] を使用
    FX_KEY_DELETE,
    FX_WIRED_MODE,    // arg = 0/1
    FX_BAUD_SET,      // arg = rate index (B §3)
    FX_BOOTSEL,       // arg = magic (dev only: USB BOOTSEL reboot)
} v3_fx_t;

// parser通過フレームに対する live-state (STATE/NEUTRAL) の扱い。
typedef enum {
    V3_IGNORE = 0,     // 適用しない (未知型・拒否STATE)
    V3_APPLY_STATE,    // STATE payload適用 (呼出側が行う)
    V3_APPLY_NEUTRAL,  // 全解放適用 (UNSUPPORTED下でも適用=安全停止優先)
} v3_live_t;

typedef struct {
    // セッション状態
    bool hello_seen;
    uint8_t hello_ver, hello_flags;
    uint8_t result;      // RESULT_* (HELLO_ACK用)
    bool state_accept;   // falseはUNSUPPORTED時のみ (既定true: HELLO前も受付)
    uint8_t errcode;     // 直近エラー (STATUS ERRCODE用)
    bool auto_status;    // HELLO FLAGS bit0 (1Hz自動送信)
    bool cap_valid;      // main.cがlink状態から毎tick設定する
    uint8_t player_lamp, player_flags;   // mainが毎tick設定する
    bool player_valid;                   // 初回SUB 0x30受信までfalse
    uint8_t player_sent_lamp, player_sent_flags; // 最終送出値
    bool player_ever_sent;               // 初回は変化なしでも送出する
    // CONFIG受理値
    uint8_t cap_seconds;
    uint8_t wired_val;
    uint8_t color[12];
    // 効果・送信箱
    v3_fx_t fx;
    uint8_t fx_arg;
    uint8_t ob_n;
    v3_ob_t ob[V3_OB_N];
    uint8_t ob_dropped;
} v3_session_t;

void v3_session_init(v3_session_t *s);

// parser通過1フレームの意味処理。STATE/NEUTRALの適用は戻り値に従い
// 呼出側 (Core1高速路・テスト) が行う。CONFIG等の効果は s->fx で返す。
v3_live_t v3_on_frame(v3_session_t *s, uint8_t type,
                      const uint8_t *payload, uint8_t len, uint8_t seq);

// STATUS 7B組立 (純粋関数。spec §5.5)。
void v3_pack_status(uint8_t flags, uint8_t last_seq, uint16_t err_crc,
                    uint16_t err_drop, uint8_t errcode, uint8_t out[7]);

// PLAYER_INFO変化検出: validかつ(未送出|前回送出値と相違)なら
// ACT_SEND_PLAYER_INFOをqueueし送出値を更新する。純粋 (HW副作用なし)。
void v3_player_tick(v3_session_t *s);
