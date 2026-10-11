// test_imu_dummy.c -- Phase 0 ダミーIMU host tests (no Pico SDK needed).
// Covers: 0x40/0x03 state, 0x41/0x11 observe logs, STEP window + N_actual,
// FILL_MASK slots, wraparound, LE bytes, step-line match, FORCE.
// Build: via tests/host/CMakeLists.txt, IMU_DUMMY=ON configure only
// (ctest name: imu_dummy). Existing 15 tests are untouched.
// Configure matrix: A default (MODE1/MASK7), B MASK1, C FORCE1, D MODE0.
// STEP cases T6-T9 run only when MODE==1; T13 runs only when MODE==0.
#ifndef IMU_DUMMY
#define IMU_DUMMY 1
#endif
#include <stdio.h>
#include <string.h>
#include "../../src/usb/imu_dummy.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

// 1サンプル12B中のgyro試験軸オフセット (T7/T13用)。
static int gyro_off(void)
{
    return 6 + IMU_DUMMY_GYRO_AXIS * 2;
}

static int16_t rd_i16le(const uint8_t *p)
{
    uint16_t u = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return (int16_t)(u >= 0x8000u ? (int32_t)u - 65536 : (int32_t)u);
}

static void mk_req(uint8_t *req, size_t len, uint8_t sub)
{
    memset(req, 0, len);
    req[0] = 0x01u;
    if (len > 10) {
        req[10] = sub;
    }
}

int main(void)
{
    char log[128];

    printf("[T1] initial state is inactive\n");
    imu_dummy_reset_state();
    memset(log, 0, sizeof(log));
#if IMU_DUMMY_FORCE == 0
    CHECK(imu_dummy_active() == false, "T1 inactive after reset");
#else
    printf("  SKIP T1 (FORCE build)\n");
#endif

    printf("[T2] 0x40 + 0x03 gate the active flag\n");
#if IMU_DUMMY_FORCE == 0
    {
        uint8_t req[16];
        imu_dummy_reset_state();
        mk_req(req, sizeof(req), 0x40u);
        req[11] = 0x01u;
        CHECK(imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log)) == true,
              "T2 0x40 observed");
        CHECK(imu_dummy_active() == false, "T2 0x40 alone is inactive");
        mk_req(req, sizeof(req), 0x03u);
        req[11] = 0x30u;
        CHECK(imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log)) == true,
              "T2 0x03 observed");
        CHECK(imu_dummy_active() == true, "T2 0x40+0x03(0x30) is active");
        req[11] = 0x3Fu;
        imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
        CHECK(imu_dummy_active() == false, "T2 0x03(0x3F) is inactive");
        mk_req(req, sizeof(req), 0x40u);
        req[11] = 0x00u;
        imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
        CHECK(imu_dummy_active() == false, "T2 0x40(0) disables");
    }
#else
    printf("  SKIP T2 (FORCE build)\n");
#endif

    printf("[T3] 0x41 logs 4 args in order, 0x10 is ignored\n");
    {
        uint8_t req[16];
        mk_req(req, sizeof(req), 0x41u);
        req[11] = 0x11u;
        req[12] = 0x22u;
        req[13] = 0x33u;
        req[14] = 0x44u;
        CHECK(imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log)) == true,
              "T3 0x41 observed");
        CHECK(strstr(log, "args=11 22 33 44") != NULL, "T3 args order");
        CHECK(strstr(log, "(short)") == NULL, "T3 no short tag");
        mk_req(req, sizeof(req), 0x10u);
        CHECK(imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log)) == false,
              "T3 0x10 ignored");
    }

    printf("[T4] short 0x40 keeps state and tags (short)\n");
#if IMU_DUMMY_FORCE == 0
    {
        uint8_t req[11];
        imu_dummy_reset_state();
        mk_req(req, sizeof(req), 0x40u);
        CHECK(imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log)) == true,
              "T4 short 0x40 observed");
        CHECK(imu_dummy_active() == false, "T4 state unchanged");
        CHECK(strstr(log, "(short)") != NULL, "T4 short tag");
        CHECK(strstr(log, "arg=00") != NULL, "T4 zero-filled byte");
    }
#else
    printf("  SKIP T4 (FORCE build)\n");
#endif

    printf("[T5] reset returns to initial state\n");
    {
        uint8_t req[16];
        mk_req(req, sizeof(req), 0x40u);
        req[11] = 0x01u;
        imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
        mk_req(req, sizeof(req), 0x03u);
        req[11] = 0x30u;
        imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
        imu_dummy_reset_state();
#if IMU_DUMMY_FORCE == 0
        CHECK(imu_dummy_active() == false, "T5 inactive after reset");
#else
        CHECK(imu_dummy_active() == true, "T5 FORCE stays active");
#endif
    }

