/* Switch 2 コントローラ広告のみ受理。Company 0x0553 / VID 0x057E。
 * 移植元: pico-wakecon src/cap.c。変更点: util.h依存を除去
 * (util_is_zero → 局所helper。BTstack非依存は維持)。 */

#include <string.h>

#include "cap.h"

static bool cap_is_zero(const uint8_t *d, size_t n)
{
    size_t i;
    if (d == NULL) {
        return true;
    }
    for (i = 0u; i < n; i++) {
        if (d[i] != 0u) {
            return false;
        }
    }
    return true;
}

void cap_table_init(cap_table_t *table)
{
    if (table != NULL) {
        memset(table, 0, sizeof(*table));
    }
}

bool cap_parse(const uint8_t *adv, uint8_t adv_len, cap_hit_t *hit)
{
    uint8_t pos = 0u;
    bool found = false;
    if (adv == NULL || hit == NULL) {
        return false;
    }
    memset(hit, 0, sizeof(*hit));
    while ((uint16_t)pos + 1u < (uint16_t)adv_len) {
        uint8_t field_len = adv[pos];
        uint8_t field_type;
        const uint8_t *field;
        uint8_t i;
        if (field_len == 0u) {
            break;
        }
        if ((uint16_t)pos + 1u + (uint16_t)field_len > (uint16_t)adv_len) {
            return false;
        }
        field_type = adv[pos + 1u];
        field = &adv[pos + 2u];
        if (field_type == 0xffu) {
            if (field_len != (uint8_t)(CAP_MFG_SIZE + 1u)) {
                return false;
            }
            if (field[0] != 0x53u || field[1] != 0x05u) {
                return false;
            }
            if (field[5] != 0x7eu || field[6] != 0x05u) {
                return false;
            }
            hit->pid = (uint16_t)field[7] | ((uint16_t)field[8] << 8);
            hit->flag = field[11];
            for (i = 0u; i < 6u; i++) {
                hit->switch_mac[i] = field[12u + (5u - i)];
            }
            if (adv_len > CAP_ADV_SIZE) {
                return false;
            }
            memcpy(hit->payload, adv, adv_len);
            hit->payload_len = adv_len;
            found = true;
        }
        pos = (uint8_t)(pos + 1u + field_len);
    }
    return found;
}

int cap_offer(cap_table_t *table, const uint8_t addr[6], uint8_t addr_type,
              int rssi, const cap_hit_t *hit)
{
    uint8_t i;
    if (table == NULL || addr == NULL || hit == NULL) {
        return -1;
    }
    for (i = 0u; i < table->used; i++) {
        if (table->slot[i].addr_type == addr_type &&
            memcmp(table->slot[i].addr, addr, 6) == 0) {
            table->slot[i].sightings++;
            if (rssi > table->slot[i].rssi) {
                table->slot[i].rssi = rssi;
            }
            if (hit->flag == CAP_TYPE_WAKE) {
                table->slot[i].has_wake = true;
                table->slot[i].wake = *hit;
            }
            return (int)i;
        }
    }
    if (table->used >= CAP_TABLE_SIZE) {
        return -1;
    }
    i = table->used++;
    memcpy(table->slot[i].addr, addr, 6);
    table->slot[i].addr_type = addr_type;
    table->slot[i].rssi = rssi;
    table->slot[i].sightings = 1u;
    if (hit->flag == CAP_TYPE_WAKE) {
        table->slot[i].has_wake = true;
        table->slot[i].wake = *hit;
    } else {
        table->slot[i].has_wake = false;
    }
    return (int)i;
}

int cap_best_wake(const cap_table_t *table)
{
    uint8_t i;
    int best = -1;
    if (table == NULL) {
        return -1;
    }
    for (i = 0u; i < table->used; i++) {
        if (!table->slot[i].has_wake) {
            continue;
        }
        if (cap_is_zero(table->slot[i].wake.switch_mac, 6)) {
            continue;
        }
        if (best < 0 || table->slot[i].rssi > table->slot[best].rssi) {
            best = (int)i;
        }
    }
    return best;
}
bool cap_encode(const cap_saved_t *saved, uint8_t *blob)
{
    if (saved == NULL || blob == NULL) {
        return false;
    }
    if (cap_is_zero(saved->spoof, 6)) {
        return false;
    }
    memset(blob, 0, CAP_BLOB_SIZE);
    memcpy(&blob[0], saved->spoof, 6);
    blob[6] = saved->spoof_type;
    memcpy(&blob[7], saved->switch_mac, 6);
    memcpy(&blob[13], saved->payload, CAP_ADV_SIZE);
    blob[13u + 16u] = CAP_TYPE_WAKE;
    return true;
}

bool cap_decode(const uint8_t *blob, uint32_t length, cap_saved_t *saved)
{
    if (blob == NULL || saved == NULL) {
        return false;
    }
    if (length != CAP_BLOB_SIZE) {
        return false;
    }
    memset(saved, 0, sizeof(*saved));
    memcpy(saved->spoof, &blob[0], 6);
    saved->spoof_type = blob[6];
    memcpy(saved->switch_mac, &blob[7], 6);
    memcpy(saved->payload, &blob[13], CAP_ADV_SIZE);
    if (cap_is_zero(saved->spoof, 6)) {
        return false;
    }
    if (saved->payload[16] != CAP_TYPE_WAKE) {
        return false;
    }
    return true;
}