// test_color_getinfo.c -- T_COLOR_GET / T_COLOR_INFO host tests (TDD RED).
// 提案ID（未確定・要裁定）: T_COLOR_GET=0x39 (PC->Pico LEN0・色読出要求)、
// T_COLOR_INFO=0x3A (Pico->PC LEN12・RGBx4応答)。ID未確定のため本ファイルでは
// 数値を局所定義し protocol.h の新設シンボルに依存しない（REDは
// コンパイル成功＋実行失敗で表す）。裁定後は protocol.h に定数化すること。
// Build: via tests/host/CMakeLists.txt (ctest name: color_getinfo)。
// Pure C11。tusb.h / btstack.h を含めない（TU分離則・grepで検証）。
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/proto/protocol.h"
#include "../../src/proto/dispatch.h"
#include "../../src/proto/spi.h"

/* 提案値（未確定）。裁定までは推測を恒久化しないこと。 */
#define T_COLOR_GET_P  ((uint8_t)0x39) /* PC->Pico LEN0 */
#define T_COLOR_INFO_P ((uint8_t)0x3A) /* Pico->PC LEN12 */

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    v3_session_t s;

    /* 要裁定の blocking question。可視化のみ（合否には数えない）。 */
    printf("[0] BLOCKING: ID裁定待ち (GET=0x39? INFO=0x3A?) — 提案値で検証する\n");
    printf("  INFO: proposed GET=0x39 LEN0 / INFO=0x3A LEN12\n");

    printf("[1] proto_expected_len entries (strict LEN)\n");
    {
        CHECK(proto_expected_len(T_COLOR_GET_P) == 0,
              "COLOR_GET LEN=0 (proposed 0x39)");
        CHECK(proto_expected_len(T_COLOR_INFO_P) == 12,
              "COLOR_INFO LEN=12 (proposed 0x3A)");
    }

    printf("[2] dispatch: GET queues reply, INFO stays ignored (Pico->PC)\n");
    v3_session_init(&s);
    {
        /* STATUS_REQ同形: 戻り値はV3_IGNOREのまま送信箱に積む想定。 */
        v3_live_t r = v3_on_frame(&s, T_COLOR_GET_P, NULL, 0, 0x60);
        CHECK(r == V3_IGNORE, "GET returns V3_IGNORE (outbox only)");
        CHECK(s.ob_n == 1, "GET queues COLOR_INFO send (proposed)");
    }
    v3_session_init(&s);
    {
        /* 送出型（STATUS/PONG/RUMBLE/PLAYER_INFO同様）はdispatch対象外。 */
        uint8_t info12[12];
        memcpy(info12, spi_color_6050, 12);
        CHECK(v3_on_frame(&s, T_COLOR_INFO_P, info12, 12, 0x61) == V3_IGNORE &&
              s.ob_n == 0 && s.fx == FX_NONE,
              "INFO is Pico->PC: dispatch ignores");
    }

    printf("[3] append-only: FX/ACT末尾値を固定（挿入はrenumbering hazard）\n");
    {
        CHECK(FX_EMULATE_MODE == 8, "FX tail==8 (EMULATE_MODE)");
        CHECK(ACT_SEND_RUMBLE == 5, "ACT tail==5 (RUMBLE)");
        /* 将来の FX_COLOR_* / ACT_SEND_COLOR_INFO は末尾追加のみ。 */
    }

    /* TU分離: 本TUは tusb.h / btstack.h を含まない（grepで検証・合否に数えない）。 */
    printf("[4] TU-uniqueness: no tusb.h/btstack.h in this TU (grep verified)\n");

    printf("[5] 12B passthrough: INFO payload == spi_color_6050先頭12B\n");
    {
        /* spi_color_6050は13B（RGBx4＋不明1B）。送出は先頭12Bの写し。 */
        const spi_entry_t *e = spi_find(0x6050);
        CHECK(e != NULL && e->size == 13, "SPI 6050 size==13");
        CHECK(e != NULL && memcmp(e->data, spi_color_6050, 13) == 0,
              "table aliases spi_color_6050");
        CHECK(spi_color_6050[12] == 0x00,
              "trailing unknown byte 0x00 (12B送出の対象外)");
        CHECK(spi_color_6050[0] == 0x82 && spi_color_6050[6] == 0x46 &&
              spi_color_6050[9] == 0xFF,
              "first 12B defaults (body/btn/L/R)");
    }

    printf("[6] version: PROTO_VER=4維持・FW_MINORは2->3へ（dispatch.h:14）\n");
    {
        /* 新規フレーム追加でも版交渉は不変（EMULATE_MODE前例 spec §5.6）。 */
        CHECK(PROTO_VER == 4, "PROTO_VER=4 (unchanged)");
        /* 新規フレームのためEMULATE前例（1->2）に倣い2->3。実装時に上げる。 */
        CHECK(FW_MINOR == 3u, "FW_MINOR==3 (COLOR new frames, dispatch.h:14)");
    }

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
