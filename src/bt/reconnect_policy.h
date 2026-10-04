// reconnect_policy.h -- 能動再接続(page out)と discoverable の判定。
// BTstack 非依存の純関数だけを置く。link_conn.c は BTstack を触るため
// host test できないので、判定を切り離して tests/host で固定する。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 接続1回あたりの能動page予算。
 * 枯渇後は受動待機へ移行する。前の実装は giveup で probe_outgoing_tried
 * を戻すだけだったため 5 秒周期で無限に接続し直し、本体側が
 * 「使用するコントローラのボタンを押してください」を出し続けていた。 */
#define RECONNECT_PAGE_BUDGET 3u

/* Switch が接続を拒否した回数の上限。
 * 2026-10-01 実測: page->open->0x13 が約10秒周期で反復し、open 成功ごとに
 * 予算を補充していたため一度も枯渇せず止まらなかった。数えるのは
 * 「page した回数」ではなく「拒否された回数」。 */
#define RECONNECT_REJECT_LIMIT 3u

/* これより短いセッションは「実際に使われなかった」= 拒否とみなす。 */
#define RECONNECT_HEALTHY_MS 5000u

/* 拒否上限で待機している状態から再武装する手段は 2 つ。
 * (1) ホストからの T_RECONNECT (0x3B) — 明示的な「起こす」操作
 * (2) 受信接続 (Switch 側の page) — budget を見ない経路
 * 時間減衰での自動再試行はしない。本体を周期的にノックしないことが
 * この修正の目的であるため。 */

typedef struct {
    bool wired;           /* 有線: 二重認識防止で Classic に出ない */
    bool beacon;          /* 再生中: 偽装 MAC で名乗らない */
    bool hid_up;          /* 既に接続中 */
    bool host_known;      /* 保存済みホストがある */
    bool outgoing_tried;  /* page 済み・応答待ち */
    uint32_t budget_left; /* 残りの page 予算 */
    uint32_t rejects;     /* 連続する拒否回数 */
} reconnect_state_t;

/* このティックに能動pageしてよいか。 */
bool reconnect_should_page(const reconnect_state_t *st);

/* page した直後に呼ぶ。予算を1つ消費する。 */
void reconnect_consume_page(uint32_t *budget_left);

/* 切断が「拒否」か。HID が開いてから短時間で切られたものは
 * Switch 側の能動切断 (reason 0x13) であり、こちらから再挑戦する
 * べきではない。 */
bool reconnect_is_rejection(uint32_t held_ms);

/* 待機状態 (page 予算切れ・拒否上限到達) かどうか。ホストの
 * T_RECONNECT はこの状態を解除する。 */
bool reconnect_is_parked(uint32_t budget_left, uint32_t rejects);

/* inquiry に応じるか。ペア済みは既知の BD_ADDR で page されるため
 * discoverable は不要。未ペアは登録画面から見つけてもらう必要がある。 */
bool reconnect_should_be_discoverable(bool host_known, bool quiet);

/* connectable は電波生きている限り常に 1。 */
bool reconnect_should_be_connectable(bool quiet);
