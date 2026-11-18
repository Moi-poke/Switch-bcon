// test_imu3.c -- report-synchronous IMU delivery (spec §5.13).
// Regression for the red case: four PC samples [125,0,0,0] arriving before one
// 0x30 must retain the 125 in the report integral instead of losing it to a
// latest-three ring. Also fixes zero-arrival replay, steady 3/15ms pass-through,
// sample order, saturation/residual, requeue and role calibration.
// Build: via tests/host/CMakeLists.txt (ctest name: imu3).
#include <stdio.h>
#include <string.h>

#include "../../src/proto/imu3.h"
#include "../../src/usb/usb_hid.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static void sample(int16_t s[IMU3_AXES], int16_t ax, int16_t ay, int16_t az,
                   int16_t gx, int16_t gy, int16_t gz)
{
    s[0] = ax;
    s[1] = ay;
    s[2] = az;
    s[3] = gx;
    s[4] = gy;
    s[5] = gz;
}

static int16_t report_imu(const uint8_t rep[64], int slot, int axis)
{
    int off = 13 + slot * 12 + axis * 2;
    return (int16_t)(rep[off] | ((uint16_t)rep[off + 1] << 8));
}

static long long report_sum(const uint8_t rep[64], int axis)
{
    int k;
    long long sum = 0;
    for (k = 0; k < IMU3_SLOTS; k++) {
        sum += report_imu(rep, k, axis);
    }
    return sum;
}

static int build_report(const int16_t slots[IMU3_SLOTS][IMU3_AXES],
                        const int16_t latest[IMU3_AXES], uint8_t role,
                        uint8_t rep[64])
{
    usb_sub_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = role;
    ctx.imu_valid = true;
    ctx.imu3_valid = true;
    memcpy(ctx.imu, latest, sizeof(ctx.imu));
    memcpy(ctx.imu3, slots, sizeof(ctx.imu3));
    return usb_build_30_report(&ctx, rep);
}

