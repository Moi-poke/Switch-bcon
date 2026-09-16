/* 取込。LE 接続中・Classic 接続中は不可。 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "cap.h"
#include "hid.h"
#include "link.h"
#include "store.h"
#include "bt_compat.h"

#define SCAN_INTERVAL 0x0030u

cap_table_t probe_cap_table;
cap_saved_t probe_cap_saved;
bool probe_cap_valid;
bool probe_scanning;
static uint32_t cap_deadline_ms;

bool link_cap_start(uint8_t seconds)
{
    char text[64];
    if (probe_scanning || probe_beacon) {
        probe_line("C ERR BUSY");
        return false;
    }
    if (probe_hid_cid != 0u) {
        probe_line("C ERR CONNECTED");
        return false;
    }
    cap_table_init(&probe_cap_table);
    gap_set_scan_params(0u, SCAN_INTERVAL, SCAN_INTERVAL, 0u);
    gap_set_scan_duplicate_filter(false);
    gap_start_scan();
    probe_scanning = true;
    /* 電波を確保する (有線中の電源断から復帰する場合あり)。 */
    link_radio_update();
    cap_deadline_ms =
        to_ms_since_boot(get_absolute_time()) + (uint32_t)seconds * 1000u;
    snprintf(text, sizeof(text), "CAP-START %us. press HOME on Joy-Con",
             (unsigned)seconds);
    probe_line(text);
    return true;
}

void link_cap_report(const uint8_t *packet)
{
    bd_addr_t raddr;
    uint8_t rtype;
    int rssi;
    uint8_t dlen;
    const uint8_t *data;
    cap_hit_t hit;
    char text[128];
    int idx;
    if (!probe_scanning) {
        return;
    }
    gap_event_advertising_report_get_address(packet, raddr);
    rtype = gap_event_advertising_report_get_address_type(packet);
    rssi = (int)gap_event_advertising_report_get_rssi(packet);
    dlen = gap_event_advertising_report_get_data_length(packet);
    data = gap_event_advertising_report_get_data(packet);
    if (!cap_parse(data, dlen, &hit)) {
        return;
    }
    idx = cap_offer(&probe_cap_table, raddr, rtype, rssi, &hit);
    snprintf(text, sizeof(text),
             "CAP mac=%02x%02x%02x%02x%02x%02x type=%u pid=%04x flag=%02x rssi=%d idx=%d",
             raddr[0], raddr[1], raddr[2], raddr[3], raddr[4], raddr[5],
             (unsigned)rtype, (unsigned)hit.pid, (unsigned)hit.flag,
             rssi, idx);
    probe_line(text);
}

uint8_t link_cap_used(void)
{
    return probe_cap_table.used;
}

const cap_entry_t *link_cap_entry(uint8_t i)
{
    if (i >= probe_cap_table.used) {
        return NULL;
    }
    return &probe_cap_table.slot[i];
}

/* 一覧と保存を捨てる。次回 C から取り直す。 */
void link_cap_clear(void)
{
    cap_table_init(&probe_cap_table);
    memset(&probe_cap_saved, 0, sizeof(probe_cap_saved));
    probe_cap_valid = false;
    store_cap_forget();
    probe_line("cap cleared");
}

/* 取込期限。link_poll から呼ばれる。 */
void link_cap_tick(uint32_t now_ms)
{
    if (probe_scanning &&
        (int32_t)(now_ms - cap_deadline_ms) >= 0) {
        char text[112];
        int best;
        gap_stop_scan();
        probe_scanning = false;
        best = cap_best_wake(&probe_cap_table);
        if (best >= 0) {
            const cap_entry_t *e = &probe_cap_table.slot[best];
            probe_cap_valid = false;
            memcpy(probe_cap_saved.spoof, e->addr, 6);
            probe_cap_saved.spoof_type = e->addr_type;
            memcpy(probe_cap_saved.switch_mac, e->wake.switch_mac, 6);
            memcpy(probe_cap_saved.payload, e->wake.payload, CAP_ADV_SIZE);
            if (store_cap_save()) {
                probe_cap_valid = true;
            }
            snprintf(text, sizeof(text),
                     "CAP-DONE saved=%u spoof=%02x%02x%02x%02x%02x%02x",
                     probe_cap_valid ? 1u : 0u,
                     e->addr[0], e->addr[1], e->addr[2],
                     e->addr[3], e->addr[4], e->addr[5]);
        } else {
            snprintf(text, sizeof(text), "CAP-DONE saved=0 seen=%u",
                     (unsigned)probe_cap_table.used);
        }
        probe_line(text);
        /* 有線中なら電波を止め直す (取込で起こしていた場合)。 */
        link_radio_update();
    }
}
