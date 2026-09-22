#ifndef BCON_SPI_H
#define BCON_SPI_H

/* Pro Controller SPI フラッシュの中身。移植元: pico-wakecon src/spi.c/h。
 * 実機記録・資料の写し(仕様値)。出典: nxbt pairing session / dekuNukem /
 * CTCaer/jc_toolkit#28 (wakeconの帰属を継承)。USB (0x10応答) とBTで共有。
 * spi_color_6050 は COLOR_SET で書換える可変域。 */

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t addr;
    uint8_t size;
    const uint8_t *data;
} spi_entry_t;

extern const spi_entry_t SPI_TABLE[];
extern const uint8_t SPI_TABLE_N;
extern uint8_t spi_color_6050[13];  /* 本体/Btn/L/R + 不明1B */

const spi_entry_t *spi_find(uint16_t addr);

/* Joy-Con provisional zero-fill reply (zeros ONLY, explicitly provisional).
 * Source: switchnotes console_pairing_session ("bare minimum eeprom"
 * 0x6000-0x8FFF; zero-filled EEPROM paired OK, color shown black).
 * Contract: returns pointer to static zero buffer; sets *out_len = req_len
 * clamped to 32; req_len==0 or NULL out -> return NULL with *out_len
 * untouched-if-NULL-safe.
 * Call-site rule (callers: BT hid.c, USB usb_hid.c must branch):
 *   if (role == EMUL_ROLE_PROCON) { existing spi_find path, byte-identical }
 *   else { if (0x6000<=addr && addr<0x9000) reply spi_joy_blank(req_len)
 *          with ACK 0x90;
 *          else transport-default miss behavior (BT: no-reply as today;
 *          USB: 0xFF as today) }
 * (EMUL_ROLE_* from src/proto/protocol.h.) */
const uint8_t *spi_joy_blank(uint8_t req_len, uint8_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