int main(void)
{
    imu3_pending_t p;
    int16_t out3[IMU3_SLOTS][IMU3_AXES];
    int16_t out[IMU3_AXES];
    int16_t s[4][IMU3_AXES];
    uint8_t rep[64];

    printf("[0] red case: 4 samples [125,0,0,0] -> report integral keeps 125\n");
    imu3_reset(&p);
    sample(s[0], 1, 2, 3, 125, 0, 0);
    sample(s[1], 4, 5, 6, 0, 0, 0);
    sample(s[2], 7, 8, 9, 0, 0, 0);
    sample(s[3], 10, 11, 12, 0, 0, 0);
    imu3_push(&p, s[0]);
    imu3_push(&p, s[1]);
    imu3_push(&p, s[2]);
    imu3_push(&p, s[3]);
    CHECK(imu3_take(&p, out3, out), "take returns valid");
    CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 125,
          "gyro X integral across slots == 125");
    CHECK(out3[0][3] == 125 || out3[1][3] == 125 || out3[2][3] == 125,
          "isolated 125 survives in one slot");
    CHECK(out3[0][0] == 10 && out3[1][0] == 10 && out3[2][0] == 10 &&
          out3[0][1] == 11 && out3[0][2] == 12,
          "current accel repeated in all slots");
    CHECK(out[0] == 10 && out[1] == 11 && out[2] == 12 && out[3] == 0,
          "latest sample is the current accel");
    CHECK(build_report(out3, out, 0, rep) == 64 &&
          report_sum(rep, 3) == 125 && report_imu(rep, 0, 0) == 10,
          "USB 0x30 IMU 36B carries the same integral/current accel");

    printf("[1] steady 3/15ms is unchanged\n");
    imu3_reset(&p);
    sample(s[0], 1, 0, 0x1000, 10, 0, 0);
    sample(s[1], 2, 0, 0x1000, 20, 0, 0);
    sample(s[2], 3, 0, 0x1000, 30, 0, 0);
    imu3_push(&p, s[0]);
    imu3_push(&p, s[1]);
    imu3_push(&p, s[2]);
    CHECK(imu3_take(&p, out3, out), "take returns valid");
    CHECK(memcmp(out3[0], s[0], sizeof(out3[0])) == 0 &&
          memcmp(out3[1], s[1], sizeof(out3[1])) == 0 &&
          memcmp(out3[2], s[2], sizeof(out3[2])) == 0,
          "three samples are byte-identical slots (oldest -> newest)");
    CHECK(build_report(out3, out, 0, rep) == 64 && report_sum(rep, 3) == 60,
          "USB report gyro integral == 60");

    printf("[2] repeated report with no arrival does not replay motion\n");
    CHECK(imu3_take(&p, out3, out), "take still valid (pose held)");
    CHECK(out3[0][3] == 0 && out3[1][3] == 0 && out3[2][3] == 0,
          "gyro reads zero, not the previous 10/20/30");
    CHECK(out3[0][0] == 3 && out3[1][0] == 3 && out3[2][0] == 3 &&
          out3[0][2] == 0x1000,
          "latest accel is held in all slots");
    CHECK(build_report(out3, out, 0, rep) == 64 && report_sum(rep, 3) == 0,
          "USB report integral is zero");

    printf("[3] isolated sample at every position of a 4-sample burst\n");
    {
        int pos;
        for (pos = 0; pos < 4; pos++) {
            int k;
            imu3_reset(&p);
            for (k = 0; k < 4; k++) {
                sample(s[k], 0, 0, 0x1000, 0, k == pos ? 125 : 0, 0);
                imu3_push(&p, s[k]);
            }
            (void)imu3_take(&p, out3, out);
            CHECK(out3[0][4] + out3[1][4] + out3[2][4] == 125,
                  "gyro Y integral 125 regardless of burst position");
            if (pos <= 1) {
                CHECK(out3[0][4] == 125,
                      "oldest burst samples fold into slot 0");
            } else {
                CHECK(out3[pos - 1][4] == 125,
                      "newest two samples keep slots 1/2 in order");
            }
        }
    }

    printf("[4] short windows: gyro is not duplicated by padding\n");
    imu3_reset(&p);
    sample(s[0], 7, 8, 9, 125, 0, 0);
    imu3_push(&p, s[0]);
    (void)imu3_take(&p, out3, out);
    CHECK(out3[0][3] == 125 && out3[1][3] == 0 && out3[2][3] == 0,
          "1-sample window keeps integral 125 (not 375)");
    CHECK(out3[0][0] == 7 && out3[1][0] == 7 && out3[2][0] == 7,
          "current accel repeated for the padded slots");
    imu3_reset(&p);
    sample(s[0], 1, 0, 0, 10, 0, 0);
    sample(s[1], 2, 0, 0, 20, 0, 0);
    imu3_push(&p, s[0]);
    imu3_push(&p, s[1]);
    (void)imu3_take(&p, out3, out);
    CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 30,
          "2-sample window keeps integral 30 (not 60)");

    printf("[5] 40-sample stall burst: order/sum kept, then fresh zero\n");
    {
        int k;
        imu3_reset(&p);
        for (k = 0; k < 40; k++) {
            sample(s[0], (int16_t)k, 0, 0x1000, 1, 0, 0);
            imu3_push(&p, s[0]);
        }
        (void)imu3_take(&p, out3, out);
        CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 40,
              "sum of 40 unit samples is preserved");
        CHECK(out[0] == 39 && out3[0][0] == 39,
              "current accel is the newest sample");
        CHECK(p.pending == 0u && p.frames == 40u,
              "pending consumed once; frame counter unchanged");
        (void)imu3_take(&p, out3, out);
        CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 0,
              "next report has no replay");
    }

    printf("[6] int16 saturation carries residual into later slots\n");
    imu3_reset(&p);
    sample(s[0], 0, 0, 0x1000, 30000, 0, 0);
    sample(s[1], 0, 0, 0x1000, 30000, 0, 0);
    sample(s[2], 0, 0, 0x1000, 0, 0, 0);
    sample(s[3], 0, 0, 0x1000, 0, 0, 0);
    imu3_push(&p, s[0]);
    imu3_push(&p, s[1]);
    imu3_push(&p, s[2]);
    imu3_push(&p, s[3]);
    (void)imu3_take(&p, out3, out);
    CHECK(out3[0][3] == 32767 && out3[1][3] == 27233 && out3[2][3] == 0,
          "60000 becomes 32767 + residual 27233 (no wrap)");
    CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 60000,
          "saturated report still sums to the input");
    CHECK(p.clipped == 0u, "no unrepresentable residual");

    imu3_reset(&p);
    {
        int k;
        for (k = 0; k < 4; k++) {
            sample(s[0], 0, 0, 0x1000, 32767, 32767, -32768);
            imu3_push(&p, s[0]);
        }
    }
    (void)imu3_take(&p, out3, out);
    CHECK(out3[0][3] == 32767 && out3[1][3] == 32767 && out3[2][3] == 32767,
          "unrepresentable overflow clamps every slot");
    CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 98301,
          "clamped report keeps the maximum representable integral");
    CHECK(p.clipped > 0u, "clipped overflow is counted, not wrapped");

    printf("[7] drop/static timeout paths\n");
    imu3_reset(&p);
    sample(s[0], 5, 6, 7, 100, 0, 0);
    imu3_push(&p, s[0]);
    imu3_drop_pending(&p);
    CHECK(imu3_take(&p, out3, out), "valid pose survives pending drop");
    CHECK(out3[0][3] == 0 && out[0] == 5 && out[2] == 7,
          "dropped motion is not replayed; latest accel kept");
    imu3_set_static(&p);
    CHECK(imu3_take(&p, out3, out), "static take valid");
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0x1000 && out[3] == 0,
          "500ms path uses spec static 1g");

    printf("[8] USB send-failure requeue keeps unsent integral and order\n");
    imu3_reset(&p);
    sample(s[0], 1, 0, 0, 125, 0, 0);
    sample(s[1], 2, 0, 0, 0, 0, 0);
    sample(s[2], 3, 0, 0, 0, 0, 0);
    sample(s[3], 4, 0, 0, 0, 0, 0);
    imu3_push(&p, s[0]);
    imu3_push(&p, s[1]);
    imu3_push(&p, s[2]);
    imu3_push(&p, s[3]);
    (void)imu3_take(&p, out3, out);
    sample(s[0], 5, 0, 0, 7, 0, 0);
    imu3_push(&p, s[0]);
    imu3_requeue(&p, out3);
    CHECK(p.pending == 4u, "requeue prepends 3 slots to the new sample");
    (void)imu3_take(&p, out3, out);
    CHECK(out3[0][3] + out3[1][3] + out3[2][3] == 132,
          "retry + new sample integral == 125 + 7");
    CHECK(out[0] == 5, "newest accel still wins");

    printf("[9] role2 (Joy-R) calibration keeps the same integral\n");
    imu3_reset(&p);
    sample(s[0], 0, 10, 0, 0, 125, 0);
    sample(s[1], 0, 20, 0, 0, 0, 0);
    sample(s[2], 0, 30, 0, 0, 0, 0);
    sample(s[3], 0, 40, 0, 0, 0, 0);
    imu3_push(&p, s[0]);
    imu3_push(&p, s[1]);
    imu3_push(&p, s[2]);
    imu3_push(&p, s[3]);
    (void)imu3_take(&p, out3, out);
    CHECK(build_report(out3, out, 2, rep) == 64, "role2 0x30 builds");
    CHECK(report_sum(rep, 4) == -125, "role2 Y negation preserves |integral|");
    CHECK(report_imu(rep, 0, 1) == -40, "role2 current accel Y is negated");

    printf(fails ? "\nFAILED %d\n" : "\nOK\n", fails);
    return fails ? 1 : 0;
}
