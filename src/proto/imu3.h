/* Report-synchronous IMU sample delivery (spec §5.13).
 *
 * PC sends T_IMU_SAMPLE at 200Hz, but a 0x30 report is generated only every
 * 15ms (3x5ms slots). This module accumulates every accepted sample between
 * two 0x30 reports and consumes the batch exactly once at report generation.
 *
 * Guarantees:
 *  - N==3 pending samples pass through unchanged (steady 3/15ms).
 *  - N<3 samples are emitted in arrival order and zero-padded (gyro); the
 *    latest accel is repeated in all slots (current pose, no zero-g replay).
 *  - N>3 samples keep the oldest work folded into slot 0 and the newest two
 *    samples in slots 1/2, so the sum over the three slots equals the sum of
 *    all pending samples. An isolated [125,0,0,0] burst therefore reports
 *    125 in the first slot instead of losing it to a latest-three ring.
 *  - A report with zero pending samples emits zero gyro and the latest accel;
 *    consumed motion is never replayed.
 *  - Per-axis sums use int64 and clamping carries any overflow into the next
 *    slot (int16 saturation). Overflow that cannot fit a 3-slot report is
 *    counted in `clipped` instead of wrapping.
 *
 * This file is Pico/BTstack-independent and host-testable.
 */
#ifndef BCON_PROTO_IMU3_H
#define BCON_PROTO_IMU3_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMU3_SLOTS 3
#define IMU3_AXES 6
/* Raw samples retained; older ones are folded into old_sum. 6 keeps two
 * report periods of micro-motion order without an unbounded queue. */
#define IMU3_PENDING_MAX 6

typedef struct {
    int16_t buf[IMU3_PENDING_MAX][IMU3_AXES]; /* oldest first */
    int64_t old_sum[IMU3_AXES];               /* folded samples older than buf */
    int16_t latest[IMU3_AXES];                /* newest accepted sample */
    uint32_t frames;                          /* accepted sample count */
    uint32_t pending;                         /* samples since last take */
    uint32_t clipped;                         /* axes truncated by int16 saturation */
    uint8_t n;                                /* valid rows in buf (0..MAX) */
    bool valid;                               /* at least one sample ever */
} imu3_pending_t;

/* Clear all state (boot/reset). */
void imu3_reset(imu3_pending_t *p);

/* Accept one 6xint16 sample (ax ay az gx gy gz). */
void imu3_push(imu3_pending_t *p, const int16_t sample[IMU3_AXES]);

/* Consume all pending samples and produce the three 0x30 slots plus the
 * latest sample. Returns false only when no sample has ever been accepted
 * (out/out3 are zeroed); true means the caller should use the values.
 * out3 is oldest -> newest, matching usb_sub_ctx_t.imu3. */
bool imu3_take(imu3_pending_t *p, int16_t out3[IMU3_SLOTS][IMU3_AXES],
               int16_t out[IMU3_AXES]);

/* Drop pending motion without invalidating the latest pose. Used by the
 * 100ms no-report bound so a late report cannot dump stale motion. This is an
 * explicit bounded loss: the discarded interval's angle is not preserved
 * (fresh response is preferred over replaying a long backlog). */
void imu3_drop_pending(imu3_pending_t *p);

/* 500ms no-sample timeout: pending motion is dropped and the latest pose
 * becomes the spec static value (0,0,0x1000,0,0,0). */
void imu3_set_static(imu3_pending_t *p);

/* Re-prepend three already-generated slots (USB send-failure retry). They are
 * older than any samples that arrived meanwhile; pending/order/sum are kept
 * and `frames` is not advanced (these are not new sensor frames). */
void imu3_requeue(imu3_pending_t *p,
                  const int16_t slots[IMU3_SLOTS][IMU3_AXES]);

/* ---- main.c firmware bridge ----
 * Called by the actual 0x30 generation paths (wired usb_wired.c and wireless
 * bt/hid.c). Not used by the pure host test. bcon_imu_take3 consumes pending
 * samples; bcon_imu_requeue3 re-prepends an unsent USB report on send failure.
 */
bool bcon_imu_take3(int16_t out3[IMU3_SLOTS][IMU3_AXES],
                    int16_t out[IMU3_AXES]);
void bcon_imu_requeue3(const int16_t slots[IMU3_SLOTS][IMU3_AXES]);

#ifdef __cplusplus
}
#endif
#endif
