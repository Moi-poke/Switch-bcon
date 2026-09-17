# PC ⇄ Pico バイナリプロトコル仕様書 (v3 / PROTO_VER=3)

**プロジェクト:** pico-bcon（新規作成。旧 pokecon v2・wakecon ASCII と非互換）
**対象経路:** PC →(UART)→ Pico 2 W →(USB-HID / Classic BT)→ Switch 1
**対象外:** Switch 2 BLE入力エミュレーション（将来課題）
**設計目標:** 最速（変換＋UART＋ファーム ≈ 2ms級）／高信頼（SYNC＋CRC8＋スライディング再同期）／公式プロコン認識

## 改訂履歴

| Ver | 日付 | 変更内容 |
|-----|------|---------|
| 3.0 | 2026-09-13 | 初版（本リポジトリ）。STATE=ボタンu32-LE（VIIPER順）LEN8・HAT廃止／CONFIG面 0x30-0x35 新設／RUMBLE 0x22 予約／STATUS 7B・HELLO_ACK 4B維持／USBは任天堂写し・bInterval 8 |

> フィールド追加・意味変更時は必ず本表と `PROTO_VER` を更新する。

## 1. 設計原則

1. 固定長バイナリ。既知型は正確長を強制し定数時間パース。
2. SYNC（0xAB）＋CRC-8/SMBUS＋スライディング再同期（失敗時は先頭1B前進）。
3. 全状態送信。1フレーム欠落は次フレームで完全復帰。
4. 線路ボタンは輸送非依存の論理u32。USB順／BLE順／Classic順の差はPico側pack関数で吸収。
5. 双方向・方向独立SEQ（PC→Pico と Pico→PC は別カウンタ、mod256）。
6. UARTはバイナリ専用。ASCIIコマンドは存在しない。

## 2. 物理層

| 項目 | 値 | 備考 |
|------|-----|------|
| ボーレート | 既定 1Mbps（2Mbps可） | 8N1、フロー制御なし既定 |
| ピン | 既定 GP4/5（UART1）。ビルド時GP0/1選択可 | ジャンパ検出は将来 |
| 変換チップ | FTDI推奨・latency timer 1ms。CH340は1Mbps以下推奨 | — |
| バイトオーダ | リトルエンディアン | u16/u32に適用 |

帯域：STATE=13B（SYNC込）。1kHz送信で約13KB/s（リンク使用率約13%）。

## 3. フレーム構造

合計 = `N + 5`（`N`=ペイロード長、上限32）。

```
Offset  Field    Size  説明
  0     SYNC      1    固定 0xAB
  1     TYPE      1    種別（第4章）
  2     LEN       1    ペイロード長（既知型は正確長を強制）
  3     PAYLOAD   N    TYPE依存（第5章）
 3+N    SEQ       1    方向独立・mod256（送信ごとに +1）
 4+N    CRC8      1    CRC-8/SMBUS、対象 TYPE..SEQ（SYNC除外）
```

* SYNCはペイロード中にも出現し得る。最終判定は必ずCRC。
* 既知型のLEN不一致は破棄＋先頭1B前進。未知型は32B上限で可変スキップ（前方互換）。
* SEQ欠番はmod256で検出。`expect=(last+1)&0xFF`。不一致は欠落イベントとして1加算（欠落数ではない）。初回フレームは計数しない。

## 4. フレーム種別

| TYPE | 名称 | 方向 | LEN | 用途 |
|------|------|------|-----|------|
| 0x01 | STATE | PC→Pico | 8 | 全状態（ホットパス） |
| 0x02 | NEUTRAL | PC→Pico | 0 | 全入力解放（安全停止） |
| 0x03 | PING | PC→Pico | 0 | 生存確認／RTT |
| 0x10 | HELLO | PC→Pico | 2 | 版通知・FLAGS |
| 0x11 | HELLO_ACK | Pico→PC | 4 | 版応答・RESULT |
| 0x20 | STATUS | Pico→PC | 7 | 状態・統計・errcode |
| 0x21 | PONG | Pico→PC | 1 | PING自身seqをエコー |
| 0x22 | RUMBLE | Pico→PC | 2 | 振動振幅（予約・v1未送出） |
| 0x30 | CAPTURE_START | PC→Pico | 1 | wake取込開始・秒数1-60 |
| 0x31 | BEACON_START | PC→Pico | 0 | wake再生（約1.5s） |
| 0x32 | COLOR_SET | PC→Pico | 12 | 本体色RGB×4 |
| 0x33 | KEY_DELETE | PC→Pico | 0 | Classicリンク鍵全削除 |
| 0x34 | WIRED_MODE | PC→Pico | 1 | 0=無線・1=有線（Flash保存） |
| 0x35 | STATUS_REQ | PC→Pico | 0 | STATUS即時返送要求 |
| 0x36 | BAUD_SET | PC→Pico | 1 | rate index（B §3・合意切替） |
| 0x37 | BOOTSEL | PC→Pico | 1 | 開発用：magic 0x5AでUSB BOOTSEL再起動 |
| 0x36 | BAUD_SET | PC→Pico | 1 | rate index（B §3・合意切替） |
| 0x37 | BOOTSEL | PC→Pico | 1 | 開発用：magic 0x5AでUSB BOOTSEL再起動 |

