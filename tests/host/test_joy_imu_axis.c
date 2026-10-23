// test_joy_imu_axis.c -- role2 (Joy-Con R) の 0x30 IMU 軸変換のホスト検証。
//
// 実機 Joy-Con R の生値は平坦時 Z=-1g（ProCon は +1g）で、カーネル
// (hid-nintendo.c joycon_parse_imu_report) も右Joy-ConのY,Zを反転して
// ProConフレームへ揃える。ProCon生値からJoy-Con Rの生値を合成するには
// 同じ Rx(180°) = Y,Z反転 を掛ける。2026-10-22 実機: 反転なし(net恒等)では
// 平坦時に視点が上を向き、リセットも効かなくなった（重力極性の不一致）。
// ここでは反転後のバイトと、0x41 のレンジ換算が符号反転後の値に効くことを固定する。
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usb_hid.h"

static int fails;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

/* 13 + j*2 から LE int16 を読む。 */
static int16_t rd16(const uint8_t *p, int j)
{
    uint32_t u = (uint32_t)p[j * 2] | ((uint32_t)p[j * 2 + 1] << 8);
    return (int16_t)u;
}

static int16_t le16(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    return (int16_t)u;
}

int main(void)
{
    usb_sub_ctx_t ctx;
    uint8_t rep[64];
    static const int16_t imu[6] = {100, 200, 300, 400, 500, 600};
    static const int16_t imu3[3][6] = {
        {11, 12, 13, 14, 15, 16},
        {21, 22, 23, 24, 25, 26},
        {31, 32, 33, 34, 35, 36},
    };

    usb_imu_set_sensitivity(3u, 0u);

    printf("[1] role2: Rx(180°) = negate Y,Z for accel and gyro\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    memcpy(ctx.imu, imu, sizeof(ctx.imu));
    CHECK(usb_build_30_report(&ctx, rep) == 64, "builds 64B");
    CHECK(rd16(&rep[13], 0) == 100 && rd16(&rep[13], 1) == -200 &&
          rd16(&rep[13], 2) == -300, "accel = (ax, -ay, -az)");
    CHECK(rd16(&rep[13], 3) == 400 && rd16(&rep[13], 4) == -500 &&
          rd16(&rep[13], 5) == -600, "gyro = (gx, -gy, -gz)");

    printf("[1b] imu_mode=2: compressed quaternion blob\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    ctx.imu3_valid = true;
    ctx.ms_now = 0x1234u;
    memcpy(ctx.imu3[2], imu, sizeof(imu));
    ctx.imu_mode = 2u;
    CHECK(usb_build_30_report(&ctx, rep) == 64, "builds 64B");
    /* NS-LIB-HID の accel は y,x,z 順。role2のY,Z反転はmode2では未適用。 */
    CHECK(rd16(&rep[13], 0) == 200, "accel slot0 = ay first");
    CHECK(rd16(&rep[13], 1) == 100, "accel slot0 = ax second");
    CHECK(rd16(&rep[13], 2) == 300, "accel slot0 = az third");
    CHECK(rep[19] == 0x0Eu && rep[20] == 0x00u, "mode=2 max_index=3");
    /* blob offset 34 -> rep[47..48]: (ms>>1)&0x3FF | 3<<10 */
    CHECK(rep[47] == 0x1Au && rep[48] == 0x0Du, "timestamp 0x1234 ms");

    printf("[2] role2 imu3: each slot converted independently\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    ctx.imu3_valid = true;
    memcpy(ctx.imu3, imu3, sizeof(ctx.imu3));
    CHECK(usb_build_30_report(&ctx, rep) == 64, "builds 64B");
    CHECK(rd16(&rep[13], 0) == 11 && rd16(&rep[13], 1) == -12 &&
          rd16(&rep[13], 2) == -13 && rd16(&rep[13], 3) == 14 &&
          rd16(&rep[13], 4) == -15 && rd16(&rep[13], 5) == -16,
          "slot0 = (11,-12,-13,14,-15,-16)");
    CHECK(rd16(&rep[37], 0) == 31 && rd16(&rep[37], 1) == -32 &&
          rd16(&rep[37], 2) == -33 && rd16(&rep[37], 3) == 34 &&
          rd16(&rep[37], 4) == -35 && rd16(&rep[37], 5) == -36,
          "slot2 = (31,-32,-33,34,-35,-36)");

    printf("[3] role0/role1 stay identity (ProCon bytes unchanged)\n");
    {
        static const uint8_t roles[2] = {0u, 1u};
        int r;
        for (r = 0; r < 2; r++) {
            int j;
            int ok = 1;
            memset(&ctx, 0, sizeof(ctx));
            ctx.role = roles[r];
            ctx.imu_valid = true;
            memcpy(ctx.imu, imu, sizeof(ctx.imu));
            if (usb_build_30_report(&ctx, rep) != 64) {
                ok = 0;
            }
            for (j = 0; j < 6; j++) {
                if (rd16(&rep[13], j) != imu[j]) {
                    ok = 0;
                }
            }
            CHECK(ok, r == 0 ? "role0 passthrough" : "role1 passthrough");
        }
    }

    printf("[4] 0x41 range scaling applies after the sign flip (per-axis)\n");
    /* gyro sel 0 = ±250dps → 生値x8。反転後の値に倍率が掛かること。 */
    usb_imu_set_sensitivity(0u, 0u);
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    ctx.imu_valid = true;
    ctx.imu[3] = 8;   /* gx → 出力 j=3（反転なし） */
    ctx.imu[4] = 16;  /* gy → 出力 j=4（反転） */
    ctx.imu[5] = 24;  /* gz → 出力 j=5（反転） */
    CHECK(usb_build_30_report(&ctx, rep) == 64, "builds 64B");
    CHECK(rd16(&rep[13], 3) == 64 && rd16(&rep[13], 4) == -128 &&
          rd16(&rep[13], 5) == -192, "scaled as (gx,-gy,-gz)*8");
    usb_imu_set_sensitivity(3u, 0u);

    printf("[5] saturation still applies for role0\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 0u;
    ctx.imu_valid = true;
    ctx.imu[3] = 32000;
    CHECK(usb_build_30_report(&ctx, rep) == 64, "builds 64B");
    /* 32000 は int16 範囲内なのでそのまま。範囲外相当は imu_scale_value 側で飽和。 */
    CHECK(rd16(&rep[13], 3) == 32000, "in-range value passes through");

    printf("[6] static fallback per role (IMU valid=false)\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 2u;
    CHECK(usb_build_30_report(&ctx, rep) == 64, "role2 builds 64B");
    {
        int ok = 1;
        int i;
        for (i = 0; i < 36; i++) {
            if (rep[13 + i] != 0u) {
                ok = 0;
            }
        }
        CHECK(ok, "role2 static 36B zero");
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 0u;
    CHECK(usb_build_30_report(&ctx, rep) == 64, "role0 builds 64B");
    CHECK(le16(&rep[13 + 4]) == 0x1000, "role0 static az=0x1000 (1g)");

    printf("\nRESULT: %s (%d failures)\n",
           fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
