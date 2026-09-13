/* 移植元: pico-wakecon src/usb_descriptors.c (バイト列同一)。
 * 有線 Pro Controller の USB 記述子 (TinyUSB glue)。
 * バイト列は ToadKing の純正ダンプの写し。BT 側の記述子とは別物。
 * bcdDevice 0x0200 は実測未確認 (0x0210 説あり。ドック検証で確定する)。 */

#include <string.h>

#include "tusb.h"

/* Device: USB 2.00, EP0 64B, VID 0x057E / PID 0x2009。
 * bcdDevice は ToadKing の線上のバイト 0x00,0x02 (LE) = 0x0200 を採る。
 * 実測未確認: 0x0210 説あり、ドック検証で確定。 */
static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x057E,
    .idProduct = 0x2009,
    .bcdDevice = 0x0200, /* 実測未確認: 0x0210 説あり、ドック検証で確定 */
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* HID Report Descriptor (203B。ToadKing 原文の写し)。
 * 入力 ID 0x30、入力 0x21/0x81、出力 0x01/0x10/0x80/0x82。 */
static uint8_t const desc_hid_report[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x15, 0x00,       // Logical Minimum (0)
    0x09, 0x04,       // Usage (Joystick)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x30,       //   Report ID (48)
    0x05, 0x01,       //   Usage Page (Generic Desktop Ctrls)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (0x01)
    0x29, 0x0A,       //   Usage Maximum (0x0A)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x0A,       //   Report Count (10)
    0x55, 0x00,       //   Unit Exponent (0)
    0x65, 0x00,       //   Unit (None)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x0B,       //   Usage Minimum (0x0B)
    0x29, 0x0E,       //   Usage Maximum (0x0E)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x03,       //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x0B, 0x01, 0x00, 0x01, 0x00, //   Usage (0x010001)
    0xA1, 0x00,       //   Collection (Physical)
    0x0B, 0x30, 0x00, 0x01, 0x00, //     Usage (0x010030)
    0x0B, 0x31, 0x00, 0x01, 0x00, //     Usage (0x010031)
    0x0B, 0x32, 0x00, 0x01, 0x00, //     Usage (0x010032)
    0x0B, 0x35, 0x00, 0x01, 0x00, //     Usage (0x010035)
    0x15, 0x00,       //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     Logical Maximum (65534)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x04,       //     Report Count (4)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             //   End Collection
    0x0B, 0x39, 0x00, 0x01, 0x00, //   Usage (0x010039)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (System: English Rotation, Length: Centimeter)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x0F,       //   Usage Minimum (0x0F)
    0x29, 0x12,       //   Usage Maximum (0x12)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x34,       //   Report Count (52)
    0x81, 0x03,       //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
    0x85, 0x21,       //   Report ID (33)
    0x09, 0x01,       //   Usage (0x01)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x85, 0x81,       //   Report ID (-127)
    0x09, 0x02,       //   Usage (0x02)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x85, 0x01,       //   Report ID (1)
    0x09, 0x03,       //   Usage (0x03)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0x85, 0x10,       //   Report ID (16)
    0x09, 0x04,       //   Usage (0x04)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0x85, 0x80,       //   Report ID (-128)
    0x09, 0x05,       //   Usage (0x05)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0x85, 0x82,       //   Report ID (-126)
    0x09, 0x06,       //   Usage (0x06)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0xC0,             // End Collection
};

/* 純正 wDescriptorLength 203 との一致をビルドで縛る。 */
_Static_assert(sizeof(desc_hid_report) == 203u, "wired HID report must be 203B");

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

/* Configuration 一式 (wTotalLength 41)。
 * 純正通り: Remote Wakeup、500mA、HID 1IF、IN 0x81 / OUT 0x01 (各64B・間隔8)。 */
enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

#define WIRED_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, WIRED_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(desc_hid_report), 0x01, 0x80 | 0x01, 64, 8),
};

/* 純正 wTotalLength 41 との一致をビルドで縛る。 */
_Static_assert(sizeof(desc_configuration) == 41u, "wired config must be 41B");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* 文字列 (純正の写し。シリアルは純正固定値)。 */
enum {
    WIRED_STRID_LANGID = 0,
    WIRED_STRID_MANUFACTURER,
    WIRED_STRID_PRODUCT,
    WIRED_STRID_SERIAL,
};

static char const *const wired_string_desc_arr[] = {
    (const char[]){0x09, 0x04}, /* 0: 英語 (0x0409) */
    "Nintendo Co., Ltd",        /* 1: 製造者 */
    "Pro Controller",           /* 2: 製品名 */
    "000000000001",             /* 3: シリアル (純正固定値) */
};

static uint16_t wired_desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    size_t chr_count;
    size_t max_count;
    size_t i;
    char const *str;
    (void)langid;

    if (index == WIRED_STRID_LANGID) {
        memcpy(&wired_desc_str[1], wired_string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= (sizeof(wired_string_desc_arr) / sizeof(wired_string_desc_arr[0]))) {
            return NULL;
        }
        str = wired_string_desc_arr[index];
        chr_count = strlen(str);
        max_count = (sizeof(wired_desc_str) / sizeof(wired_desc_str[0])) - 1u;
        if (chr_count > max_count) {
            chr_count = max_count;
        }
        for (i = 0; i < chr_count; i++) {
            wired_desc_str[1 + i] = (uint16_t)(unsigned char)str[i];
        }
    }
    wired_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * chr_count + 2u));
    return wired_desc_str;
}

/* GET_REPORT 要求が来た実測はないため 0 (STALL) のまま。
 * でたらめ値は返さない。観測されたら usb_wired 側で持つ。 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

/* OUT EP (80 xx ハンドシェイク) の受口は usb_wired.c にある。
 * 二重定義のリンクエラーを避けるためここには置かない。 */