## 5. ペイロード定義

### 5.1 STATE (LEN=8 / LEN=12)

```
Off  Field  Size  説明
 0   BTN     4    u32-LE（VIIPER互換ビット順、下表）。22bit使用・残り予約0
 4   LX      1    0-255・0x80中央
 5   LY      1    同上（Y反転はPC側で単一定義）
 6   RX      1    同上
 7   RY      1    同上
```

BTNビット（1=押下）：

| bit | ボタン | bit | ボタン |
|-----|--------|-----|--------|
| 0 | B | 12 | L |
| 1 | A | 13 | ZL |
| 2 | Y | 14 | Minus(-) |
| 3 | X | 15 | L押込 |
| 4 | R | 16 | Home |
| 5 | ZR | 17 | Capture |
| 6 | Plus(+) | 18 | GR（Switch 2、Switch 1では無視） |
| 7 | R押込 | 19 | GL（同上） |
| 8 | Down | 20 | C（同上） |
| 9 | Right | 21 | Headset（同上） |
| 10 | Left | 22-31 | 予約（0固定） |
| 11 | Up | | |

* 十字キーはボタン（斜めは2bit同時）。HATフィールドなし。
* 予約bitは0固定で送信。受信側は予約bitを無視する（将来拡張のため拒否しない）。
* スティックY反転・12bit化等の座標変換はPC送信ラッパ1箇所に集約。Picoは8bit値をそのまま保持し、輸送pack時に12bit化する。

LEN=12形式（12bitスティック拡張）：BTN 4BはLEN=8と同一。続く8BがLX・LY・RX・RYの
u16LE（0-4095、中央0x0800）。Picoは12bit域で保持し、Y反転（4096-Y・4095 clamp）は
pack時にLEN=8と同一式で適用する。LEN=8受信値は取込時に<<4して12bit化するため、
0x80は0x800と等価。parser/dispatchは8/12のみ受理し、他のLENはERR_BAD_LEN。

### 5.2 NEUTRAL (LEN=0)

BTN=0・全スティック0x80を即時適用。STATE全中立と等価の短縮形。

### 5.3 PING (LEN=0) / PONG (LEN=1)

PONG payload = 受信PINGのヘッダSEQをエコー（RTT対応を一意に）。

### 5.4 HELLO (LEN=2) / HELLO_ACK (LEN=4)

HELLO：`[0]=PROTO_VER（要求版=3)、[1]=FLAGS（bit0=STATUS自動送信要求、他予約0）`。
HELLO_ACK：`[0]=採用版、[1]=FW_MAJOR、[2]=FW_MINOR、[3]=RESULT`。
RESULT：`0x00 OK／0x01 DOWNGRADED（要求が新しく自版で継続）／0x02 UNSUPPORTED（互換なし・中立維持・STATE拒否）`。
Picoはv3のみ話す。HELLO前のSTATUS自動送信はしない。FLAGS bit0指定時のみ1Hz周期送信。

### 5.5 STATUS (LEN=7)

```
Off  Field        Size  説明
  0   STATE_FLAGS   1    bit0 USB mounted／bit1 Switch ready／bit2 timeout-neutral中
                         bit3 WDT recovered／bit4 UART overrun／bit5 wired mode／bit6 BT connected
                         bit7 RUMBLE受信あり (前回STATUS以降にSwitch振動出力を受信)
 1   LAST_SEQ      1    直近受理PC→Pico seq
 2   ERR_CRC       2    CRC/形式リジェクト累計（u16 LE、飽和）
 4   ERR_DROP      2    SEQ欠番イベント累計（mod256、飽和）
 6   ERRCODE       1    直近エラー理由（0=正常。下表）
```

