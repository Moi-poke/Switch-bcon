# op内死（Flash書込op開始後に戻らない死亡）：第三者レビュー用・事象整理書（2026-09-15）

対象ログ：`log/COM3_2026_09_15.21.09.02.303.txt`（W8計装UF2 `log/switch-bcon-w8-epoch.uf2`、12MB）。
前書 `docs/wdt_phenomena_brief_20260915.md`（書込→約2秒後死）および
`docs/wdt_w7_postmortem_brief_20260915.md`（U3・U5）の続編。
本書の主題は**新署名「op内死」**であり、従来の「完走後死」との対比が要点である。

## 0. 質問への先答え

はい、本件は**電源サイクル最初の接続の直後にPicoが落ちる話**である。
より正確には：起動後約34秒アイドル→初接続→暗号化→L2CAP確立中の
PSM-0x11（HID-Control）channel-open直後に、HID-openハンドラ先頭の
host保存（Flash書込op）が開始したまま戻らず、タイマ系停止→約2秒後に
WDT再起動する。同一電源サイクルで4/4再現（epoch 1–4）した。

## 1. 時系列再構成（epoch 1、電源投入後の初boot）

```
21:09:08  boot（epoch 1、host=0、link keys=0、w8全計数0）        :2-14
21:09:10–34  BCON t=1–26s、cid=0（26秒間アイドル、無接続）       :15-210
21:09:41  conn request → conn status=0x00 ok                    :156,162
21:09:42  pairing complete status 00 → encrypt en=1             :187,191
21:09:42  L2CAP PSM-0x11/0x13 incoming＋accept、config交換       :211-244
21:09:42  L2CAP_EVENT_CHANNEL_OPENED psm 0x11（34.210）          :245
           ERTM mode 0（34.227）                                 :246
           ── 以後沈黙（PSM-0x13 openなし、HIDログなし、BCON停止）──
21:09:44  WDT再起動（次boot banner、wdt=1）                      :248,251
```

次bootダンプ（epoch 2先頭、:252-260）：
`w8flash-total safe=1/0 tag=1/0`（begin 1、end 0）、
`w8last store tag=4243484F(BCHO) epoch=1 rc=0 dur=0`、
`del=0/0 lkput=0/0 lkdel=0/0`、全timer adds−fires=+1、
`w7stage`相当 `last=10 trail=2,3,4,5,7,8,9,10`。

epoch 2–4も同一署名を反復（各bootがbeginを1つ積み、
`safe/tag` totalsは1/0→2/0→3/0→4/0、`host=0` のまま）。

## 2. op内死の証拠（5点）

1. **begin>end**：`safe`・`tag` とも begins=ends+1（4 boot累積で4/0）。
   計数は `tag_store_safe` 入口（BEGIN）→`flash_safe_execute`→
   `tag_store_fn` 入口（BEGIN）→`tlv->store_tag`→…→ENDの順に記録される
   実装（W8差分で確認済み）。END欠落＝`store_tag` から戻っていない。
2. **write行なし**：BTstack bank層の `write '4243484F'` ログ
   （`store_tag` 内のprogram直前の `log_info`）が出ていない。
   よって停止点は `store_tag` 入口〜最初のprogramの間
  （migrate判定・erase・lockout取得のいずれか）。
3. **host=0 維持**：次bootも `host=0`（:251,472,693,921相当行）。
   書込がcommitされなかったことと整合。自前 `host saved (n=..)` 行もなし。
4. **最終event＋WDT時刻整合**：最終アプリログは34.227のERTM行。
   WDT（2秒窓）の発火が約36.2（次banner :44は立上がり分を含む）と整合。
   BCON（t=27s以降なし）を含むタイマ系が34.2時点で停止。
5. **stage trailは正常**（`last=10`）：最後の `usb_handler` は
   set＋add完了まで到達済み。死はBTstackイベント文脈
  （HID-openコールバック内の `store_host`）で起きており、
   `poll_tick` 本体・re-arm経路のwedgeではない。

補足：`store_host`（BCHO書込）の呼出し元はHID-open成功分岐のみ
（`src/main.c`、静的監査済み）。`hid open. …` 表示行が `store_host`
復帰**後**に出るため、表示なしは「ハンドラに入り、保存開始後に
戻らなかった」と整合する。BTstackがHID-openをcontrol-channel-open
時点で発火させる挙動とも整合する（PSM-0x13 openは死後に来ない）。

## 3. 対照：同一電源サイクル内のep5（完走後死）とep6（生存）

- **ep5**：conn :34→encrypt→0x11 open（11.148、:1143）→
  `write '4243484F' at 89d`＋`host saved (n=1)`（:1145-1146）→
  0x13 open（:1160）→MODE_CHANGE mode 2（:1167）→約2秒後WDT。
  次boot `host=1`（書込commitの証明）。`w8last dur=9518`。
  ＝従来型Death-B（完走→約2秒後死）。同一UF2・同一電源でop内死と
  共存した。
