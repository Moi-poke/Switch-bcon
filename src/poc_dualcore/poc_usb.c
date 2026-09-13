// poc_usb.c -- PoC用 TinyUSB stub (汎用boot keyboard)。
// このTUでは tusb.h のみをincludeし、btstack.h を絶対に含めない
// (hid_report_type_t 二重定義の回避。poc_usb.h 参照)。
//
// USB注意: VID 0xCAFE・汎用キーボード・"not a gamepad"。
// 公式ProCon写しは Task 3 の仕事。Switchに挿してもProCon動作はしない。

#include "tusb.h"
#include "poc_usb.h"

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

static uint8_t const poc_desc_device[] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
    0xFE, 0xCA, 0x01, 0x40, 0x00, 0x01, 0x01, 0x02,
    0x00, 0x01,
};

static uint8_t const poc_hid_report[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x81, 0x03, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
};

#define POC_CFG_TOTAL (9 + 9 + 9 + 7)
static uint8_t const poc_desc_config[] = {
    0x09, 0x02, (POC_CFG_TOTAL & 0xFF), 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(poc_hid_report), 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x08,
};

static const char *poc_strings[] = {
    "pico-bcon", "PoC stub (not a gamepad)", "0001",
};

uint8_t const *tud_descriptor_device_cb(void) { return poc_desc_device; }

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return poc_desc_config;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t buf[32];
    static const uint16_t lang[] = { 0x0304, 0x0409 };
    (void)langid;
    if (index == 0) return lang;
    if (index >= 1 && index <= 3) {
        const char *s = poc_strings[index - 1];
        size_t n = 0;
        while (s[n] && n < 31) n++;
        buf[0] = (uint16_t)(0x0300 | ((n * 2 + 2) & 0xFF));
        for (size_t i = 0; i < n; i++) buf[1 + i] = (uint16_t)s[i];
        return buf;
    }
    return NULL;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return poc_hid_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}

void poc_usb_init(void) {
    tud_init(BOARD_TUD_RHPORT);
}

void poc_usb_pump(void) {
    tud_task();
}

bool poc_usb_mounted(void) {
    return tud_mounted();
}

bool poc_usb_send_neutral(void) {
    if (!tud_mounted() || !tud_hid_ready()) return false;
    return tud_hid_keyboard_report(0, 0, NULL);
}