ERRCODE：`0x00 正常／0x01 LEN不正／0x02 CRC不一致／0x03 SEQ欠番／0x04 UNSUPPORTED版／0x05 UART overrun／0x06 parser overflow／0x10-0x1F CONFIG拒否（0x10+TYPE下位）`。
周期送信時のerrcodeは直近エラーを保持（正常復帰後に0へ戻す）。

### 5.6 CONFIG（0x30-0x35）

* CAPTURE_START：`[0]=秒数1-60`。範囲外は拒否（ERRCODE 0x10）。
* BEACON_START：LEN0。未保存時は拒否（0x11）。
* COLOR_SET：`[0..11]=RGB×4（本体・ボタン・左・右）`。有線中は再列挙して読み直させる。
* KEY_DELETE：LEN0。Classic鍵全削除（Switch側登録解除も要案内）。
* WIRED_MODE：`[0]=0/1`。範囲外拒否（0x14）。Flash保存・起動時復元。
  切替は再起動で適用する (CYW43/BTstackの有無が起動時確定のため。
  受理後約500msで自発再起動する)。有線起動では無線一式を上げない
  (CYW43給電中はSwitch 2ドックがUSB列挙しない実測のため)。
  取込・再生 (CAPTURE/BEACON) は無線起動でのみ有効。有線中の要求は
  ERRCODE `0x10`/`0x11` で拒否する (先にWIRED_MODE=0＋再起動が必要)。
* STATUS_REQ：LEN0。即時STATUS返送（errcode=0x00）。
* BAUD_SET：`[0]=rate index`。範囲外は拒否（0x16）。受理後は旧rateでSTATUS ACK→guard後に合意切替。
* BOOTSEL：`[0]=0x5A`（magic）。開発用・単独UART運用のためのUSB BOOTSEL再起動。
  不正値は拒否（0x17）。受理後は旧rateでSTATUS ACK→約500ms後に`reset_usb_boot`。
  LOG_UART（UART0）側でも `bootsel` 行（大小不問・改行終端）で同一動作。
  Flash書き込みなし。PROTO_VER据置（Task 9のv4改訂時に統合）。

### 5.7 RUMBLE (LEN=2、予約)

`[0]=左振幅0-255、[1]=右振幅0-255`。v1は送出しない。Switch出力受信パーサの受け口のみ確保し、ACK返送＋破棄する。将来PC転送を有効化する。

## 6. CRC8

CRC-8/SMBUS（poly 0x07、init 0x00、 refin/refoutなし、xorout 0x00）。検査値 `crc8("123456789")=0xF4`。
対象 SYNC除外（TYPE..SEQ）。残存見逃し率 約1/256（破損フレームがsync/len通過条件下）。

## 7. 受信ステートマシン

蓄積バッファ上で反復：(1)先頭0xAB整列→(2)LEN検証（既知型は正確長）→(3)CRC検証。一致で確定・SEQ更新・コールバック、不一致でERR加算＋先頭1B前進（フレーム全体を捨てない）。満杯時は最古1B破棄＋ERR加算。部分フレーム停滞は次フレーム到来でCRC失敗→スライド復帰する（最大1フレーム損失）。

## 8. タイミング・安全

| 機構 | 規定 |
|------|------|
| STATE送信 | 変化時即送＋定期リフレッシュ（既定60Hz、上限1kHz） |
| USBポーリング | bInterval 8（実機写し）。低遅延は到着即report更新で確保 |
| timeout-neutral | 直近STATEから200ms無受信で全解放 |
| WDT | 2s。timeout-neutralとは別機構（前者=入力安全、後者=生存性） |
| USB監視 | unmount/suspendで即中立、再mountで復帰 |
| コア | dual-core可否はPoCで判定。不合格時はsingle-core＋1ms poll |

## 9. テストベクタ

正準生成器は `src/proto/protocol.c` の `frame_build`。CRC検査 `0xF4`。STATE例はu32全中立＋A押下（`BTN=0x00000002`）・SEQ任意で生成しテスト内で検証する（固定バイト列の手書きを避ける）。

## 10. PC側指針

HAT相当・Y反転・12bit pack・差分＋リフレッシュ・単一write()・方向別SEQ・HELLO・CONFIG送信・FTDI latency 1msはPC送信ラッパ1箇所に集約する。

## 11. 輸送写像表 (u32 → Switch 1 輸送3B)

線路u32 (VIIPER順・§5.1) は輸送非依存の論理値。Pico側pack関数
(`src/proto/pack.c` の `ctrl_pack_btn3`・`pack_stick_12bit`) で吸収する。
BT 3BとUSB 0x30ボタン3Bは任天堂が同一順序のため、両表は同値である
(実装は単一のpack関数を共有する)。