#if IMU_DUMMY_MODE == 1
    printf("[T6] fill before trigger: gyro 0, acc static\n");
    {
        uint8_t dst[IMUD_IMU_BYTES];
        int k;
        bool ok = true;
        imu_dummy_reset_state();
        imu_dummy_fill(dst, 1000u);
        for (k = 0; k < IMUD_SLOTS; k++) {
            const uint8_t *s = &dst[k * 12];
            ok = ok && rd_i16le(&s[0]) == (int16_t)IMU_DUMMY_ACC_X;
            ok = ok && rd_i16le(&s[2]) == (int16_t)IMU_DUMMY_ACC_Y;
            ok = ok && rd_i16le(&s[4]) == (int16_t)IMU_DUMMY_ACC_Z;
            ok = ok && rd_i16le(&s[6]) == 0 && rd_i16le(&s[8]) == 0 &&
                rd_i16le(&s[10]) == 0;
        }
        CHECK(ok, "T6 pre-trigger fill");
    }

    printf("[T7] fill after trigger follows FILL_MASK\n");
    {
        uint8_t dst[IMUD_IMU_BYTES];
        int k;
        bool ok = true;
        imu_dummy_reset_state();
        imu_dummy_step_trigger(1000u, log, sizeof(log));
        imu_dummy_fill(dst, 1000u);
        for (k = 0; k < IMUD_SLOTS; k++) {
            const uint8_t *s = &dst[k * 12];
            int16_t want =
                (((IMU_DUMMY_FILL_MASK >> k) & 1) != 0)
                    ? (int16_t)IMU_DUMMY_G_RAW
                    : 0;
            ok = ok && rd_i16le(&s[gyro_off()]) == want;
        }
        CHECK(ok, "T7 mask slots");
    }

    printf("[T8] 8ms counting gives N_actual=500 for T=4000\n");
    {
        // 既定T=4000のみ検証 (T_MS=4000/8=500)。T可変時は比例で検証する。
        uint32_t t;
        bool end_seen = false;
        imu_dummy_reset_state();
#if IMU_DUMMY_FORCE == 0
        {
            // on_report_sentはactiveのときだけ数えるため有効化する。
            uint8_t req[16];
            mk_req(req, sizeof(req), 0x40u);
            req[11] = 0x01u;
            imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
            mk_req(req, sizeof(req), 0x03u);
            req[11] = 0x30u;
            imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
        }
#endif
        imu_dummy_step_trigger(0u, log, sizeof(log));
        for (t = 0u; t < (uint32_t)IMU_DUMMY_T_MS; t += 8u) {
            imu_dummy_fill((uint8_t[IMUD_IMU_BYTES]){0}, t);
            imu_dummy_on_report_sent(t);
            if (imu_dummy_poll(t, log, sizeof(log))) {
                end_seen = true;
            }
        }
        CHECK(end_seen == false, "T8 no end inside window");
        CHECK(imu_dummy_poll((uint32_t)IMU_DUMMY_T_MS, log, sizeof(log)) == true,
              "T8 end at window close");
        {
            char want[32];
            unsigned expect = (unsigned)IMU_DUMMY_T_MS / 8u;
            snprintf(want, sizeof(want), "N_actual=%u", expect);
            CHECK(strstr(log, want) != NULL, "T8 N_actual count");
        }
        CHECK(imu_dummy_poll((uint32_t)IMU_DUMMY_T_MS + 1u, log, sizeof(log)) ==
                  (IMU_DUMMY_STEP_AUTOREPEAT_MS > 0u ? true : false),
              "T8 end logged once (or autorepeat rearms)");
    }

    printf("[T9] wraparound window still closes once\n");
    {
        uint32_t t0 = 0xFFFFFF00u;
        uint32_t tend = t0 + (uint32_t)IMU_DUMMY_T_MS;
        imu_dummy_reset_state();
        imu_dummy_step_trigger(t0, log, sizeof(log));
        CHECK(imu_dummy_poll(t0 + 100u, log, sizeof(log)) == false,
              "T9 no end inside wrapped window");
        CHECK(imu_dummy_poll(tend, log, sizeof(log)) == true,
              "T9 end after wrap");
    }
#else
    printf("  SKIP T6-T9 (SINE build)\n");
