// poc_usb.h -- PoC用 TinyUSB stubの公開IF (Task 3で src/usb/* に置換)。
// main.c (BTstack側) が tusb.h を直接includeしないための分離層。
// 背景: btstack_hid.h と TinyUSB hid.h が同一TUで hid_report_type_t を
// 二重定義する。wakeconもTU分離 (main.c/BTstack側とusb_wired.c/USB側) で
// 回避している。最終FWも同構造 (src/usb/*) になる。
#pragma once

#include <stdbool.h>
#include <stdint.h>

void poc_usb_init(void);
void poc_usb_pump(void); // tud_task() 相当。Core0の1ms timerから呼ぶ。
bool poc_usb_mounted(void);
bool poc_usb_send_neutral(void); // IN経路生存確認の中立report。送れたらtrue。