| u32 bit | ボタン | BT byte.bit | USB byte.bit | 備考 |
|---------|--------|-------------|--------------|------|
| 0 | B | B0.b2 | B0.b2 | |
| 1 | A | B0.b3 | B0.b3 | |
| 2 | Y | B0.b0 | B0.b0 | |
| 3 | X | B0.b1 | B0.b1 | |
| 4 | R | B0.b6 | B0.b6 | |
| 5 | ZR | B0.b7 | B0.b7 | |
| 6 | Plus(+) | B1.b1 | B1.b1 | |
| 7 | R押込 | B1.b2 | B1.b2 | |
| 8 | Down | B2.b0 | B2.b0 | 斜めは2bit同時 |
| 9 | Right | B2.b2 | B2.b2 | 斜めは2bit同時 |
| 10 | Left | B2.b3 | B2.b3 | 斜めは2bit同時 |
| 11 | Up | B2.b1 | B2.b1 | 斜めは2bit同時 |
| 12 | L | B2.b6 | B2.b6 | |
| 13 | ZL | B2.b7 | B2.b7 | |
| 14 | Minus(-) | B1.b0 | B1.b0 | |
| 15 | L押込 | B1.b3 | B1.b3 | |
| 16 | Home | B1.b4 | B1.b4 | |
| 17 | Capture | B1.b5 | B1.b5 | |
| 18 | GR | — (落とす) | — (落とす) | Switch 1輸送に位置なし |
| 19 | GL | — (落とす) | — (落とす) | 同上 |
| 20 | C | — (落とす) | — (落とす) | 同上 |
| 21 | Headset | — (落とす) | — (落とす) | 同上 |
| 22-31 | 予約 | — (落とす) | — (落とす) | 0送信・受信側は無視 |

B0.b4/b5 (右SR/SL)・B1.b6・B2.b4/b5 (左SR/SL) に相当するu32 bitは存在しない。
pack出力は常に0 (ProConにSR/SLはない)。

輸送report層の固定加工 (pack関数の外・各輸送のbuilderが行う):

- USB 0x30/0x21: `btn[1] |= 0x80`、`btn[2] &= 0xCF`、電池 `0x91`、振動 `0x09`
  (2wiCC ControllerData互換。wakecon `usb_pack_controller_data` 通り)。
- BT 0x30: 生3Bのまま。電池 `0x80`、振動 `0x08` (wakecon `hid.c` 通り)。

スティック12bit化 (両輸送共通):

- `x12 = x8 << 4`、`y12 = 4096 − (y8 << 4)` (4095でclamp)。
- Y反転はPC側で済ませる (§5.1)。Picoは8bit値をそのままpackする。

## 12. USB写し固定値 (Task 3)

- Device: USB 2.00・EP0 64B・`057E:2009`・`bcdDevice 0x0200`
  (ToadKing写し。`0x0210` 説あり。ドック検証で確認し確定する)。
- Config一式41B: Remote Wakeup・500mA・HID 1IF・IN `0x81`/OUT `0x01`
  (各64B・`bInterval 8` 実機写し。低遅延化のための短縮はしない)。
- HID report 203B (ToadKing写し。入力 `0x30`・`0x21`/`0x81`、
  出力 `0x01`/`0x10`/`0x80`/`0x82`)。
- 文字列: `Nintendo Co., Ltd` / `Pro Controller` / `000000000001` (純正固定値)。
- SPI `0x6000` 域 (シリアル) は `0xFF` で答える (実機風値を返すと
  Switch 2が `2162-0002` で落ちる実測のため。2wiCCも同運用)。
  ※「シリアル0xFF」とはこのSPI域の事。USB文字列シリアルは上記固定値のまま。
- SPI色 `0x6050` (13B。既定グレー系。`COLOR_SET` で書換・Task 4)。
  内訳: 本体RGB・ボタンRGB・左グリップRGB・右グリップRGB (左右独立)・不明1B。
  左右同色でも別々に保持する。`0x601B=0x01` (色情報あり) が無いとSwitchは
  デフォルト色を使い `0x6050` を無視する (dekuNukem準拠)。
  注意: Switch側は初回接続時の色をキャッシュするため、色定義を変えた場合は
  登録解除→再接続で取り直させる。
- ハンドシェイク: `80 04` 受信で入力開始、`80 05`・unmountで中立＋再待機。
- 無線OFF要求: CYW43/BT動作中はSwitch 2ドックがUSB列挙しない実測
  (wakecon知見) のため、有線FWは無線を上げない。最終FWは `WIRED_MODE`
  (Task 4) で無線停波を管理する。
