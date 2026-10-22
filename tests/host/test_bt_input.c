// test_bt_input.c -- BT 0x30 (IMU 36B 付加) と 0x41 引数抽出・送信周期の host test。
// BTstack 非依存の src/bt/hid_pack.c を固定する。IMU 36B の生成は
// usb_build_30_report() と同一であること（複製が無いこと）をバイト比較で担保する。
// Build: via tests/host/CMakeLists.txt (ctest name: bt_input).
#include <stdio.h>
#include <string.h>

#include "../../src/bt/hid_pack.h"
#include "../../src/usb/usb_hid.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static int all_zero(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

/* USB 0x30 を同じ入力で組み、BT の IMU 36B と一致することを確かめる。 */
static int imu_matches_usb(uint8_t role, bool imu_valid, const int16_t imu[6],
                           bool imu3_valid, const int16_t imu3[3][6])
{
    usb_sub_ctx_t ctx;
    uint8_t rep[64];
    uint8_t out[BT_INPUT30_IMU_LEN];
    uint8_t btn3[3] = {0x00u, 0x00u, 0x00u};
    uint8_t sticks[6] = {0x00u, 0x08u, 0x80u, 0x00u, 0x08u, 0x80u};
    uint16_t n;

    memset(&ctx, 0, sizeof(ctx));
    ctx.role = role;
    ctx.imu_valid = imu_valid;
    ctx.imu3_valid = imu3_valid;
    memcpy(ctx.imu, imu, sizeof(ctx.imu));
    memcpy(ctx.imu3, imu3, sizeof(ctx.imu3));
    if (usb_build_30_report(&ctx, rep) != 64) {
        return 0;
    }
    n = bt_pack_input30(out, 0x00u, btn3, sticks, true, role, imu_valid, imu,
                        imu3_valid, imu3);
    if (n != BT_INPUT30_IMU_LEN) {
        return 0;
    }
    return memcmp(&out[14], &rep[13], 36u) == 0;
}

