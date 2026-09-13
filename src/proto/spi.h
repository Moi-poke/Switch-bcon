#ifndef BCON_SPI_H
#define BCON_SPI_H

/* Pro Controller SPI フラッシュの中身。移植元: pico-wakecon src/spi.c/h。
 * 実機記録・資料の写し(仕様値)。出典: nxbt pairing session / dekuNukem /
 * CTCaer/jc_toolkit#28 (wakeconの帰属を継承)。USB (0x10応答) とBTで共有。
 * spi_color_6050 は COLOR_SET (Task 4) で書換える可変域。 */

#include <stdbool.h>
#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif
