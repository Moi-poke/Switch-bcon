// test_player_capsaved.c -- PLAYER_INFO flags bit2=cap_saved host tests.
// spec §5.8 (4.2): flags = bit0 IMU | bit1 vib | bit2 cap_saved
// (probe_cap_valid の写し。BEACON再生そのものは成功信号ではない)。
// PROTO_VER=4・LEN=2据置。dispatch.c:136-147 の差分検出はバイト比較のため
// bit2変化は追加ロジックなしで送出される。本テストはその契約を固定する。
// Build: via tests/host/CMakeLists.txt (ctest name: player_capsaved)。
// Pure C11。tusb.h / btstack.h を含めない。
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/proto/dispatch.h"
#include "../../src/proto/protocol.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    v3_session_t s;

    printf("[0] bit2 change emits (cap_saved 0->1)\n");
    v3_session_init(&s);
    {
        s.player_valid = true;
        s.player_lamp = 0x03;
        s.player_flags = 0x03; // IMU|vib、capなし
        v3_player_tick(&s);
        CHECK(s.ob_n == 1 && s.ob[0].act == ACT_SEND_PLAYER_INFO,
              "first valid -> queued once");
        s.player_flags = 0x07; // bit2立上げ (取込保存あり)
        v3_player_tick(&s);
        CHECK(s.ob_n == 2 && s.ob[1].act == ACT_SEND_PLAYER_INFO &&
              s.player_sent_flags == 0x07,
              "bit2 0->1 -> queued, sent=0x07");
        s.player_flags = 0x03; // bit2立下げ (取込クリア)
        v3_player_tick(&s);
        CHECK(s.ob_n == 3 && s.ob[2].act == ACT_SEND_PLAYER_INFO &&
              s.player_sent_flags == 0x03,
              "bit2 1->0 -> queued, sent=0x03");
    }

    printf("[1] byte identical -> quiet (bit2含め変化なしは送出しない)\n");
    v3_session_init(&s);
    {
        s.player_valid = true;
        s.player_lamp = 0x03;
        s.player_flags = 0x07; // cap_saved付きで安定
        v3_player_tick(&s);
        {
            uint8_t n = s.ob_n;
            v3_player_tick(&s);
            CHECK(s.ob_n == n, "identical flags -> quiet");
        }
        // lamp変化はbit2無関係でも送出する (回帰: 差分検出がbit2専用化していない)
        s.player_lamp = 0x04;
        v3_player_tick(&s);
        CHECK(s.ob_n == 2 && s.ob[1].act == ACT_SEND_PLAYER_INFO,
              "lamp change still queued");
    }

    printf("[2] STATUS_REQ still appends ACT_SEND_PLAYER_INFO\n");
    v3_session_init(&s);
    {
        bool has_status = false, has_pi = false;
        CHECK(v3_on_frame(&s, T_STATUS_REQ, NULL, 0, 5) == V3_IGNORE,
              "STATUS_REQ ignored (outbox only)");
        for (uint8_t k = 0; k < s.ob_n; k++) {
            if (s.ob[k].act == ACT_SEND_STATUS) has_status = true;
            if (s.ob[k].act == ACT_SEND_PLAYER_INFO) has_pi = true;
        }
        CHECK(s.ob_n == 2 && has_status && has_pi,
              "STATUS_REQ -> STATUS + PLAYER_INFO");
    }

    printf("[3] PLAYER_INFO LEN stays 2, PROTO_VER stays 4\n");
    {
        CHECK(proto_expected_len(T_PLAYER_INFO) == 2, "PLAYER_INFO LEN=2");
        CHECK(PROTO_VER == 4, "PROTO_VER=4");
        CHECK(FW_MINOR == 2u, "FW_MINOR stays 2 (no bump)");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
