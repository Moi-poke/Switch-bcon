#ifndef BCON_BT_HID_PACK_H
#define BCON_BT_HID_PACK_H

/* BT 0x30 input report assembly (Pico/BTstack非依存・host test可能)。
 * A1(HIDPヘッダ)+30+12B状態+[36B IMU] を組み立てる。
 * btn3/sticks6 は呼出側で role pack 済み。全ポインタは非NULL必須。 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_INPUT30_BASE_LEN 14u
#define BT_INPUT30_IMU_LEN  50u

/* 戻値: 14 (IMU無効/生成失敗) または 50 (IMU有効)。
 * out は BT_INPUT30_IMU_LEN 以上。 */
uint16_t bt_pack_input30(uint8_t out[BT_INPUT30_IMU_LEN], uint8_t timer,
                         const uint8_t btn3[3], const uint8_t sticks6[6],
                         bool imu_enabled, uint8_t role,
                         bool imu_valid, const int16_t imu[6],
                         bool imu3_valid, const int16_t imu3[3][6]);

/* 0x01 output report (BTstackはreport IDを除去済み) から0x41の
 * gyro/accel選択子を取り出す。subはreport[9]、引数はreport[10..11]。
 * 戻値: 引数あり=true。 */
bool bt_imu_sensitivity_args(const uint8_t *report, int report_size,
                             uint8_t *gyro_sel, uint8_t *accel_sel);

/* 0x30の送信周期。full modeでない間は100ms、full modeはIMU無効7ms・
 * 有効15ms。 */
uint32_t bt_input_interval_ms(bool full_mode, bool imu_enabled);

#ifdef __cplusplus
}
#endif
#endif
