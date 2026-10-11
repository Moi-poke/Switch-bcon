// imu_dummy.h -- Phase 0 実験用ダミーIMU (pico/tusb/btstack非依存)。
// USB有線・ProCon役の0x30 IMU領域[13..48]をCore0内生成器で埋める。
// imu_dummy.c と共に IMU_DUMMY=1 のときだけ link する。本実装ではない。
// host test (tests/host/test_imu_dummy.c) からも使うため、
// ビルド設定は IMU_DUMMY_* マクロでのみ受け、HW呼出しを持たない。
#ifndef BCON_IMU_DUMMY_H
#define BCON_IMU_DUMMY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// log行の最大長 (呼出側バッファ128Bに収まるよう96B以内で組む)。
#define IMUD_LOG_MAX 96

// IMU領域36B = 3サンプル×12B (1サンプル=accXYZ LE16+gyro123 LE16)。
#define IMUD_IMU_BYTES 36
#define IMUD_SLOTS 3

// 既定値 (CMakeから -DIMU_DUMMY_*= で上書きする。host testは同名macroで構成)。
#ifndef IMU_DUMMY_MODE
#define IMU_DUMMY_MODE 1 // 0=SINE 1=STEP
#endif
#ifndef IMU_DUMMY_GYRO_AXIS
#define IMU_DUMMY_GYRO_AXIS 2 // 0/1/2 = gyro_1/2/3
#endif
#ifndef IMU_DUMMY_G_RAW
#define IMU_DUMMY_G_RAW 5899 // STEP値 (int16、負可)
#endif
#ifndef IMU_DUMMY_T_MS
#define IMU_DUMMY_T_MS 4000u // STEP継続時間ms
#endif
#ifndef IMU_DUMMY_SINE_AMP_RAW
#define IMU_DUMMY_SINE_AMP_RAW 1475 // SINE振幅raw
#endif
#ifndef IMU_DUMMY_SINE_PERIOD_MS
#define IMU_DUMMY_SINE_PERIOD_MS 2000u // SINE周期ms
#endif
#ifndef IMU_DUMMY_FILL_MASK
#define IMU_DUMMY_FILL_MASK 7 // bit0=slot0。7=ALL 1=FIRST 2=MID 4=LAST
#endif
#ifndef IMU_DUMMY_ACC_X
#define IMU_DUMMY_ACC_X 0 // 静止加速度X (int16)
#endif
#ifndef IMU_DUMMY_ACC_Y
#define IMU_DUMMY_ACC_Y 0 // 静止加速度Y (int16)
#endif
#ifndef IMU_DUMMY_ACC_Z
#define IMU_DUMMY_ACC_Z 16384 // 静止加速度Z (int16、現行0x4000維持)
#endif
#ifndef IMU_DUMMY_FORCE
#define IMU_DUMMY_FORCE 0 // 1なら0x40/0x03有効条件を無視
#endif
#ifndef IMU_DUMMY_STEP_AUTOREPEAT_MS
#define IMU_DUMMY_STEP_AUTOREPEAT_MS 0u // 0=手動。>0で自動再トリガ間隔ms
#endif

