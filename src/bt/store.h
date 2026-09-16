#ifndef BCON_STORE_H
#define BCON_STORE_H

/* 移植元: pico-wakecon src/store.h/c。
 * フラッシュ保存。Classic 鍵は SDK が自動保存。ここは番地・色・取込・有線のみ。
 * 変更点: 書込系は flash_safe_execute 経由 (dual-coreでCore1をlockoutするため)。
 * 読込系は起動時 (Core1起動前) のみ呼ぶ。TAG値はbcon独自 ('BCxx')。
 * wakecon ('NXxx') とは別名前空間 (同一Pico共存のため)。 */

#include <stdbool.h>
#include <stdint.h>

#include "btstack.h"
#include "cap.h"

#ifdef __cplusplus
extern "C" {
#endif

void store_host(bd_addr_t addr);
bool store_host_load(void);
void store_host_forget(void); // KEY_DELETE用 (bcon追加)
void store_wired(bool en);
bool store_wired_load(void);
bool store_wired_load_def(bool dflt); // 未保存時の既定値付き (bcon追加)
void store_baud(uint8_t idx); // B-0/§3共有レート表の永続 (変化時のみ1 write)
bool store_baud_load(uint8_t *out); // false=未保存 (bcon追加)
void store_color(void);
void store_color_load(void);
bool store_cap_save(void);
bool store_cap_load(void);
void store_cap_forget(void);

#ifdef __cplusplus
}
#endif

#endif
