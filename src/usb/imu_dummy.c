// imu_dummy.c -- Phase 0 実験用ダミーIMU (pico/tusb/btstack非依存)。
// 本実装ではない。HW呼出し・malloc・lockを持たない。
// 呼出はすべてCore0 (USB路) から。時刻差は (uint32_t)(now - t0) で測る。
#include "imu_dummy.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 0x40/0x03の保持状態 (usb_wired.cの格納と対。ACK応答は変えない)。
static bool s_imud_enabled;
static uint8_t s_imud_mode;

// STEP区間: [s_step_t0, s_step_t0+T_MS)。s_step_on=falseは区間外。
static bool s_step_on;
static uint32_t s_step_t0;
static uint32_t s_step_n; // 区間内の実送信成功数 (N_actual)
static uint32_t s_step_seq; // 通番 (trigger毎に+1)


void imu_dummy_reset_state(void)
{
    s_imud_enabled = false;
    s_imud_mode = 0u;
    s_step_on = false;
    s_step_n = 0u;
}


static uint8_t u8_at(const uint8_t *req, size_t len, size_t i)
{
    return (req != NULL && i < len) ? req[i] : 0u;
}


bool imu_dummy_note_subcmd(const uint8_t *req, size_t len, char *log,
                           size_t log_size)
{
    uint8_t sub;
    // USB 0x01要求: sub=req[10]。BTのreport[10]=引数とはindexが違う。
    if (req == NULL || len < 11u || req[0] != 0x01u) {
        return false;
    }
    sub = req[10];
    if (sub != 0x40u && sub != 0x03u && sub != 0x41u && sub != 0x11u) {
        return false;
    }
    if (log == NULL || log_size == 0u) {
        return true;
    }
    if (sub == 0x40u) {
        if (len >= 12u) {
            s_imud_enabled = (req[11] != 0u);
            snprintf(log, log_size, "IMUD sub40 arg=%02X en=%u",
                     (unsigned)req[11], s_imud_enabled ? 1u : 0u);
        } else {
            snprintf(log, log_size, "IMUD sub40 arg=00 en=%u (short)",
                     s_imud_enabled ? 1u : 0u);
        }
        return true;
    }
    if (sub == 0x03u) {
        if (len >= 12u) {
            s_imud_mode = req[11];
            snprintf(log, log_size, "IMUD sub03 mode=%02X",
                     (unsigned)req[11]);
        } else {
            // 引数不足: 状態は変えず、欠落バイトは00埋めで示す。
            snprintf(log, log_size, "IMUD sub03 mode=00 (short)");
        }
        return true;
    }
    if (sub == 0x41u) {
        // 感度設定4Bは観測のみ (ACKは現行のまま)。len不足は00埋め。
        snprintf(log, log_size, "IMUD sub41 args=%02X %02X %02X %02X len=%u%s",
                 (unsigned)u8_at(req, len, 11), (unsigned)u8_at(req, len, 12),
                 (unsigned)u8_at(req, len, 13), (unsigned)u8_at(req, len, 14),
                 (unsigned)len, (len < 15u) ? " (short)" : "");
        return true;
    }
    // 0x11 SPI書込。addr=req[14..11]を上位から、size=req[15]。観測のみ。
    snprintf(log, log_size, "IMUD sub11 addr=%02X%02X%02X%02X size=%02X len=%u%s",
             (unsigned)u8_at(req, len, 14), (unsigned)u8_at(req, len, 13),
             (unsigned)u8_at(req, len, 12), (unsigned)u8_at(req, len, 11),
             (unsigned)u8_at(req, len, 15), (unsigned)len,
             (len < 16u) ? " (short)" : "");
    return true;
}


bool imu_dummy_active(void)
{
    if (IMU_DUMMY_FORCE != 0) {
        return true;
    }
    return s_imud_enabled && s_imud_mode >= 0x30u && s_imud_mode <= 0x33u;
}


void imu_dummy_write_i16le(uint8_t dst[2], int32_t value)
{
    uint16_t u;
    if (dst == NULL) {
        return;
    }
    if (value > 32767) {
        value = 32767;
    } else if (value < -32768) {
        value = -32768;
    }
    u = (uint16_t)(value < 0 ? value + 65536 : value);
    dst[0] = (uint8_t)(u & 0xFFu);
    dst[1] = (uint8_t)((u >> 8) & 0xFFu);
}