// 範囲外の構成値はビルド時に落とす (実験の取違え防止)。
#if (IMU_DUMMY_MODE != 0) && (IMU_DUMMY_MODE != 1)
#error "IMU_DUMMY_MODE must be 0 (SINE) or 1 (STEP)"
#endif
#if (IMU_DUMMY_GYRO_AXIS < 0) || (IMU_DUMMY_GYRO_AXIS > 2)
#error "IMU_DUMMY_GYRO_AXIS must be 0..2"
#endif
#if (IMU_DUMMY_FILL_MASK < 0) || (IMU_DUMMY_FILL_MASK > 7)
#error "IMU_DUMMY_FILL_MASK must be 0..7 (bit0=slot0)"
#endif
#if (IMU_DUMMY_G_RAW < -32768) || (IMU_DUMMY_G_RAW > 32767)
#error "IMU_DUMMY_G_RAW must fit int16"
#endif
#if (IMU_DUMMY_ACC_X < -32768) || (IMU_DUMMY_ACC_X > 32767)
#error "IMU_DUMMY_ACC_X must fit int16"
#endif
#if (IMU_DUMMY_ACC_Y < -32768) || (IMU_DUMMY_ACC_Y > 32767)
#error "IMU_DUMMY_ACC_Y must fit int16"
#endif
#if (IMU_DUMMY_ACC_Z < -32768) || (IMU_DUMMY_ACC_Z > 32767)
#error "IMU_DUMMY_ACC_Z must fit int16"
#endif
#if (IMU_DUMMY_SINE_AMP_RAW < 0) || (IMU_DUMMY_SINE_AMP_RAW > 32767)
#error "IMU_DUMMY_SINE_AMP_RAW must be 0..32767"
#endif
#if (IMU_DUMMY_T_MS == 0u)
#error "IMU_DUMMY_T_MS must be > 0"
#endif
#if (IMU_DUMMY_SINE_PERIOD_MS == 0u)
#error "IMU_DUMMY_SINE_PERIOD_MS must be > 0"
#endif
#if (IMU_DUMMY_FORCE != 0) && (IMU_DUMMY_FORCE != 1)
#error "IMU_DUMMY_FORCE must be 0 or 1"
#endif

// 0x40/0x03の受信状態を初期値へ戻す。handshakeを落とす全箇所で呼ぶ。
void imu_dummy_reset_state(void);

// USB 0x01要求の観測。0x40/0x03/0x41/0x11のときだけtrueを返しlogへ1行書く。
// ACKを含む応答バイトは変えない (観測のみ)。reqはNUL終端を仮定しない。
bool imu_dummy_note_subcmd(const uint8_t *req, size_t len, char *log,
                           size_t log_size);

// ダミー出力の有効条件。FORCE=1なら常にtrue。
bool imu_dummy_active(void);

// 36BのIMU領域を埋める。now_msは呼出側が渡す (usb_hid.cはpico非依存)。
void imu_dummy_fill(uint8_t dst[IMUD_IMU_BYTES], uint32_t now_ms);

// STEP区間 [now_ms, now_ms+T_MS) を開始し、stepログ行をlogへ書く。
// 区間中の再呼出しは区間を捨てて取り直す。
void imu_dummy_step_trigger(uint32_t now_ms, char *log, size_t log_size);

// 0x30実送信の成功点で呼ぶ。区間内かつactiveのときN_actualを+1する。
void imu_dummy_on_report_sent(uint32_t now_ms);

// 毎tick呼ぶ。区間終了後の初tickで終了ログを1回出しtrueを返す。
// AUTOREPEAT>0なら終了ログの後、間隔経過で自動再トリガする。
bool imu_dummy_poll(uint32_t now_ms, char *log, size_t log_size);

// LOG行が行全体で "step" か (大小不問)。lineはNUL終端を仮定しない。
bool imu_dummy_is_step_line(const char *line, size_t len);

// int32値をint16範囲にclampしてLE 2Bへ書く。T10の直叩き用に公開する。
void imu_dummy_write_i16le(uint8_t dst[2], int32_t value);

// 起動時の構成1行をlogへ書く (usb_wired_initで1回)。
void imu_dummy_build_info(char *log, size_t log_size);

// §1診断用: 0x40有効flagと0x03 modeの読出し (diag行用)。
bool imu_dummy_is_enabled(void);
uint8_t imu_dummy_input_mode(void);

// §1診断用: reset_stateと同義だが、呼出し元whyをlogへ1行書く。
// 旧imu_dummy_reset_state()は残す (T5は変更しない)。
void imu_dummy_reset_state_why(char *log, size_t log_size, const char *why);

#ifdef __cplusplus
}
#endif
#endif