#endif

    printf("[T10] LE bytes\n");
    {
        uint8_t b[2];
        imu_dummy_write_i16le(b, 16384);
        CHECK(b[0] == 0x00u && b[1] == 0x40u, "T10 16384 -> 00 40");
        imu_dummy_write_i16le(b, -1475);
        CHECK(b[0] == 0x3Du && b[1] == 0xFAu, "T10 -1475 -> 3D FA");
        imu_dummy_write_i16le(b, 0);
        CHECK(b[0] == 0x00u && b[1] == 0x00u, "T10 0 -> 00 00");
        imu_dummy_write_i16le(b, 32767);
        CHECK(b[0] == 0xFFu && b[1] == 0x7Fu, "T10 32767 -> FF 7F");
        imu_dummy_write_i16le(b, -32768);
        CHECK(b[0] == 0x00u && b[1] == 0x80u, "T10 -32768 -> 00 80");
        imu_dummy_write_i16le(b, 40000);
        CHECK(b[0] == 0xFFu && b[1] == 0x7Fu, "T10 clamp high");
        imu_dummy_write_i16le(b, -40000);
        CHECK(b[0] == 0x00u && b[1] == 0x80u, "T10 clamp low");
    }

    printf("[T11] step line match\n");
    CHECK(imu_dummy_is_step_line("step", 4) == true, "T11 step");
    CHECK(imu_dummy_is_step_line("STEP\r\n", 6) == true, "T11 STEP CRLF");
    CHECK(imu_dummy_is_step_line("Step ", 5) == true, "T11 Step + space");
    CHECK(imu_dummy_is_step_line("steps", 5) == false, "T11 steps rejected");
    CHECK(imu_dummy_is_step_line(" step", 5) == false, "T11 leading space rejected");
    CHECK(imu_dummy_is_step_line("bootsel", 7) == false, "T11 bootsel rejected");
    {
        // Q3: s_loglineは32B非NULバッファ。後続ゴミを見ないことを検証する。
        char raw[32];
        memset(raw, 'X', sizeof(raw));
        memcpy(raw, "step", 4);
        CHECK(imu_dummy_is_step_line(raw, 4) == true, "T11 non-NUL step");
        memcpy(raw, "steps", 5);
        CHECK(imu_dummy_is_step_line(raw, 5) == false, "T11 non-NUL steps");
        CHECK(imu_dummy_is_step_line(NULL, 0) == false, "T11 NULL rejected");
    }

    printf("[T12] FORCE build is always active\n");
#if IMU_DUMMY_FORCE == 1
    imu_dummy_reset_state();
    CHECK(imu_dummy_active() == true, "T12 FORCE active");
#else
    printf("  SKIP T12 (non-FORCE build)\n");
#endif

#if IMU_DUMMY_MODE == 0
    printf("[T13] SINE values at phase points\n");
    {
        // P=2000: t=0→0、t=P/4→+AMP、t=P/2→0 (誤差±2)。FILL_MASK遵守。
        uint32_t p = (uint32_t)IMU_DUMMY_SINE_PERIOD_MS;
        uint8_t dst[IMUD_IMU_BYTES];
        int k;
        bool ok_zero = true, ok_peak = true, ok_half = true, ok_mask = true;
        imu_dummy_fill(dst, 0u);
        for (k = 0; k < IMUD_SLOTS; k++) {
            ok_zero = ok_zero && rd_i16le(&dst[k * 12 + gyro_off()]) == 0;
        }
        imu_dummy_fill(dst, p / 4u);
        for (k = 0; k < IMUD_SLOTS; k++) {
            int16_t got = rd_i16le(&dst[k * 12 + gyro_off()]);
            if (((IMU_DUMMY_FILL_MASK >> k) & 1) != 0) {
                int32_t d = (int32_t)got - (int32_t)IMU_DUMMY_SINE_AMP_RAW;
                ok_peak = ok_peak && d >= -2 && d <= 2;
            } else {
                ok_mask = ok_mask && got == 0;
            }
        }
        imu_dummy_fill(dst, p / 2u);
        for (k = 0; k < IMUD_SLOTS; k++) {
            int16_t got = rd_i16le(&dst[k * 12 + gyro_off()]);
            ok_half = ok_half && got >= -2 && got <= 2;
        }
        CHECK(ok_zero, "T13 phase 0");
        CHECK(ok_peak, "T13 phase P/4");
        CHECK(ok_half, "T13 phase P/2");
        CHECK(ok_mask, "T13 mask slots");
    }
#else
    printf("  SKIP T13 (STEP build)\n");
#endif

    printf("[T14] build info carries git provenance\n");
    {
        imu_dummy_build_info(log, sizeof(log));
        CHECK(strstr(log, "IMUD build ") == log, "T14 prefix");
        CHECK(strstr(log, "git=") != NULL, "T14 git hash field");
        CHECK(strstr(log, "dirty=") != NULL, "T14 dirty field");
        CHECK(strlen(log) < 128u, "T14 fits 128B buffer");
    }

    printf("[T15] reset with why logs caller and clears state\n");
    {
        uint8_t req[16];
        mk_req(req, sizeof(req), 0x40u);
        req[11] = 0x01u;
        imu_dummy_note_subcmd(req, sizeof(req), log, sizeof(log));
        imu_dummy_reset_state_why(log, sizeof(log), "umount");
        CHECK(strstr(log, "IMUD reset why=umount") != NULL, "T15 why line");
#if IMU_DUMMY_FORCE == 0
        CHECK(imu_dummy_active() == false, "T15 state cleared");
#endif
        CHECK(imu_dummy_is_enabled() == false, "T15 enabled cleared");
        CHECK(imu_dummy_input_mode() == 0u, "T15 mode cleared");
        imu_dummy_reset_state_why(NULL, 0u, NULL);
        CHECK(imu_dummy_is_enabled() == false, "T15 NULL-safe");
    }
    if (fails == 0) {
        printf("imu_dummy: ALL PASS\n");
        return 0;
    }
    printf("imu_dummy: %d FAILURES\n", fails);
    return 1;
}
