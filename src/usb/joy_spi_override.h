// joy_spi_override.h -- 実Joy-Con RのSPI応答をそのまま返す診断用。
//
// 実機の 0x6020/0x6080/0x8026 を採取したら、このファイルに値を書いて
// JOY_SPI_HAVE_OVERRIDE を 1 にし、JOY_SPI_CAL_ROT と組み合わせて
// 「純正のSPI＋純正の生値」で再生試験を行う。
//
// 採取は tools/spi_dump.py（USB HID経由）で行う。
// 個体識別情報（0x6000のMAC等）はここに入れない。較正ブロックのみ。
#pragma once

#define JOY_SPI_HAVE_OVERRIDE 0

#if JOY_SPI_HAVE_OVERRIDE
static const uint8_t JOY_SPI_6020_OVR[24] = {
    0
};
static const uint8_t JOY_SPI_6080_OVR[24] = {
    0
};
static const uint8_t JOY_SPI_8026_OVR[26] = {
    0
};
#endif
