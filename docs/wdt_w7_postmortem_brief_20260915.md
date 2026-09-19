# W7 post-mortem解釈：第三者レビュー用・事象整理書（2026-09-15）

`docs/wdt_phenomena_brief_20260915.md` の続編。W7統合計装（flash-op counters＋微細stage＋timer会計＋hci_dump）の死亡時ダンプが得られたので、その解釈と未解決点（U3・U5）について意見を求める。推測と確定を分離して記載する。

## 1. 前提（前書から継続・変更なし）

- 機器: Raspberry Pi Pico 2 W（RP2350）、Pico SDK 2.3.0、BTstack同梱版
- 相手: Nintendo Switch 2（peer `3C:A9:AB:37:CF:D3`、終始同一）
- 自MAC: `7C:BB:8A:D3:FD:CA`
- WDT: `watchdog_enable(2000, 1)`、給餌は1ms周期BTstackタイマ駆動の `poll_tick()` 末尾
- Core1: 製品側はprintfなし・BT/CYW43/Flash不使用・ポーリング＋短mutexのみ
- HEAD `c366bf4`＋未commit（Phase 3 T1〜T7＋色分離）。commit・PRは明示指示まで禁止
- 秘密鍵バイトをログ・文書に出さないこと

## 2. W7ダンプ原文（死亡直後bootの印字）

取得元：`log/COM3_2026_09_15.02.43.25.327.txt:138-143`（UF2 `log/switch-bcon-w7-diag.uf2`）。

```
w7rec n=119178 slot=42
w7flash safe=1/1 tag=1/1 del=1/1 lkput=0/0 lkdel=0/0
w7lastrc safe=0 tag=0 del=0 dur=9430/9060/9163
w7tmr usb a=9260 f=9258 to=18982 empty a=1490 f=1488 to=19079 stats a=50 f=48 to=19168
w7stage last=10 n=92580 trail=2,3,4,5,7,8,9,10
w7ev last=0x6e n=4998
```

計数の定義（実装 `docs/history/2026-09-15-wdt/sdd/task-7-report.md` §2–§6、diff `sdd/bcon-wab-diffs/w7-store-vs-backup.diff`・`w7-main-vs-backup.diff`）：

- `safe`: 自前 `tag_store_safe()` の `flash_safe_execute` 呼出し（begin/end）
- `tag`: 同内部 `tag_store_fn` 内の `tlv->store_tag` 呼出し（begin/end）
- `del`: 自前 `tag_delete_fn` 内の `tlv->delete_tag` 呼出し（begin/end）
- `lkput`/`lkdel`: `hci_set_link_key_db` に被せたshimの `put_link_key`/`delete_link_key` 呼出し
- `a`/`f`: 自前3 timer（usb/empty/stats）の `add_timer` 後／handler入口
- `stage 1..10`: `POLL_ENTER(1)/POLL_BODY_DONE(2)/WDT_UPDATE_DONE(3)/POLL_TAIL_DONE(4)/POLL_RETURN_IMMINENT(5)/USB_HANDLER_ENTER(6)/POLL_RETURNED(7)/SET_TIMER_DONE(8)/ADD_TIMER_DONE(9)/USB_HANDLER_RETURN_IMMINENT(10)`
- recorderは `.uninitialized_data`（NOLOAD）常駐でWDT再起動を跨いで継続する。電源断・UF2再flashではリセットされる

死亡セッションのログ対応（同ファイル）：

- `[00:00:18.822] LOG -- btstack_tlv_flash_bank.c.451: write '4243484f', len 6` → `host saved (n=1)` → `hid open. host … saved`（`store_host` の実書込。TAG_HOST=`BCHO`）
- 以後L2CAP確立ログのみで約2秒後に沈黙→WDT（`wdt=1`）。`auth complete status=0x00`（成功）のためauth-fail経路は不発
- 対照：直前boot（`log/COM3_2026_09_15.02.42.39.163.txt`）は `hid open. host … saved` のみで `write '4243484f'` 行なし（dedupe skip）→ SUB 17件完走・生存。書込→死／ skip→生存の対応は9/9則と整合

## 3. 確定した解釈

1. `safe=1/1 tag=1/1` は死亡セッションの `store_host` 実書込と一対一に対応する（BTstackの `write '4243484f'` 行あり）
2. `lkput=0/0 lkdel=0/0` により、当該bootでBTstack link-key DBへのput/deleteはゼロ（auth成功・鍵保存なしと整合）
3. `w7stage last=10 trail=2,3,4,5,7,8,9,10` は正常な1ms周期の末尾（re-arm済み・次回不発）を示す。`poll_tick` 本体内wedge説は棄却を維持
4. 二重 `add_timer` 説は棄却を維持（BTstackの二重登録は `log_error("already registered!")` を出す実装で、有効な `hci_dump` 経路上の死亡ログ4件に痕跡ゼロ）

