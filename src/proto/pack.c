// pack.c -- 写像表の実装 (spec §11)。分岐のみ・副作用なし。
#include "pack.h"

void ctrl_pack_btn3(const ctrl_state_t *st, uint8_t out3[3]) {
    uint32_t b = (st != NULL) ? st->buttons : 0u;
    uint8_t b0 = 0u, b1 = 0u, b2 = 0u;
    if (b & BTN_Y)       b0 |= 0x01u;
    if (b & BTN_X)       b0 |= 0x02u;
    if (b & BTN_B)       b0 |= 0x04u;
    if (b & BTN_A)       b0 |= 0x08u;
    if (b & BTN_R)       b0 |= 0x40u;
    if (b & BTN_ZR)      b0 |= 0x80u;
    if (b & BTN_MINUS)   b1 |= 0x01u;
    if (b & BTN_PLUS)    b1 |= 0x02u;
    if (b & BTN_RSTICK)  b1 |= 0x04u;
    if (b & BTN_LSTICK)  b1 |= 0x08u;
    if (b & BTN_HOME)    b1 |= 0x10u;
    if (b & BTN_CAPTURE) b1 |= 0x20u;
    if (b & BTN_DOWN)    b2 |= 0x01u;
    if (b & BTN_UP)      b2 |= 0x02u;
    if (b & BTN_RIGHT)   b2 |= 0x04u;
    if (b & BTN_LEFT)    b2 |= 0x08u;
    if (b & BTN_L)       b2 |= 0x40u;
    if (b & BTN_ZL)      b2 |= 0x80u;
    // BTN_GR/GL/C/HEADSET・予約bitは輸送位置なしのため落とす。
    out3[0] = b0;
    out3[1] = b1;
    out3[2] = b2;
}

void pack_stick_12bit(uint16_t x12, uint16_t y12, uint8_t out3[3]) {
    uint16_t x = x12 & 0x0FFFu;
    uint16_t y = (uint16_t)(4096u - (y12 & 0x0FFFu));
    if (y > 4095u) y = 4095u;
    out3[0] = (uint8_t)(x & 0xFFu);
    out3[1] = (uint8_t)(((x >> 8) & 0x0Fu) | ((y & 0x0Fu) << 4));
    out3[2] = (uint8_t)((y >> 4) & 0xFFu);
}
