#include <string.h>

#include "hid_pack.h"
#include "usb_hid.h" /* usb_build_30_report / usb_sub_ctx_t */

uint16_t bt_pack_input30(uint8_t out[BT_INPUT30_IMU_LEN], uint8_t timer,
                         const uint8_t btn3[3], const uint8_t sticks6[6],
                         bool imu_enabled, uint8_t role,
                         bool imu_valid, const int16_t imu[6],
                         bool imu3_valid, const int16_t imu3[3][6])
{
    usb_sub_ctx_t ictx;
    uint8_t rep[64] = {0};

    memset(out, 0, BT_INPUT30_IMU_LEN);
    out[0] = 0xA1u;
    out[1] = 0x30u;
    out[2] = timer;
    out[3] = 0x80u;
    out[4] = btn3[0];
    out[5] = btn3[1];
    out[6] = btn3[2];
    memcpy(&out[7], sticks6, 6u);
    out[13] = 0x08u;
    if (!imu_enabled) {
        return BT_INPUT30_BASE_LEN;
    }
    memset(&ictx, 0, sizeof(ictx));
    ictx.role = role;
    ictx.imu_valid = imu_valid;
    ictx.imu3_valid = imu3_valid;
    memcpy(ictx.imu, imu, sizeof(ictx.imu));
    memcpy(ictx.imu3, imu3, sizeof(ictx.imu3));
    if (usb_build_30_report(&ictx, rep) != 64) {
        return BT_INPUT30_BASE_LEN; /* IMU部を載せない (呼出側がfallbackログ) */
    }
    memcpy(&out[14], &rep[13], 36u);
    return BT_INPUT30_IMU_LEN;
}

bool bt_imu_sensitivity_args(const uint8_t *report, int report_size,
                             uint8_t *gyro_sel, uint8_t *accel_sel)
{
    if (report == NULL || gyro_sel == NULL || accel_sel == NULL ||
        report_size <= 10 || report[9] != 0x41u) {
        return false;
    }
    *gyro_sel = report[10];
    *accel_sel = (report_size > 11) ? report[11] : 0u;
    return true;
}

uint32_t bt_input_interval_ms(bool full_mode, bool imu_enabled)
{
    if (!full_mode) {
        return 100u;
    }
    /* 0x30には5ms間隔のIMU試料を3スロット格納する。同じ3試料を
     * 15ms未満で反復送信すると、受信側が各スロットを新しい5ms試料
     * として処理し、実時間より過大に積分する可能性がある。 */
    return imu_enabled ? 15u : 7u;
}
