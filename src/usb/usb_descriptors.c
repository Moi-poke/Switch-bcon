/* 移植元: pico-wakecon src/usb_descriptors.c (バイト列同一)。
 * 有線 Pro Controller の USB 記述子 (TinyUSB glue)。
 * バイト列は ToadKing の純正ダンプの写し。BT 側の記述子とは別物。
 * bcdDevice 0x0200 は実測未確認 (0x0210 説あり。ドック検証で確定する)。 */

#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"

#include "usb_hid.h"
#include "usb_cdc.h" // profile参照 (dep-free header。tusb/btstack衝突なし)

/* USB profileは実行時選択 (単一UF2。usb_cdc_set_profileで起動時1回確定)。 */

/* 無線(CDC) profileのUSB ID。有線HIDは純正写し (057E:2009) のまま。
 * 同一PCで有線HIDと無線CDCが同じUSBインスタンスIDになると、Windowsの
 * デバイス/ドライバキャッシュが衝突してCOMポートが出ないため別IDにする。 */
#define CDC_PROFILE_VID 0x2E8Au /* Raspberry Pi */
#define CDC_PROFILE_PID 0x000Au

/* Device: USB 2.00, EP0 64B, VID 0x057E / PID は Plan A で全 role 0x2009 固定
 * (USB_PID_BY_ROLE 全要素 0x2009。PERSONALITY_TABLE の BT 側 2006/2007 は
 * 無線用プレースホルダで USB 列挙には使わない)。
 * bcdDevice は ToadKing の線上のバイト 0x00,0x02 (LE) = 0x0200 を採る。
 * 実測未確認: 0x0210 説あり、ドック検証で確定。
 * 記述子は全 role 同一 (VID・PID・bcd・EP・間隔・構成は不変)。
 * desc_device を const にしない理由: 歴史的に PID を起動時 role で選んでいた
 * 名残 (現状は固定値の再代入。USB init/列挙より先に確定)。 */
static tusb_desc_device_t desc_device = {
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
    /* 有線=従来の純正写し (Plan A: 全role 0x2009)。無線=PC専用CDCの別ID。
     * 文字列も profile で切り替える (下の string cb)。tud_init前に確定済み。 */
    if (!usb_cdc_is_wired_profile()) {
        desc_device.idVendor = CDC_PROFILE_VID;
        desc_device.idProduct = CDC_PROFILE_PID;
        desc_device.bDeviceClass = TUSB_CLASS_MISC;
        desc_device.bDeviceSubClass = MISC_SUBCLASS_COMMON;
        desc_device.bDeviceProtocol = MISC_PROTOCOL_IAD;
    } else {
        desc_device.idVendor = 0x057Eu;
        desc_device.idProduct = usb_pid_for_role(usb_get_role());
        desc_device.bDeviceClass = 0x00;
        desc_device.bDeviceSubClass = 0x00;
        desc_device.bDeviceProtocol = 0x00;
    }
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

/* Keyboard Report Descriptor (69B。HID-Project BootKeyboard 互換)。
 * 出典: NicoHood/HID `src/SingleReport/BootKeyboard.cpp` の
 * `_hidReportDescriptorKeyboard`。Arduino 標準 Keyboard（レポートID 2・
 * LED出力なし）は Switch が無反応だったが、この BootKeyboard 記述子で
 * 入力できた実績がある（2021-01 Reddit / r/arduino）。
 * 8B入力: modifiers(8bit)・consumer予約(8bit)・keycode x6(8bit, usage 0x00-0xE7)。
 * 1B出力: LED 8bit。OUT EPは持たず、LEDは control SET_REPORT で受ける。
 * レポートIDなし。interface は report protocol (subclass/protocol 0) で、
 * ホストはこの記述子を解析してキーボードとして扱う。
 * 有線profileの2番目のHIDインスタンス (interface 1 / IN 0x82) が使う。 */
static uint8_t const desc_kb_report[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (0xE0)
    0x29, 0xE7,       //   Usage Maximum (0xE7)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs) modifiers
    0x05, 0x0C,       //   Usage Page (Consumer)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0xFF,       //   Usage Maximum (255)
    0x81, 0x00,       //   Input (Data,Array,Abs) consumer reserved byte
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x08,       //   Usage Maximum (Kana + 3 custom)
    0x95, 0x08,       //   Report Count (8)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x02,       //   Output (Data,Var,Abs) LED report
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xE7, 0x00, //   Logical Maximum (231)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0xE7,       //   Usage Maximum (231)
    0x81, 0x00,       //   Input (Data,Array,Abs) keycodes
    0xC0,             // End Collection
};

_Static_assert(sizeof(desc_kb_report) == 69u, "keyboard HID report must be 69B");

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    if (instance == 1u) {
        return desc_kb_report;
    }
    return desc_hid_report;
}

/* Configuration 一式はprofileで選択 (実行時)。両配列を保持しcbで返す。
 * 有線は1 device 2 interface: itf0=ProCon (純正41B分のIF+EPはバイト同一)、
 * itf1=HID keyboard (report protocol・IN 0x82・8B・IDなし)。wTotalLength 66B。
 * 無線CDCは2系統: itf0/1=data (v3 frame) + itf2/3=log (probe_line写し)。
 * EP: data (notif 0x83 / out 0x02 / in 0x82)、log (notif 0x84 / out 0x03 / in 0x85)
 * はHID (0x81/0x01/0x82) と非衝突 (profile排他のため同時には存在しない)。 */
