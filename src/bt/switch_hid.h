#ifndef SWITCH_HID_H
#define SWITCH_HID_H

// 移植元: pico-wakecon src/switch_hid.h (バイト列・MIT表示とも継承)。
// 以下原文のまま。

// ==========================================================================
// Pro Controller が Bluetooth で名乗る HID レポート記述子と定数
//
// 記述子のバイト列は DavidPagels/retro-pico-switch の SwitchConsts.h から
// 写したものである（MIT ライセンス）。これは「バイトの並び」という仕様
// であり、Switch が受け付ける形は 1 通りしかない。書き換える余地が
// 無いので、そのまま写す。中身の意味を全部は使わない。SDP と
// hid_device_init へ渡すために要るだけである。
//
// MIT ライセンスの条件により、元の著作権表示と許諾文を残す:
//
//   MIT License
//   Copyright (c) 2023 David Pagels
//
//   Permission is hereby granted, free of charge, to any person obtaining
//   a copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be
//   included in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Report ID の一覧
//   0x21 Input  48 byte … サブコマンドへの応答
//   0x30 Input  48 byte … 通常の入力レポート（姿勢を送る本体）
//   0x31 Input 361 byte … NFC / IR
//   0x32 Input 361 byte … 同上
//   0x33 Input 361 byte … 同上
//   0x3F Input          … 簡易モード（ボタン 16 + Hat + スティック 4x16）
//   0x01 Output 48 byte … Switch → コントローラ（サブコマンド）
//   0x10 Output  9 byte … 振動
//   0x11 Output 48 byte … NFC / IR
//   0x12 Output 48 byte … NFC / IR
// ==========================================================================
#include "pico/stdlib.h"

// --------------------------------------------------------------------------
// Pro Controller の識別情報
//   VID / PID は SDP の PnP レコードで名乗る。
//   Switch はこれを見て「何者か」を判断する。
// --------------------------------------------------------------------------
#define SWITCH_VENDOR_ID       0x057E   // 任天堂
#define SWITCH_PRODUCT_ID      0x2009   // Pro Controller
#define SWITCH_PRODUCT_VERSION 0x0001

// Class of Device: 周辺機器 / ゲームパッド。
#define SWITCH_CLASS_OF_DEVICE 0x2508

// BD_ADDR の先頭 3 バイト（OUI）。
//   Pico 自身のアドレス（88:A2:9E:...）では Switch が拾わなかった。
//   残り 3 バイトは乱数にする。同じ Switch へ 2 台繋ぐときに
//   衝突しないようにするためである。
#define SWITCH_OUI_0 0x7c
#define SWITCH_OUI_1 0xbb
#define SWITCH_OUI_2 0x8a

// 名乗る名前。GAP と SDP で違う名前を使う（実物がそうなっている）。
#define SWITCH_GAP_NAME  "Pro Controller"
#define SWITCH_HID_NAME  "Wireless Gamepad"

// --------------------------------------------------------------------------
// HID レポート記述子
//   これを SDP と hid_device_init の両方へ渡す。
// --------------------------------------------------------------------------
static const uint8_t switch_bt_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x06, 0x01, 0xFF,  //   Usage Page (Vendor Defined 0xFF01)

    0x85, 0x21,  //   Report ID (33)
    0x09, 0x21,  //   Usage (0x21)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x30,  //   Report Count (48)
    0x81, 0x02,  //   Input

    0x85, 0x30,  //   Report ID (48)
    0x09, 0x30,  //   Usage (0x30)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x30,  //   Report Count (48)
    0x81, 0x02,  //   Input

    0x85, 0x31,        //   Report ID (49)
    0x09, 0x31,        //   Usage (0x31)
    0x75, 0x08,        //   Report Size (8)
    0x96, 0x69, 0x01,  //   Report Count (361)
    0x81, 0x02,        //   Input

    0x85, 0x32,        //   Report ID (50)
    0x09, 0x32,        //   Usage (0x32)
    0x75, 0x08,        //   Report Size (8)
    0x96, 0x69, 0x01,  //   Report Count (361)
    0x81, 0x02,        //   Input

    0x85, 0x33,        //   Report ID (51)
    0x09, 0x33,        //   Usage (0x33)
    0x75, 0x08,        //   Report Size (8)
    0x96, 0x69, 0x01,  //   Report Count (361)
    0x81, 0x02,        //   Input

    0x85, 0x3F,  //   Report ID (63)
    0x05, 0x09,  //   Usage Page (Button)
    0x19, 0x01,  //   Usage Minimum (0x01)
    0x29, 0x10,  //   Usage Maximum (0x10)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x10,  //   Report Count (16)
    0x81, 0x02,  //   Input
    0x05, 0x01,  //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x39,  //   Usage (Hat switch)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x07,  //   Logical Maximum (7)
    0x75, 0x04,  //   Report Size (4)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x42,  //   Input (Null State)
    0x05, 0x09,  //   Usage Page (Button)
    0x75, 0x04,  //   Report Size (4)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x01,  //   Input (Const)
    0x05, 0x01,  //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,  //   Usage (X)
    0x09, 0x31,  //   Usage (Y)
    0x09, 0x33,  //   Usage (Rx)
    0x09, 0x34,  //   Usage (Ry)
    0x16, 0x00, 0x00,              //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  //   Logical Maximum (65534)
    0x75, 0x10,                    //   Report Size (16)
    0x95, 0x04,                    //   Report Count (4)
    0x81, 0x02,                    //   Input
    0x06, 0x01, 0xFF,              //   Usage Page (Vendor Defined 0xFF01)

    0x85, 0x01,  //   Report ID (1)
    0x09, 0x01,  //   Usage (0x01)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x30,  //   Report Count (48)
    0x91, 0x02,  //   Output

    0x85, 0x10,  //   Report ID (16)
    0x09, 0x10,  //   Usage (0x10)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x09,  //   Report Count (9)
    0x91, 0x02,  //   Output

    0x85, 0x11,  //   Report ID (17)
    0x09, 0x11,  //   Usage (0x11)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x30,  //   Report Count (48)
    0x91, 0x02,  //   Output

    0x85, 0x12,  //   Report ID (18)
    0x09, 0x12,  //   Usage (0x12)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x30,  //   Report Count (48)
    0x91, 0x02,  //   Output
    0xC0,        // End Collection
};

#endif  // SWITCH_HID_H
