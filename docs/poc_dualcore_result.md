# dual-core PoC 結果 (Task 2 Step 3)

日付: 2026-09-13 / 基板: Pico 2 W (`pico2_w`) / SDK 2.3.0 / FW: `src/poc_dualcore/`
判定: **合格 (条件付き derated) → dual-core 採用**

## 条件 (deratedの理由)

- 手元のUART変換器が115200bps上限のため、spec既定1Mbpsではなく
  `-DPOC_DATA_BAUD=115200` ビルド＋`--baud 115200 --hz 500` で実施。
- 115200bps (約11.5KB/s) に対する500Hz×13B (約6.5KB/s) は線路使用率約57%。
- 1Mbps全速 (1kHz) での再確認は FTDI級アダプタ入手後の宿題とする
  (本FW既定値はspec通り1Mbpsのまま。`CMakeLists.txt` の `POC_DATA_BAUD`)。
- 単一アダプタ運用: 起動確認のみUART0、負荷中はLED (点灯固定=受信parse進行中、
  点滅=無受信)、事後にUART0で累積カウンタ回収。

## 同時負荷の内容 (計画書の要求)

- UART1へ500Hz STATE連続送信 (600秒本番＋60秒予行)＋自走parser
- Core0の5秒周期 `flash_safe_execute` (最終4Kセクタ消去＋256B書込＋verify)
- BTstack (Classic+BLE電源ON・未ペア)＋TinyUSB stub (PCに汎用KB列挙)
- 通算uptime約46分 (t=2785s) でハング0

## 証拠

- 送信側: 600秒完走 `done sent=300000 secs=600.0 eff_hz=500.0`
  (別途60秒予行 約30,000。前回64秒分31,998は配線不良でFW未達)
- FW側: `c:\pico-bcon\log\COM3_2026_09_13.18.05.39.816.txt` (t=2689–2785s抜粋)
  および起動側 `c:\pico-bcon\log\COM3_2026_09_13.17.01.11.954.txt` (t=1–323s)
- 代表行 (負荷後):
  `POC t=2785s bt=1 usb=1 boot=3 frames=330003 crc=0 drop=2 ovr=0
  iters=421539840 fok=552 ffail=0 fmax_us=38589 keys=2748`
- `log/` は計測生データ置場としてgit管理外 (`.gitignore` 参照)。

## 照合

| 項目 | 期待 | 実測 | 判定 |
|------|------|------|------|
| 送信→受理 | 30,000＋300,000＝330,000 | frames−自検3＝330,000 | 一致 |
| CRC/形式reject (crc) | 0 | 0 | OK |
| SEQ欠番イベント (drop) | 境界2のみ | 2 (自検0x12→予行0x00、予行末0x2F→本番0x00。仕様通り1欠番連続=1加算) | 説明可 |
| UART overrun (ovr) | 0 | 0 | OK |
| Flash verify失敗 (ffail) | 0 | 0 (fok 552+) | OK |
| Flash stall最大 (fmax_us) | ring 16KB (160ms分) 以下 | 38,589us (約39ms) | 余裕あり |
| BT (`bt`) | 1 | 1 (`hci_power_control`追加後に点灯) | OK |
| USB (`usb`) | 1 | 1 (stub KB列挙・IN送信継続) | OK |
| ハング | 0 | 0 (46分・iters単調増加、uint32周回1回は想定内) | OK |

## 過程で出た不具合と対処 (いずれも本対策済み)

1. `bt=0` のまま — PoCが `hci_power_control(HCI_POWER_ON)` を呼んでいなかった。
   BTstackは自発給電しない (wakeconは `link_conn.c` の `link_radio_update` 経由)。
   最終FW (Task 4) への教訓として記録する。
2. 初回64秒走で `frames=3` のまま — 変換器TX→GP5の配線不良 (FW未達)。
   sender正常・FW自検PASSだったため配線に切り分け、修正後に本計測。
3. `hid_report_type_t` 二重定義 — BTstackとTinyUSBのヘッダは同一TU不可。
   最終FWもTU分離 (`src/usb/*` vs BT側) を踏襲する (Task 3)。

## 確定事項 (最終FWへ)

- dual-core 採用。不合格時切替 (single-core＋1ms poll) は使わない。
- Core0＝BTstack＋TinyUSB＋CYW43＋Flash＋log、Core1＝UART DMA＋parser＋mutex push。
- Core1で `flash_safe_execute_core_init()` (内部で `multicore_lockout_victim_init`)。
  全Flash書込は `flash_safe_execute` 経由。起動時 `victim=1` をlog確認する。
- DMA ring 16KB (115200bpsでは約1400ms分、1Mbpsでは約160ms分)。
  実測stall 39msに対し1Mbpsでも約4倍の余裕がある。

## 宿題

- [ ] 1Mbps/1kHz全速での再走 (FTDI級アダプタ入手後。`build/` 再構成のみで可)
- [ ] 実ペアリング・再接続・wake取込再生下の同時負荷 (Task 4の実機検証に含める)
