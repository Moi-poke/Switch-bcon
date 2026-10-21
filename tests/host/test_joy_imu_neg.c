// test_joy_imu_neg.c -- role2 (Joy-Con R) のIMU軸変換ホスト検証。
//
// JOY_IMU_NEG_MODE で反転の組み合わせが変わるため、同じソースを
// モード別ターゲットでビルドして期待値を切り替える。
//   0=反転なし / 1=加速度+ジャイロ / 2=ジャイロのみ / 3=加速度のみ
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usb_hid.h"

#ifndef JOY_IMU_NEG_MODE
#define JOY_IMU_NEG_MODE 1
#endif

static int fails;

static void check(int cond, const char *msg) {
    if (cond) {
        printf("  PASS %s\n", msg);
    } else {
        printf("  FAIL %s\n", msg);
        fails++;
    }
}

/* LE 2B 一致。 */
static int le16_eq(const uint8_t *p, int16_t want) {
    uint16_t u = (uint16_t)want;
    return p[0] == (uint8_t)(u & 0xFFu) && p[1] == (uint8_t)(u >> 8);
}

int main(void) {
    usb_sub_ctx_t ctx;
    uint8_t rep[64];
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    ctx.imu[0] = 1000;
    ctx.imu[1] = 2000;
    ctx.imu[2] = 4096;
    ctx.imu[3] = 100;
    ctx.imu[4] = 200;
    ctx.imu[5] = 300;

    printf("[joy imu neg] mode=%d\n", JOY_IMU_NEG_MODE);
    check(usb_build_30_report(&ctx, rep) == 64, "builds 64B");

#if JOY_IMU_NEG_MODE == 4
    /* Ry(180°) は X も反転する。 */
    check(le16_eq(&rep[13 + 0], -1000), "ax negated (Ry180)");
    check(le16_eq(&rep[13 + 6], -100), "gx negated (Ry180)");
#else
    check(le16_eq(&rep[13 + 0], 1000), "ax unchanged");
    check(le16_eq(&rep[13 + 6], 100), "gx unchanged");
#endif

    /* 3スロットすべて同一。 */
    check(memcmp(&rep[13], &rep[25], 12) == 0, "slot0==slot1");
    check(memcmp(&rep[13], &rep[37], 12) == 0, "slot0==slot2");

#if JOY_IMU_NEG_MODE == 1
    check(le16_eq(&rep[13 + 2], -2000), "ay negated");
    check(le16_eq(&rep[13 + 4], -4096), "az negated");
    check(le16_eq(&rep[13 + 8], -200), "gy negated");
    check(le16_eq(&rep[13 + 10], -300), "gz negated");
#elif JOY_IMU_NEG_MODE == 2
    check(le16_eq(&rep[13 + 2], 2000), "ay unchanged (gyro-only mode)");
    check(le16_eq(&rep[13 + 4], 4096), "az unchanged (gyro-only mode)");
    check(le16_eq(&rep[13 + 8], -200), "gy negated");
    check(le16_eq(&rep[13 + 10], -300), "gz negated");
#elif JOY_IMU_NEG_MODE == 3
    check(le16_eq(&rep[13 + 2], -2000), "ay negated");
    check(le16_eq(&rep[13 + 4], -4096), "az negated");
    check(le16_eq(&rep[13 + 8], 200), "gy unchanged (accel-only mode)");
    check(le16_eq(&rep[13 + 10], 300), "gz unchanged (accel-only mode)");
#elif JOY_IMU_NEG_MODE == 4
    check(le16_eq(&rep[13 + 2], 2000), "ay unchanged (Ry180)");
    check(le16_eq(&rep[13 + 4], -4096), "az negated (Ry180)");
    check(le16_eq(&rep[13 + 8], 200), "gy unchanged (Ry180)");
    check(le16_eq(&rep[13 + 10], -300), "gz negated (Ry180)");
#else
    check(le16_eq(&rep[13 + 2], 2000), "ay unchanged");
    check(le16_eq(&rep[13 + 4], 4096), "az unchanged");
    check(le16_eq(&rep[13 + 8], 200), "gy unchanged");
    check(le16_eq(&rep[13 + 10], 300), "gz unchanged");
#endif

    /* 反転時の飽和: -32768 は反転軸なら 32767 へ。 */
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    ctx.imu[4] = (int16_t)0x8000;
    check(usb_build_30_report(&ctx, rep) == 64, "sat builds 64B");
#if JOY_IMU_NEG_MODE == 1 || JOY_IMU_NEG_MODE == 2
    check(le16_eq(&rep[13 + 8], 32767), "gy -32768 saturates when negated");
#else
    check(le16_eq(&rep[13 + 8], -32768), "gy -32768 unchanged when not negated");
#endif

    /* 3サンプル経路: imu3_valid のとき各スロットが別値になる。 */
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    ctx.imu3_valid = true;
    usb_imu_set_sensitivity(3u, 0u); /* 既定レンジへ戻す（前段の影響を断つ） */
    for (int k = 0; k < 3; k++) {
        ctx.imu3[k][0] = (int16_t)(10 + k);
        ctx.imu3[k][3] = (int16_t)(100 + k);
    }
    check(usb_build_30_report(&ctx, rep) == 64, "imu3 builds 64B");
#if JOY_IMU_NEG_MODE == 4
    check(le16_eq(&rep[13 + 0], -10), "slot0 uses imu3[0] (ax negated)");
    check(le16_eq(&rep[25 + 0], -11), "slot1 uses imu3[1] (ax negated)");
    check(le16_eq(&rep[37 + 0], -12), "slot2 uses imu3[2] (ax negated)");
#else
    check(le16_eq(&rep[13 + 0], 10), "slot0 uses imu3[0]");
    check(le16_eq(&rep[25 + 0], 11), "slot1 uses imu3[1]");
    check(le16_eq(&rep[37 + 0], 12), "slot2 uses imu3[2]");
#endif

    /* 0x6080 6軸水平オフセット: Joy-Con R は Z=-4081 (0xF00F)。 */
    {
        uint8_t req[16];
        uint8_t out[64];
        memset(req, 0, sizeof(req));
        req[0] = 0x01u;
        req[10] = 0x10u;
        req[11] = 0x80u;
        req[12] = 0x60u;
        req[15] = 6u;
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 2u;
        check(usb_build_21_reply(req, 16, out, 64, &ctx) == 64, "spi 6080 R builds");
        /* オフセット符号は加速度フレームに合わせる: 反転モードのみ -4081。 */
#if JOY_IMU_NEG_MODE == 1 || JOY_IMU_NEG_MODE == 3
        check(out[20 + 4] == 0x0Fu && out[20 + 5] == 0xF0u,
              "R horizontal Z offset -4081 (Joy R)");
        check(out[20 + 0] == 0x5Eu && out[20 + 1] == 0x01u,
              "R horizontal X offset 350 (unchanged by Rx180)");
#elif JOY_IMU_NEG_MODE == 4
        check(out[20 + 4] == 0x0Fu && out[20 + 5] == 0xF0u,
              "R horizontal Z offset -4081 (Ry180)");
        check(out[20 + 0] == 0xA2u && out[20 + 1] == 0xFEu,
              "R horizontal X offset -350 (negated by Ry180)");
#else
        check(out[20 + 4] == 0xF1u && out[20 + 5] == 0x0Fu,
              "R horizontal Z offset +4081 (L/ProCon frame)");
        check(out[20 + 0] == 0x5Eu && out[20 + 1] == 0x01u,
              "R horizontal X offset 350 (unchanged)");
#endif
        ctx.role = 1u;
        check(usb_build_21_reply(req, 16, out, 64, &ctx) == 64, "spi 6080 L builds");
        check(out[20 + 4] == 0xF1u && out[20 + 5] == 0x0Fu,
              "L horizontal Z offset +4081 (Joy L)");
    }

    /* role0はモードに関係なく無変換。 */
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 0u;
    ctx.imu_valid = true;
    ctx.imu[1] = 2000;
    ctx.imu[2] = 4096;
    ctx.imu[4] = 200;
    ctx.imu[5] = 300;
    check(usb_build_30_report(&ctx, rep) == 64, "role0 builds 64B");
    check(le16_eq(&rep[13 + 2], 2000), "role0 ay unchanged");
    check(le16_eq(&rep[13 + 4], 4096), "role0 az unchanged");
    check(le16_eq(&rep[13 + 8], 200), "role0 gy unchanged");
    check(le16_eq(&rep[13 + 10], 300), "role0 gz unchanged");

    printf("\nRESULT: %s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES",
           fails);
    return fails;
}