int main(void)
{
    uint8_t out[BT_INPUT30_IMU_LEN];
    uint8_t btn3[3] = {0x12u, 0x34u, 0x56u};
    uint8_t sticks[6] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
    static const int16_t zero6[6] = {0, 0, 0, 0, 0, 0};
    static const int16_t zero36[3][6] = {{0}};
    int16_t imu[6] = {100, -200, 0x1000, 5, -6, 7};
    int16_t imu3[3][6] = {{1, 2, 3, 4, 5, 6},
                          {7, 8, 9, 10, 11, 12},
                          {13, 14, 15, 16, 17, 18}};
    usb_sub_ctx_t ctx;
    uint8_t rep[64];
    uint16_t n;
    int k;

    printf("[0] IMU disabled -> 14B (A1 30 timer 80 btn3 stick6 08)\n");
    memset(out, 0xAA, sizeof(out));
    n = bt_pack_input30(out, 0x7Au, btn3, sticks, false, 0u, false, zero6,
                        false, zero36);
    CHECK(n == BT_INPUT30_BASE_LEN, "returns 14");
    CHECK(out[0] == 0xA1u && out[1] == 0x30u, "HIDP header A1 30");
    CHECK(out[2] == 0x7Au, "timer passthrough");
    CHECK(out[3] == 0x80u, "battery 0x80");
    CHECK(memcmp(&out[4], btn3, 3u) == 0, "btn3 copied as-is");
    CHECK(memcmp(&out[7], sticks, 6u) == 0, "sticks6 copied as-is");
    CHECK(out[13] == 0x08u, "vibration 0x08");
    CHECK(all_zero(&out[14], BT_INPUT30_IMU_LEN - 14u),
          "IMU area stays zero when disabled");

    printf("[1] IMU enabled -> 50B, IMU 36B identical to USB 0x30\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = 0u;
    ctx.imu_valid = true;
    ctx.imu3_valid = true;
    memcpy(ctx.imu, imu, sizeof(ctx.imu));
    memcpy(ctx.imu3, imu3, sizeof(ctx.imu3));
    CHECK(usb_build_30_report(&ctx, rep) == 64, "usb builder returns 64");
    n = bt_pack_input30(out, 0x10u, btn3, sticks, true, 0u, true, imu, true,
                        imu3);
    CHECK(n == BT_INPUT30_IMU_LEN, "returns 50");
    CHECK(out[0] == 0xA1u && out[1] == 0x30u && out[2] == 0x10u,
          "header/timer unchanged by IMU");
    CHECK(out[13] == 0x08u, "vibration 0x08 stays at [13]");
    CHECK(memcmp(&out[14], &rep[13], 36u) == 0,
          "IMU 36B == usb_build_30_report[13..48]");

    printf("[2] role2 shares USB role conversion and 0x41 scaling\n");
    usb_imu_set_sensitivity(3u, 0u);
    CHECK(imu_matches_usb(2u, true, imu, true, imu3),
          "role2 imu3 path identical (default 3/0)");
    CHECK(imu_matches_usb(2u, true, imu, false, imu3),
          "role2 replicated path identical (default 3/0)");
    usb_imu_set_sensitivity(0u, 2u);
    CHECK(imu_matches_usb(2u, true, imu, true, imu3),
          "role2 identical after 0x41 selector change (0/2)");
    {
        /* 0x41 の換算が USB 側の保持値経由で BT にも効いていること
         * (role0 は不変・role2 だけ変わる)。 */
        uint8_t r0[64], r2[64];
        memset(&ctx, 0, sizeof(ctx));
        ctx.role = 0u;
        ctx.imu_valid = true;
        memcpy(ctx.imu, imu, sizeof(ctx.imu));
        (void)usb_build_30_report(&ctx, r0);
        ctx.role = 2u;
        (void)usb_build_30_report(&ctx, r2);
        CHECK(memcmp(&r0[13], &r2[13], 36u) != 0,
              "0x41 selectors change role2 bytes (shared hold)");
    }
    usb_imu_set_sensitivity(3u, 0u);

    printf("[3] IMU enabled but no sample received\n");
    n = bt_pack_input30(out, 0x00u, btn3, sticks, true, 0u, false, zero6,
                        false, zero36);
    CHECK(n == BT_INPUT30_IMU_LEN, "ProCon returns 50");
    {
        int ok = 1;
        for (k = 0; k < 3; k++) {
            const uint8_t *s = &out[14 + k * 12];
            /* accX/accY = 0, accZ = 0x1000, gyro = 0 (LE int16) */
            if (s[0] != 0u || s[1] != 0u || s[2] != 0u || s[3] != 0u) {
                ok = 0;
            }
            if (s[4] != 0x00u || s[5] != 0x10u) {
                ok = 0;
            }
            if (s[6] != 0u || s[7] != 0u || s[8] != 0u || s[9] != 0u ||
                s[10] != 0u || s[11] != 0u) {
                ok = 0;
            }
        }
        CHECK(ok, "ProCon static 1g (az=0x1000) in all 3 slots");
    }
    n = bt_pack_input30(out, 0x00u, btn3, sticks, true, 2u, false, zero6,
                        false, zero36);
    CHECK(n == BT_INPUT30_IMU_LEN, "Joy-R returns 50");
    CHECK(all_zero(&out[14], 36u), "Joy-R 36B stays zero [PABot-ref]");

    printf("[4] usb_build_30_report return contract (shared renderer)\n");
    memset(&ctx, 0, sizeof(ctx));
    CHECK(usb_build_30_report(&ctx, rep) == 64, "valid ctx -> 64");
    CHECK(usb_build_30_report(NULL, rep) == 0, "NULL ctx -> 0");

    printf("[5] 0x41 argument extraction (report[9]=sub, [10]=gyro, [11]=accel)\n");
    {
        uint8_t r[12];
        uint8_t g = 0xAAu;
        uint8_t a = 0xBBu;
        memset(r, 0, sizeof(r));
        r[9] = 0x41u;
        r[10] = 0x03u;
        r[11] = 0x01u;
        CHECK(bt_imu_sensitivity_args(r, 12, &g, &a) && g == 3u && a == 1u,
              "full report -> gyro=3 accel=1");
        g = 0xAAu;
        a = 0xBBu;
        CHECK(bt_imu_sensitivity_args(r, 11, &g, &a) && g == 3u && a == 0u,
              "11B -> accel defaults to 0");
        g = 0xAAu;
        a = 0xBBu;
        CHECK(!bt_imu_sensitivity_args(r, 10, &g, &a) && g == 0xAAu,
              "10B -> false (no argument)");
        r[9] = 0x40u;
        CHECK(!bt_imu_sensitivity_args(r, 12, &g, &a), "sub!=0x41 -> false");
        r[9] = 0x41u;
        CHECK(!bt_imu_sensitivity_args(NULL, 12, &g, &a), "NULL report -> false");
        CHECK(!bt_imu_sensitivity_args(r, 12, NULL, &a), "NULL gyro -> false");
        CHECK(!bt_imu_sensitivity_args(r, 12, &g, NULL), "NULL accel -> false");
    }

    printf("[6] 0x30 send interval (full: IMU off 7ms / on 15ms)\n");
    CHECK(bt_input_interval_ms(false, false) == 100u, "pre-full -> 100ms");
    CHECK(bt_input_interval_ms(false, true) == 100u, "pre-full IMU on -> 100ms");
    CHECK(bt_input_interval_ms(true, false) == 7u, "full IMU off -> 7ms");
    CHECK(bt_input_interval_ms(true, true) == 15u, "full IMU on -> 15ms");

    /* 未使用警告抑止は無し (全変数を検査で使用)。 */

    printf("\nRESULT: %s (%d failures)\n",
           fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails;
}
