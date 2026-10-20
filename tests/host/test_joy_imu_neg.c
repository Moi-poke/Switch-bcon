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

    /* X軸は常に不変。 */
    check(le16_eq(&rep[13 + 0], 1000), "ax unchanged");
    check(le16_eq(&rep[13 + 6], 100), "gx unchanged");

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
#else
    check(le16_eq(&rep[13 + 2], 2000), "ay unchanged");
    check(le16_eq(&rep[13 + 4], 4096), "az unchanged");
    check(le16_eq(&rep[13 + 8], 200), "gy unchanged");
    check(le16_eq(&rep[13 + 10], 300), "gz unchanged");
#endif

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
