# pico-bcon 引継ぎ（2026-09-14 04:30 JST・BT接続安定化）

前回引継ぎ（2026-09-13夜・Task 4 BT検証中）の続き。旧 `C:\Users\moilo\pico-wakecon` は参照専用・改変禁止。

## 0. 要旨（3行）

- **WDT再起動ループの真因は open 時 TLV（hostタグ）書込**。停止したら死亡も止まった（BCON・タイマ生存を確認）。機序の最有力は BTstack タイマ構造の破壊（イベント生存・非Fault・無退出と整合）
- **鍵诅咒の機構を特定**：BTstackは non-bonding pairing の鍵を永続化しない（コード確認）。古代鍵だけ残り、毎回 stale応答→`0x05`→`0x66`
- **現状**：死亡なし・ペアリング＋openは通るが、SwitchがSUBを送らず約1秒で切断（`0x13`）。次は一時ワイプ版（4:24出荷・未試験）でSUB到達を狙う
- 棄却済み仮説の一覧は§8（再試行不要）

## 1. リポジトリ状態（未commit・commit禁止指示なし）

- `C:\pico-bcon`、main。`build/`・`log/*.uf2` はgit管理外
- 完了commit: `7bdd7d3` → `a6c0660` → `c366bf4`（以降未commit）
- 未commit： M `CMakeLists.txt`、`spec/protocol_v3.md`、`src/main.c`、`src/poc_dualcore/poc_send.py`、`src/proto/protocol.h`、`src/usb/usb_wired.c`、`tests/host/CMakeLists.txt` ／ 新規 `src/bt/*`（14件）、`src/proto/dispatch.c/.h`、`tests/host/test_config.c`
- hostテスト3/3 ALL PASS（fresh確認済み）

## 2. 出荷バイナリ（どちらもgit管理外）

- `build/pico-bcon.uf2`：有線既定（WIRED_DEFAULT=1、BUMP=0）
- `log/pico-bcon-wireless-test.uf2`：無線既定＋診断全部入り。**現行は4:24版**（送信復帰＋TLV停止＋起動時ワイプ＋hci_dump＋reporter＋MSPLIM＋SCR＋heartbeat。SNIFF無効継続、BD_ADDR_BUMP=1で自MAC末尾 `...:cb`）
- 無線版の建て方（`build/`を汚さない）：`cmake -S . -B <temp>/bcon-wireless -G Ninja -DCMAKE_MAKE_PROGRAM=...ninja.exe -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 [-DBD_ADDR_BUMP=1]` → `--target pico-bcon` → UF2を`log/`へコピー。`POC_DATA_BAUD=115200`は変換器上限のderated（既定1Mbpsは不変）
- hostテスト：`vcvars64`済みcmdで `cmake -S tests/host -B build-host` → `--build --config Debug` → `ctest -C Debug -V`

## 3. 確定事項（証拠付き）

1. **死亡＝open時TLV書込**：全死亡bootにhostタグ書込（`write '4243484f'`）あり、失敗boot（openなし）に死亡なし。4:04版で書込停止→死亡停止・タイマ生存（`empty tick`＋BCON継続、60秒超生存）。送信ゲートの有無と生死は無相関（ゲート中も死亡）＝送信路は無罪
2. **死亡の形＝BTstackタイマ死亡**：open後、イベント系（L2CAP設定・切断処理・表示）は動作継続する一方、タイマ系（empty 100ms×12機会・BCON）が沈黙→WDT給餌停止→発火。非Fault（動作確認済みreporterでFAULT行なし、MSPLIM有効下でもなし）、run loop無退出
3. **鍵诅咒の機構**：BTstackはlink keyを「bondable かつ少なくとも一方がbonding要求」の場合のみ永続化（`hci.c`のLINK_KEY_NOTIFICATION処理をコード確認）。Switchは常時 `AuthReq=0x00`（`Remote not bonding`）→ fresh鍵は保存されず、古代鍵だけ残る→毎回 stale応答→`auth 0x05`→`0x66`。ペアリングのたび鍵通知の16バイトが変わるのにTLV保存行（len 28）が出ないことで裏付け
4. **A/B**：wakecon UF2（9/12 build）は同Switch2で接続維持・SUB完走。環境無実・bcon固有で確定
5. **設定同一性**：SDK 2.3.0／Release／pico2_w／rp2350-arm-s／同一toolchain。`btstack_config.h`・`tusb_config.h`・GAP/HID/SDP設定はwakeconと1:1同一。**唯一の差分は `pico_enable_stdio_uart`（wakecon 0＝raw puts、bcon 1＝割込みUART）**。犯人としては弱い（軽量ログ時代から再現）が未排除
6. **ハンドシェイク**：dekuNukem／nxbt／SDL／Chromium資料と突合。ずれはwakecon共有か到達不能。死亡は初回SUB受信前に起きるため無関係
7. **dual-core**：DMA claim分離・Core1はIRQ不使用ポーリング・mutex短保持・lockout参加（`victim=1`は生値）・PoC stall実測39ms。干渉経路なし
8. **SNIFF無罪**：mode移行なしの死亡を確認。無効化はrevert候補
9. **元祖0x66の半分はoutgoing競合**：`Create outgoing HID Control`→認証と競合→`0x66`。受動接続のみへの切替は未実施（繰延）
10. **自MAC `7c:bb:8a:d3:fd:cb`**（BUMP+1、診断用一時措置）。相手peerは終始同一 `3c:a9:ab:37:cf:d3`（＝Switch 2）