- **ep6**：bond成立（`pairing complete` :1349 → `gap_store_link_key
  type 4` :1351 → `store with tag 42544c0f` :1353 →
  `write '42544c0f' len 28` :1354 → `auth complete 0x00` :1357）。
  BCHOはdedupe skip（host=1のため書込なし）。以後SUB完走（全26 SUBは
  本boot）＋約26分生存（tailはdevice時刻26:17、正常empty＋neutral
  rumble継続）。SNIFFのmode 0→2遷移（25分時点）も生存。
  ＝L2CAP前の鍵保存は無害、BCHO書込なしPortは生存。

## 4. 仮説と強弱

### H1（本命）：Core1-lockout無限待ち
`tag_store_safe` は `flash_safe_execute(tag_store_fn, …, UINT32_MAX)`
で包む。lockout取得待ちは**無期限**であり、Core1がhaltしなければ
コールバック（＝tag BEGINの次）に入る前に永久停止する。
給餌timerを含むrun-loop全体が止まるためWDTが2秒後に発火する。
整合点：BEGIN記録済み・ write行なし（コールバック未入場なら当然）、
W5/W7/ep5の完走例（通常はlockout即時取得）との両立（条件付きhang）。
弱点・要検証点：Core1（UART-DMA polling＋短mutex）がhalt不能になる
条件の特定が未了。Core1コード上の非有界待ち（mutex・DMA・sleep）の
洗い出しが必要。BTstackイベント文脈（低優先度IRQ下）からの呼出しと
lockoutの相互作用も未整理。

### H2（次点）：bank-migrate／erase内の停止
`store_tag` 入口のmigrate判定→他bank erase（割込み禁止・数十ms級）
の中での停止。整合点：write行の前（eraseはログなし）で止まること、
ep1-4→ep5で挙動が変わったこと（bank状態の進行：部分的programや
tombstone蓄積→ep5でclean slotに到達？オフセットはep5で89d）。
弱点：RP2350の4KB eraseは通常数十msであり、2秒のWDTを直接説明
しない。eraseが**戻らない**（controller stall）機序が別途要る。
SDKのHAL erase worst-caseとbank sizeからの机上見積りが可能。

### H3：XIP stall（Flash実行コード問題）
コールバック経路上の関数がRAM常駐でなくXIP上の場合、program中の
bus stallでhangする。整合点：op内停止。弱点：SDKはflash操作関数を
RAM配置する設計であり、W5/ep5の完走例と矛盾しやすい。同一バイナリで
完走とop内死が共存する説明には追加条件が要る。

### H4：例外・Fault
HardFault等。弱点：boot banner域にFAULT行なし（要再確認：本ログの
各banner直後行）。非Fault停止の既往認定と矛盾。

## 5. 求める意見

1. §2の5点から「op内死（`store_tag`復帰なし）」の認定は妥当か。ほかに
   `begins>ends`＋write行なし＋`host=0` を説明できる機序はあるか。
2. H1（lockout無限待ち）について：RP2350＋SDK 2.3.0の
   `flash_safe_execute`＋`multicore_lockout` の意味論上、victim（Core1・
   UART-DMA polling）がhalt不能になる現実的な条件はあるか。
   BTstackイベントコールバック文脈からの呼出しは関係するか。
3. H2について：BTstack flash-bankのmigrate／erase worst-case時間と、
   本件bank size・write_offset（89d時点）からの見積りは妥当か。
   eraseが戻らない条件は考えられるか。
4. 同一UF2・同一電源で「op内死×4→完走後死×1→生存」が起きた事実は、
   bank状態進行説（H2寄り）と条件付きhang説（H1寄り）のどちらを支持するか。
   両者を分ける次の一手（机上・実機とも）は何か。
5. ep6の生存（bond＋鍵保存あり、BCHOなし）は「殺すのはBCHO/host系
   （またはそのlink phase）であり、あらゆるFlash書込ではない」との
   読解を支持するか。代替の読解はあるか。
6. 恒久対策（BT動作中のFlash変更を全面禁止しdirty-flag＋安全条件付き
   単一workerへ集約：`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md`）
   は、op内死の発見後も妥当か。H1が真の場合に追加すべき安全条件はあるか
   （例：worker内でもlockout timeoutを有限化＋失敗時リトライ）。

## 6. 制約・関連文書

- HEAD `c366bf4`＋未commit、commit・PRは明示指示まで禁止。
  `C:\Users\moilo\pico-wakecon` は参照専用・改変禁止。
- 秘密鍵バイトの記録禁止。hci_dumpは診断期限定。
- 関連：`docs/wdt_phenomena_brief_20260915.md`（9/9則の出発点）、
  `docs/wdt_w7_postmortem_brief_20260915.md`（U3・U5の整理とW8提案）、
  `docs/history/2026-09-15-wdt/verification-status.md`（検証状態）、
  `trial-history.md`（時系列、W8実機行に本件を記録済み）。
- timer系の+1/boot蓄積（U5確定）および `del=0/0`（U3機序上の確定）は
  本ログで決着済みであり、本書の主題外である。
