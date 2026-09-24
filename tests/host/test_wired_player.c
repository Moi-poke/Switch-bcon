// test_wired_player.c -- 有線プレイヤーランプ配管のREDテスト (TDD Red段階)。
// 有線の溝: 0x30受信値 usb_player は usb_wired.c 内 static に溜まるだけで
// 外部へ出ない (dead-end。usb_wired.c:118/324/336)。無線 (src/bt/hid.c) は
// 0x30 で probe_player_id を保存し 0x31 で B0+id を返す (:425-436) のに対し、
// 有線 usb_build_21_reply は 0x30/0x40/0x48 をACKのみ (:494-499) で 0x31
// 枝が無い。feeder (格納値 -> v3_player_tick入力) も存在しない。
// 未実装シンボル (save/get/feed) を呼ぶため、このテストはリンクエラー
// (LNK2019: unresolved external) で失敗するのが正しい。実装は別タスク。
// Build: via tests/host/CMakeLists.txt (ctest name: wired_player).
// Pure C11。test_usb.c と同じ include 様式 (tusb.h / btstack.h を含めない)。
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/proto/dispatch.h"
#include "../../src/proto/protocol.h"
#include "../../src/usb/usb_hid.h"

// 未実装: 有線 0x30 格納値の host-testable な出入口。FW側では
// tud_hid_set_report_cb (0x30受信) が save を呼び、usb_wired_task 側が
// get/feed で v3_player_tick へ流す想定。定義は生産コード側が後で提供する。
// リンク失敗 = RED証明 (typoではなく missing symbol)。
extern void usb_wired_player_save(uint8_t id);
extern uint8_t usb_wired_player_get(void);
extern void usb_wired_feed_player(v3_session_t *s);

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    printf("[0] stored 0x30 id is exported/readable (無線 probe_player_id 相当)\n");
    {
        usb_wired_player_save(0x05u);
        CHECK(usb_wired_player_get() == 0x05u, "save 0x05 -> get 0x05");
        usb_wired_player_save(0x00u);
        CHECK(usb_wired_player_get() == 0x00u, "save 0x00 -> get 0x00");
    }

    printf("[1] 0x31 readback returns B0+id (無線 hid.c:432-436 の鏡像)\n");
    {
        usb_sub_ctx_t ctx;
        uint8_t out[64];
        uint8_t req[11];
        memset(&ctx, 0, sizeof(ctx));
        usb_wired_player_save(0x05u);
        ctx.player = usb_wired_player_get();
        memset(req, 0, sizeof(req));
        req[0] = 0x01u;
        req[10] = 0x31u;
        int n = usb_build_21_reply(req, 11, out, 64, &ctx);
        CHECK(n == 64 && out[13] == 0xB0u && out[14] == 0x31u &&
              out[15] == 0x05u, "31 -> B0 31 id");
    }

    printf("[2] wired feeder maps stored id into v3_player_tick input\n");
    {
        v3_session_t s;
        v3_session_init(&s);
        usb_wired_player_save(0x03u);
        usb_wired_feed_player(&s);
        CHECK(s.player_valid && s.player_lamp == 0x03u,
              "feed maps 0x03 into session");
        v3_player_tick(&s);
        CHECK(s.ob_n == 1 && s.ob[0].act == ACT_SEND_PLAYER_INFO,
              "tick queues PLAYER_INFO after feed");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
