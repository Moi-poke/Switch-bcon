// pack.h -- u32論理ボタン (VIIPER順・spec §5.1) → Switch 1 輸送3B。
// BT 3BとUSB 0x30ボタン3Bは任天堂が同一順序のため単一pack関数を共有する
// (spec §11)。Pico/BTstack非依存・host test可。Y反転はPC側済みの前提。
#pragma once

#include <stdint.h>

#include "protocol.h"

// st->buttons をNintendo順3Bにpackする。GR/GL/C/Headset/予約bitは落とす
// (Switch 1輸送に位置なし)。SR/SL位置はProConにないため常に0。
void ctrl_pack_btn3(const ctrl_state_t *st, uint8_t out3[3]);

// 8bitスティック → 12bit pack (x<<4、y=4096-(y<<4)を4095 clamp)。
void pack_stick_12bit(uint8_t x8, uint8_t y8, uint8_t out3[3]);