enum {
    ITF_NUM_CDC_DATA,
    ITF_NUM_CDC_DATA_EP,
    ITF_NUM_CDC_LOG,
    ITF_NUM_CDC_LOG_EP,
    ITF_NUM_CDC_TOTAL,
};

#define CDC_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN * 2u)

static uint8_t const desc_configuration_cdc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_CDC_TOTAL, 0, CDC_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    /* data: v3 frame (PCのプロトコル用) */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_DATA, 0, 0x83, 8, 0x02, 0x82, 64),
    /* log: probe_lineのASCII写し */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_LOG, 0, 0x84, 8, 0x03, 0x85, 64),
};

/* CDC-only wTotalLength (9 + 66*2) との一致をビルドで縛る。 */
_Static_assert(sizeof(desc_configuration_cdc) ==
                   (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN * 2u),
               "cdc config must be 9+66*2B");
/* Configuration 一式 (wTotalLength 66B)。
 * 純正ProCon部分 (IF0・Remote Wakeup・500mA・IN 0x81/OUT 0x01 各64B・間隔8) は
 * バイト同一のまま、キーボードIF (IF1・IN 0x82・8B・間隔8) を末尾に追加する。 */
enum {
    ITF_NUM_HID,
    ITF_NUM_KB,
    ITF_NUM_HID_TOTAL,
};

#define WIRED_CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const desc_configuration_hid[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_HID_TOTAL, 0, WIRED_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(desc_hid_report), 0x01, 0x80 | 0x01, 64, 8),
    TUD_HID_DESCRIPTOR(ITF_NUM_KB, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_kb_report), 0x82, 8, 8),
};

/* 純正41B + keyboard 25B との一致をビルドで縛る。 */
_Static_assert(sizeof(desc_configuration_hid) == 66u,
               "wired config must be 41+25B");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    if (!usb_cdc_is_wired_profile()) {
        return desc_configuration_cdc;
    }
    return desc_configuration_hid;
}

/* 文字列 (純正の写し。シリアルは純正固定値)。
 * Plan A: 製品名も全 role "Pro Controller" 固定 (usb_product_for_role は
 * 全 role 同値を返す。BT 側 PERSONALITY_TABLE の Joy-Con 文字列は無線用)。 */
enum {
    WIRED_STRID_LANGID = 0,
    WIRED_STRID_MANUFACTURER,
    WIRED_STRID_PRODUCT,
    WIRED_STRID_SERIAL,
};

static char const *const wired_string_desc_arr[] = {
    (const char[]){0x09, 0x04}, /* 0: 英語 (0x0409) */
    "Nintendo Co., Ltd",        /* 1: 製造者 */
    "Pro Controller",           /* 2: 製品名 (Plan A で全 role 固定) */
    "000000000001",             /* 3: シリアル (純正固定値) */
};

static uint16_t wired_desc_str[32 + 1];

/* 無線(CDC) profileのシリアル。WindowsのデバイスインスタンスIDを基板ごとに
 * 分ける (有線HIDの純正固定値 "000000000001" は不変)。 */
static char s_cdc_serial[13];

static void cdc_serial_init(void)
{
    pico_unique_board_id_t id;
    if (s_cdc_serial[0] != '\0') {
        return;
    }
    pico_get_unique_board_id(&id);
    snprintf(s_cdc_serial, sizeof(s_cdc_serial), "BCON%02X%02X%02X%02X",
             (unsigned)id.id[0], (unsigned)id.id[1],
             (unsigned)id.id[2], (unsigned)id.id[3]);
}

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
        if (!usb_cdc_is_wired_profile()) {
            if (index == WIRED_STRID_MANUFACTURER) {
                str = "Raspberry Pi";
            } else if (index == WIRED_STRID_PRODUCT) {
                str = "switch-bcon CDC";
            } else if (index == WIRED_STRID_SERIAL) {
                cdc_serial_init();
                str = s_cdc_serial;
            }
        } else if (index == WIRED_STRID_PRODUCT) {
            str = usb_product_for_role(usb_get_role());
        }
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

/* GET_REPORT: ProCon IFは実測要求がないためSTALLのまま。
 * キーボードIFは report protocol のため、ホストが初期状態を問う場合に
 * 現在の8Bを返す（返さないとSTALLでキーボードを諦めるホストがある）。 */
void probe_line(const char *s);
extern uint8_t bcon_kb[8]; /* 所有元main.c。キーボード最終状態8B */

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    if (instance == 1u) {
        if (report_type == HID_REPORT_TYPE_INPUT && report_id == 0u &&
            buffer != NULL && reqlen >= 8u) {
            memcpy(buffer, bcon_kb, 8u);
            buffer[1] = 0u;
            return 8u;
        }
        if (report_type == HID_REPORT_TYPE_OUTPUT && report_id == 0u &&
            buffer != NULL && reqlen >= 1u) {
            buffer[0] = 0u; /* LED状態は保持しないため0 */
            return 1u;
        }
        return 0; /* featureは扱わない */
    }
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    probe_line("UGET");
    return 0;
}

/* OUT EP (80 xx ハンドシェイク) の受口は usb_wired.c にある。
 * 二重定義のリンクエラーを避けるためここには置かない。 */
