#include <string.h>

#include "imu3.h"

static const int16_t k_static_sample[IMU3_AXES] = {
    0, 0, 0x1000, 0, 0, 0
};

void imu3_reset(imu3_pending_t *p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
}

void imu3_push(imu3_pending_t *p, const int16_t sample[IMU3_AXES])
{
    if (p == NULL || sample == NULL) {
        return;
    }
    if (p->n < IMU3_PENDING_MAX) {
        memcpy(p->buf[p->n], sample, sizeof(p->buf[0]));
        p->n++;
    } else {
        int k;
        for (k = 0; k < IMU3_AXES; k++) {
            p->old_sum[k] += (int64_t)p->buf[0][k];
        }
        memmove(&p->buf[0][0], &p->buf[1][0],
                sizeof(p->buf[0]) * (IMU3_PENDING_MAX - 1u));
        memcpy(p->buf[IMU3_PENDING_MAX - 1u], sample, sizeof(p->buf[0]));
    }
    memcpy(p->latest, sample, sizeof(p->latest));
    p->frames++;
    p->pending++;
    p->valid = true;
}

static int64_t clamp_i16(int64_t v, int16_t *out)
{
    if (v > 32767) {
        *out = 32767;
        return v - 32767;
    }
    if (v < -32768) {
        *out = -32768;
        return v + 32768;
    }
    *out = (int16_t)v;
    return 0;
}

/* Emit one axis of the three bins, carrying int16 overflow into the next
 * slot. Overflow that survives slot 2 cannot be represented in one 0x30. */
static void emit_axis(const int64_t bins[IMU3_SLOTS][IMU3_AXES],
                      int16_t out3[IMU3_SLOTS][IMU3_AXES],
                      int axis, uint32_t *clipped)
{
    int k;
    int64_t carry = 0;
    for (k = 0; k < IMU3_SLOTS; k++) {
        int16_t v;
        carry = clamp_i16(bins[k][axis] + carry, &v);
        out3[k][axis] = v;
    }
    if (carry != 0) {
        (*clipped)++;
    }
}

static void clear_pending(imu3_pending_t *p)
{
    p->n = 0u;
    p->pending = 0u;
    memset(p->old_sum, 0, sizeof(p->old_sum));
}

bool imu3_take(imu3_pending_t *p, int16_t out3[IMU3_SLOTS][IMU3_AXES],
               int16_t out[IMU3_AXES])
{
    int k;
    int j;

    if (p == NULL || out3 == NULL || out == NULL) {
        return false;
    }
    memset(out3, 0, sizeof(int16_t) * IMU3_SLOTS * IMU3_AXES);
    memset(out, 0, sizeof(int16_t) * IMU3_AXES);
    if (p->valid) {
        memcpy(out, p->latest, sizeof(p->latest));
    }
    if (!p->valid) {
        clear_pending(p);
        return false;
    }

    if (p->pending == 0u) {
        /* No arrival since the previous report: zero gyro, current accel.
         * Never replay the consumed slots. */
        for (k = 0; k < IMU3_SLOTS; k++) {
            for (j = 0; j < 3; j++) {
                out3[k][j] = p->latest[j];
            }
        }
        clear_pending(p);
        return true;
    }

    if (p->pending == 3u && p->n == 3u) {
        /* Nominal 200Hz path: three fresh samples -> byte-identical slots. */
        for (k = 0; k < IMU3_SLOTS; k++) {
            memcpy(out3[k], p->buf[k], sizeof(out3[k]));
        }
    } else if (p->pending < 3u) {
        /* Short window: keep received gyro in arrival order, zero-pad the
         * missing time, and hold the current accel in every slot. */
        for (k = 0; k < IMU3_SLOTS; k++) {
            for (j = 0; j < 3; j++) {
                out3[k][j] = p->latest[j];
            }
        }
        for (k = 0; k < (int)p->pending; k++) {
            for (j = 3; j < IMU3_AXES; j++) {
                out3[k][j] = p->buf[k][j];
            }
        }
    } else {
        /* More than three arrivals since the last report. Keep the newest
         * two samples in slots 1/2 and fold everything older into slot 0.
         * The three output slots then sum to the sum of every pending
         * sample, preserving the angle integral. */
        int64_t bins[IMU3_SLOTS][IMU3_AXES];
        int old_n = (int)p->n - 2;
        memset(bins, 0, sizeof(bins));
        for (j = 0; j < IMU3_AXES; j++) {
            bins[0][j] = p->old_sum[j];
        }
        for (k = 0; k < old_n; k++) {
            for (j = 0; j < IMU3_AXES; j++) {
                bins[0][j] += p->buf[k][j];
            }
        }
        for (j = 0; j < IMU3_AXES; j++) {
            bins[1][j] = p->buf[p->n - 2u][j];
            bins[2][j] = p->buf[p->n - 1u][j];
        }
        /* Current accel in every slot; the gyro bins below preserve the
         * whole-window angular integral. */
        for (k = 0; k < IMU3_SLOTS; k++) {
            for (j = 0; j < 3; j++) {
                out3[k][j] = p->latest[j];
            }
        }
        for (j = 3; j < IMU3_AXES; j++) {
            emit_axis(bins, out3, j, &p->clipped);
        }
    }

    clear_pending(p);
    return true;
}

void imu3_drop_pending(imu3_pending_t *p)
{
    if (p == NULL) {
        return;
    }
    clear_pending(p);
}

void imu3_set_static(imu3_pending_t *p)
{
    if (p == NULL) {
        return;
    }
    memcpy(p->latest, k_static_sample, sizeof(p->latest));
    p->valid = true;
    clear_pending(p);
}

void imu3_requeue(imu3_pending_t *p,
                  const int16_t slots[IMU3_SLOTS][IMU3_AXES])
{
    int16_t combined[IMU3_SLOTS + IMU3_PENDING_MAX][IMU3_AXES];
    int total;
    int folded;
    int k;

    if (p == NULL || slots == NULL) {
        return;
    }
    total = IMU3_SLOTS + (int)p->n;
    for (k = 0; k < IMU3_SLOTS; k++) {
        memcpy(combined[k], slots[k], sizeof(combined[0]));
    }
    memcpy(&combined[IMU3_SLOTS][0], &p->buf[0][0],
           sizeof(p->buf[0]) * p->n);
    folded = (total > IMU3_PENDING_MAX) ? (total - IMU3_PENDING_MAX) : 0;
    for (k = 0; k < folded; k++) {
        int j;
        for (j = 0; j < IMU3_AXES; j++) {
            p->old_sum[j] += (int64_t)combined[k][j];
        }
    }
    p->n = (uint8_t)(total - folded);
    memcpy(&p->buf[0][0], &combined[folded][0],
           sizeof(p->buf[0]) * p->n);
    p->pending += IMU3_SLOTS;
    p->valid = true;
}
