# TODO: 有線 Joy-Con 接続 (解決済み)

- 状態: 動作確認済み (2026-09-21)。PokeCon の Wired 切替で USB 接続・操作OK。
  モード: Joy-Con R および Joy-Con L の両方で確認。
- 関連 Issue 案: なし（解決のため起票不要）

## 実機結果

- ProCon 有線: 認識 OK
- Joy-Con 無線: 認識 OK
- Joy-Con 有線 (旧版): 認識 NG → PABot parity 版で再テスト中
- 2026-09-21 22時台: parity 版 (`log/switch-bcon-pabotparity-20260921.uf2`,
  SHA256 `D7994BCD…`) 書込済み。poc_send --ping は TIMEOUT するが、
  FW 生存は確定 (BT Joy-Con が Pico 抜去で消える + PokeCon の ping 疎通OK)。
  --ping 不一致の理由: PONG 返信は baud hunt ロック後 (2 連続良フレーム) の
  TX 抑制解除が条件。fire-and-forget 系 (--emulate/--bootsel) は無応答で
  済むため通る。poc_send --ping の 2 発ではロック前に終わる。
  注: parity 変更は USB 応答 builder のみで起動時経路・Flash 書込なし。
  旧 joyfix2 版 (`log/switch-bcon-joyfix2-20260921.uf2`) で切り分け可能。

## 実施済み (2026-09-21)

- PABotBase2-Pico2W UF2 (`D:\Download\PABotBase2-Pico2W-2026090200.uf2`,
  SHA256 `19147B51…CD5F`) の静的リバース。
  所見: USB は `057E:2009` 単一列挙、`2006/2007/200E` 記述子なし。
- A案 (USB=2009固定、応答内容で Joy 表現) で実装:
  - `src/usb/usb_hid.c`: Joy 用 0x10 SPI 分岐
    (6050/601B 例外 → 既知表値 → 未知域ゼロ → 域外 0xFF+ACK)、
    応答生成の helper 分離、Device Info の role コンテキスト化
  - USB PID/製品名を `2009` / `Pro Controller` に固定
  - `80 04` 無応答化 (実機準拠)
- host テスト全 green (9 suite相当: `ctest --test-dir build-host`)
- 実機フラッシュ版: `log/switch-bcon-joyfix2-20260921.uf2`
  (SHA256 `8EC99902…DBC`)
- 職場 Edge の Copilot で設計+実装レビュー (条件付き承認→5点反映→承認)

## 残りの容疑者 (優先順)

1. SPI cal 実値: Joy-USB 時に既知表値を返すようにしたが、
   `6086` 等の表外や PABot キャッシュ内容との差が未確定。
   生 USB キャプチャで PABot 応答との差分確定が必要
2. `0x02` byte11 (`0x02` vs `01`)、MAC エンディアン
3. `bcdDevice` (`0x0200` vs PABot `0x0001`)

## 次の手順 (再開時)

1. PABotBase UF2 に一時書き戻し → Wireshark/USBPcap で有線 Joy 初期化列を採取
2. switch-bcon でも同条件で採取 → 応答差分を特定
3. 差分を TDD で修正 → 実機確認
4. `spec/protocol_v3.md` §12.1/§12.3 の TBD を実測値で更新

## 証跡

- `C:\Users\moilo\AppData\Local\Temp\opencode\joyrev\` (UF2 リバース一式)
- `C:\Users\moilo\AppData\Local\Temp\opencode\joyrev2\`
  (`pabotbase2_usb_static.txt`, `wired_joy_gap.md` 32項目)
- `C:\Users\moilo\AppData\Local\Temp\opencode\ulw-joycon-*.md` (作業 notepad)
