#ifndef CAP_H
#define CAP_H

/* Switch 2 wake ビーコン (31B) の取込。配置は ndeadly 表通り。
 * BTstack 非依存。GAP 報告の解釈のみ。 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAP_ADV_SIZE 31u
#define CAP_MFG_SIZE 26u
#define CAP_TABLE_SIZE 8u
#define CAP_TYPE_WAKE 0x81u
#define CAP_BLOB_SIZE 47u

typedef struct {
    uint16_t pid;
    uint8_t flag;
    uint8_t switch_mac[6];  /* 表記順 */
    uint8_t payload[CAP_ADV_SIZE];
    uint8_t payload_len;
} cap_hit_t;

typedef struct {
    uint8_t addr[6];  /* AdvA・表記順 */
    uint8_t addr_type;
    int rssi;
    uint32_t sightings;
    bool has_wake;
    cap_hit_t wake;
} cap_entry_t;

typedef struct {
    cap_entry_t slot[CAP_TABLE_SIZE];
    uint8_t used;
} cap_table_t;

typedef struct {
    uint8_t spoof[6];
    uint8_t spoof_type;
    uint8_t switch_mac[6];
    uint8_t payload[CAP_ADV_SIZE];
} cap_saved_t;

void cap_table_init(cap_table_t *table);
bool cap_parse(const uint8_t *adv, uint8_t adv_len, cap_hit_t *hit);
int cap_offer(cap_table_t *table, const uint8_t addr[6], uint8_t addr_type,
              int rssi, const cap_hit_t *hit);
int cap_best_wake(const cap_table_t *table);
bool cap_encode(const cap_saved_t *saved, uint8_t *blob);
bool cap_decode(const uint8_t *blob, uint32_t length, cap_saved_t *saved);

#ifdef __cplusplus
}
#endif

#endif