// 現在の試験値 (gyro試験軸のraw)。STEP区間外・SINE以外は0。
static int32_t cur_test_value(uint32_t now_ms)
{
#if IMU_DUMMY_MODE == 0
    // SINE: round(AMP * sin(2π*(now mod P)/P))。時刻周回は剰余で吸収する。
    double phase = (double)(now_ms % (uint32_t)IMU_DUMMY_SINE_PERIOD_MS) *
        (2.0 * M_PI) / (double)(uint32_t)IMU_DUMMY_SINE_PERIOD_MS;
    double v = (double)IMU_DUMMY_SINE_AMP_RAW * sin(phase);
    return (int32_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
#else
    if (!s_step_on) {
        return 0;
    }
    if ((uint32_t)(now_ms - s_step_t0) < (uint32_t)IMU_DUMMY_T_MS) {
        return (int32_t)IMU_DUMMY_G_RAW;
    }
    return 0;
#endif
}


void imu_dummy_fill(uint8_t dst[IMUD_IMU_BYTES], uint32_t now_ms)
{
    int k;
    int32_t test = cur_test_value(now_ms);
    if (dst == NULL) {
        return;
    }
    for (k = 0; k < IMUD_SLOTS; k++) {
        uint8_t *s = &dst[k * 12];
        int32_t slot_test =
            ((IMU_DUMMY_FILL_MASK >> k) & 1) != 0 ? test : 0;
        // accは全slot共通。gyroは試験軸のみ、他軸とmask外slotは0。
        imu_dummy_write_i16le(&s[0], (int32_t)IMU_DUMMY_ACC_X);
        imu_dummy_write_i16le(&s[2], (int32_t)IMU_DUMMY_ACC_Y);
        imu_dummy_write_i16le(&s[4], (int32_t)IMU_DUMMY_ACC_Z);
        imu_dummy_write_i16le(&s[6], IMU_DUMMY_GYRO_AXIS == 0 ? slot_test : 0);
        imu_dummy_write_i16le(&s[8], IMU_DUMMY_GYRO_AXIS == 1 ? slot_test : 0);
        imu_dummy_write_i16le(&s[10], IMU_DUMMY_GYRO_AXIS == 2 ? slot_test : 0);
    }
}


void imu_dummy_step_trigger(uint32_t now_ms, char *log, size_t log_size)
{
    s_step_on = true;
    s_step_t0 = now_ms;
    s_step_n = 0u;
    s_step_seq++;
    if (log == NULL || log_size == 0u) {
        return;
    }
    snprintf(log, log_size,
             "IMUD step#%lu t0=%lu axis=%d g=%d T=%lu fill=%u acc=%d,%d,%d",
             (unsigned long)s_step_seq, (unsigned long)now_ms,
             IMU_DUMMY_GYRO_AXIS, IMU_DUMMY_G_RAW,
             (unsigned long)(uint32_t)IMU_DUMMY_T_MS,
             (unsigned)IMU_DUMMY_FILL_MASK, IMU_DUMMY_ACC_X, IMU_DUMMY_ACC_Y,
             IMU_DUMMY_ACC_Z);
}


void imu_dummy_on_report_sent(uint32_t now_ms)
{
    // 区間内かつactiveの送信だけ数える (STEP中の0x40(0)をNから除外するため)。
    if (!s_step_on || !imu_dummy_active()) {
        return;
    }
    if ((uint32_t)(now_ms - s_step_t0) < (uint32_t)IMU_DUMMY_T_MS) {
        s_step_n++;
    }
}


bool imu_dummy_poll(uint32_t now_ms, char *log, size_t log_size)
{
    // 終了ログは区間終了後の初tickで出す。AUTOREPEAT>0かつ間隔経過なら
    // 同じtickで再トリガも行う (区間は空けない)。1呼出し1行のため
    // 終了ログだけを返す (再開のstep行は出さない。再開区間の
    // パラメータは起動行と同一のため)。
    if (!s_step_on) {
        return false;
    }
    if ((uint32_t)(now_ms - s_step_t0) < (uint32_t)IMU_DUMMY_T_MS) {
        return false;
    }
    if (log != NULL && log_size > 0u) {
        snprintf(log, log_size, "IMUD step#%lu end N_actual=%lu T=%lu g=%d axis=%d fill=%u",
                 (unsigned long)s_step_seq, (unsigned long)s_step_n,
                 (unsigned long)(uint32_t)IMU_DUMMY_T_MS, IMU_DUMMY_G_RAW,
                 IMU_DUMMY_GYRO_AXIS, (unsigned)IMU_DUMMY_FILL_MASK);
    }
    if (IMU_DUMMY_STEP_AUTOREPEAT_MS > 0u &&
        (uint32_t)(now_ms - s_step_t0) >=
            (uint32_t)IMU_DUMMY_STEP_AUTOREPEAT_MS) {
        // AUTOREPEAT<=T_MSなら終了tickで即再開する (間隔は前回トリガ基準)。
        s_step_t0 = now_ms;
        s_step_n = 0u;
        s_step_seq++;
    } else {
        s_step_on = false;
    }
    return true;
}


bool imu_dummy_is_step_line(const char *line, size_t len)
{
    // 行全体が "step" (大小不問)。末尾CR/LF/空白のみ許容する。
    size_t n = len;
    if (line == NULL) {
        return false;
    }
    while (n > 0u && (line[n - 1u] == '\r' || line[n - 1u] == '\n' ||
                      line[n - 1u] == ' ' || line[n - 1u] == '\t')) {
        n--;
    }
    if (n != 4u) {
        return false;
    }
    return ((line[0] | 0x20) == 's' && (line[1] | 0x20) == 't' &&
            (line[2] | 0x20) == 'e' && (line[3] | 0x20) == 'p');
}


void imu_dummy_build_info(char *log, size_t log_size)
{
    // git由来はCMakeがON時のみ -DIMU_DUMMY_GIT_*= で渡す。未定義時はunknown。
    static const char *git_hash =
#ifdef IMU_DUMMY_GIT_HASH
        IMU_DUMMY_GIT_HASH;
#else
        "unknown";
#endif
    static const char *git_dirty =
#ifdef IMU_DUMMY_GIT_DIRTY
        IMU_DUMMY_GIT_DIRTY;
#else
        "1";
#endif
    if (log == NULL || log_size == 0u) {
        return;
    }
    snprintf(log, log_size,
             "IMUD build mode=%d axis=%d g=%d T=%lu fill=%u acc=%d,%d,%d force=%d interval=%u git=%s dirty=%s",
             IMU_DUMMY_MODE, IMU_DUMMY_GYRO_AXIS, IMU_DUMMY_G_RAW,
             (unsigned long)(uint32_t)IMU_DUMMY_T_MS,
             (unsigned)IMU_DUMMY_FILL_MASK, IMU_DUMMY_ACC_X, IMU_DUMMY_ACC_Y,
             IMU_DUMMY_ACC_Z, IMU_DUMMY_FORCE,
             (unsigned)IMU_DUMMY_INTERVAL_MS, git_hash, git_dirty);
}
