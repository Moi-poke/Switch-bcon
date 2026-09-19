// TinyUSB設定 (switch-bcon src/usb の正式版。PoCの暫定コピーから昇格)。
/* 参考: pico-examples usb/device/dev_hid_composite 相当を最小化したもの。
 * 有線 Pro Controller は HID インタフェース1つのみ出す (CDC/MSC/MIDI/VENDOR なし)。
 * ビルド側で CFG_TUSB_OS=OPT_OS_PICO を定義すること (CMakeLists 参照)。 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ボード固有設定。pico-sdk の tinyusb_board が実体を持つ。 */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#endif

/* 共通設定。CFG_TUSB_MCU は SDK 側が出す。 */
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

/* Device スタックのみ使う。 */
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

/* EP0 64B (純正 bMaxPacketSize0 と一致)。 */
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

/* HID のみ有効化。 */
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 1
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

/* HID バッファは Report ID + 63B を収める 64B (純正 EP サイズと一致)。 */
#define CFG_TUD_HID_EP_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