## 4. 未解決点U3：`del=1/1` に生きた呼出元がない

`tag_delete_fn`（DEL計数の唯一の源）の静的呼出元は2件のみ（`src/bt/store.c:52-56,100-113,193-205`）：

1. `store_host_forget`（TAG_HOST削除）← 呼出元は `src/main.c:409-415` の `FX_KEY_DELETE` ケースのみ
2. `store_cap_forget`（TAG_CAP削除）← 呼出元は `link_cap_clear`（`src/bt/link_cap.c:91-98`）のみ。**`link_cap_clear` 自体にリポジトリ内呼出元がゼロ**（デッドコード）

`FX_KEY_DELETE` も以下3点で否定される：

- 発火時の必須ログ `"keys deleted (classic + host tag)"`（`src/main.c:414`）が死亡前ログ全域にゼロ件
- 同経路は `gap_delete_all_link_keys()`（`hci.c:584-596`：iterator→`gap_drop_link_key_for_bd_addr`→`delete_link_key`）を必ず通る。起動時 `link keys=1` の状態で走ればshim計数 `lkdel=1/1` になるはずだが実測 `lkdel=0/0`
- 死亡前区間のBCONは `frames=3` 一定・`err=00` で、PC由来フレーム（`dispatch.c:82-84` 以外に `fx` 設定箇所なし）の痕跡なし

BTstack TLV bank内部の `btstack_tlv_flash_bank_delete_tag_until_offset`（SDK `btstack_tlv_flash_bank.c:355`）は自前 `tag_delete_fn` を経由しないことを実コードで確認済み（store_tag内部の旧エントリ無効化はDEL計数に現れない）。よって **del=1/1は現treeの静的呼出グラフでは説明できない**。

残る可能性：(a) WDT再起動を跨いだ計数蓄積（recorderが継続するため、別bootの操作が合算）、(b) 計装の表示不足（`w7_state_t.last_tag[6]` に削除TAG値は記録されているが、`w7_boot_dump` が印字していない。ringにもあるがdump対象外）。現データではTAG_HOST削除かTAG_CAP削除かも区別できない。

## 5. 未解決点U5：`adds-fires=+2`（全timer系統的）

`usb_handler`（`src/main.c:602-606`）等の `FIRE→set→add` 構造上、定常差分は起動時arm分の **+1** になるはず。観測値は3 timerとも **+2**（usb 9260/9258、empty 1490/1488、stats 50/48）。個別timerのwedgeでは説明できず、二重add説は§3の通り棄却済み。

本命は跨起動蓄積：`stats f=48` に対し当該bootの実走行は約20秒（BCON 6→18s＋死亡窓）で、1秒周期の発火回数として過多。recorder継続設計と合わせ、直前の生存boot（02:42、SUB完走）分の計数が合算されている疑いが濃い。ただし02:43先頭bootのbannerを取り逃がしており（キャプチャ開始がBCON 6s〜）、単boot証明ができない。

副次所見：store所要 `dur≈9430/9060/9163us` がW5計測（`store dt_us≈2070 rc=0`、5/5同一）の約4.5倍。bank状態（今回 `write_offset 86b`）か測定層の差か未分離。恒久worker設計には影響しないため分離して扱いたい。

## 6. 提案する次実験W8（最小差分・単一変数）

W1〜W7と同一運用（SDK・BTstack改変なし、復元証明、UF2を `log/` へ、commitなし）：

1. `w7_boot_dump` の `w7flash`／`w7lastrc` 行に `last_tag[6]`（特にDELETE_TAG）を追加印字。delのTAG特定（`BCHO=0x4243484f` vs `BCW1=0x42435731`）が目的
2. spareの `watchdog scratch[3]`（SDK予約 `[4..7]` と自前 `[0..2]` は使用中、衝突なしをW7で確認済み）にboot epochを increment＋印字。+2の蓄積／実在の分離が目的
3. それ以外はW7と同一。HWはオーナー側バッチ（flash→死亡再現→次bootダンプ取得）

## 7. 求める意見

1. U3（§4）の呼出元列挙に漏れはないか。特に見落としている `tag_delete_fn`／`tlv->delete_tag` 到達経路はあるか
2. del計数とBTstackログ（`write` 行はあるが `Erase tag` 行なし）の非対称について、削除対象不在時の無書込delete（scanのみ）で `dur≈9163us` を説明できるか。できない場合の代替説明はあるか
3. U5（§5）の+2について、跨起動蓄積以外の機序（例：init arm＋初回re-armの二重計上など）で全timer系統的な+2を説明できるか
4. W8（§6）の2点追加でU3・U5は決着するか。不足があれば最小追加項目を挙げてほしい
5. §5副次所見の所要時間差（2ms→9ms）は追う価値があるか、それともbank充填度の範囲内として無視してよいか