## 4. 未解決・次の一手（優先順）

1. **SUB到達試験（4:24版・未試験）**：起動時鍵＋host全消去（`keys+host wiped (temp)`→`link keys=0`確認）＋Switch側全登録解除＋完全電源OFF→ON→Change-Grip放置。`SUB=0x..`出現なら勝利。以後は恒久修正へ。出なければ「SSPは通るのにSUBが来ない理由」に転進
2. **恒久修正**（勝利後）：host保存dedupe（変化時のみ保存。書込嵐の恒久止め）／auth失敗時鍵破棄／outgoing抑止（受動接続のみ）／SNIFF復帰／BUMP判断（恒久アドレス決定＋以後不変）／hci_dump除去（鍵バイトを出すため秘密保持上必須）／全診断revert／Step 4 commit→Task 5（PC送信ラッパ）
3. **SCR値は不信扱い**：2回とも単独bootモデルと矛盾。timer/BCON/dump表示を正とする。原因別途
4. **TLL bank監査**（任意）：tombstone多数のbankとタイマ破壊の関連は未解明。dedupeで発火機会が消えるため優先度低

## 5. ログの読みどころ

- 死亡（旧）：`hid open done`→（tick/cansend/TX）→沈黙→バナー`wdt=1`
- 0x66：`linkkey req`→`hid open FAIL 0x66`→`auth complete 0x05`（基本outgoing発）
- stall：`conn ok`→`disc 0x05`約80ms・SSPなし
- 正常系（wakecon）：SSP完走→SUB群→維持
- 現行（4:04以降）：open→約1秒無言→Switchが切断（`0x13`）→BCON継続（死亡なし）

## 6. 制約

- 秘密（LTK/鍵本体）をログ・文書に出さない（hci_dumpは診断期限定）。peer BD_ADDR程度は可
- `log/`は生データ置場（管理外）。derated設定の変更はbuild時のみ
- 診断マーク：「Task 4診断用の一時措置」。revert時はこの文字列でgrep

## 7. 新セッションへの指示

1. まず4:24版＋Switch掃除でSUB到達試験（§4-1）。結果で分岐
2. 技能：バグ時はsystematic-debugging、完了宣言前はverification-before-completion、計画遂行はexecuting-plans
3. commit・PRは明示指示があるまで行わない

## 8. 棄却済み仮説（試してダメだったこと・再試行不要）

1. **Switch側旧鍵残存（BD_ADDR単位）**：BUMP新MAC（`...:cb`）でも同一失敗 → 棄却
2. **SNIFF突入**：mode移行なしの死亡を確認 → 無罪確定（無効化はrevert候補）
3. **送信路**：ゲート中も死亡、復帰後も生存 → 無罪確定（ゲートは復帰済み）
4. **自stack読出不良（H2）**：iterator＋host往復正常、TLVダンプ健全 → 棄却
5. **Core1/DMA・割込み干渉**：Core1はIRQ不使用ポーリング、DMAはclaim分離、mutex短保持 → 経路なし
6. **Flash未保護説（他AI案Aの機序）**：保存は`flash_safe_execute`経由、`victim=1`確認済み。保存完了後に正常動作してから死ぬため時間的に不成立
7. **Faultクラッシュ説**：動作確認済みreporter＋MSPLIM有効下で複数回FAULT行なし → 非Fault停止。ただしLOCKUPは未排除（もはや不要な段階）
8. **SCR値の直接利用**：2回とも単独bootモデルと矛盾 → 不信扱いに変更。timer/BCON/dump表示を正とする
9. **「再接続すれば直る」期待**：outgoingは`0x66`/`0x0b`の火種と確定。受動接続のみへの切替は未実施
10. **hci_dump常設**：鍵バイトを出すため秘密保持上不可。最終版から必ず除去
11. **UART stdio差分**（wakecon 0 vs bcon 1）：唯一の構成差だが**未試験**。棄却ではない。3:55以降の結果次第で実験要否を判断
