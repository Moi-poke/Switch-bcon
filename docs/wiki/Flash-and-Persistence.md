# Flash-and-Persistence — TLV 永続化と WDT 危険

## TLV タグ表

bcon 独自の名前空間 `BCxx` を使う。wakecon（`NXxx`）とは別名前空間であり、同一 Pico 共存時に wakecon 保存値（特に WIRED）を拾わないようにする。Classic リンク鍵（BTstack 管理）は同一機器として共有するのが正しいため対象外である (`src/bt/store.c:13-20`)。

| TAG | 値 | 内容 | サイズ |
|---|---|---|---|
| `BCHO` | `0x4243484F` | host 住所録（相手 BD_ADDR）(`src/bt/store.c:13`) | 6B |
| `BCCL` | `0x4243434C` | グリップ色（SPI `0x6050` の 13B 目は仕様値のため戻さない）(`src/bt/store.c:14,165-187`) | 13B |
| `BCW1` | `0x42435731` | 取込 blob（`CAP_BLOB_SIZE`）(`src/bt/store.c:15`, `src/bt/store.c:153-171`) | blob |
| `BCWR` | `0x42435752` | 有線モード 0/1 (`src/bt/store.c:16,207-217`) | 1B |

> 🚧 In-progress: 新 TAG `BCBR`（ボーレート永続）は定義のみ先行であり未実装である。`BAUD_SET` の永続先として設計書が指定している (`docs/superpowers/specs/2026-09-15-uart-features-design.md:46`)。

## TAG差分（再構築用）

- `BCBR` は未実装であり現行表の 4 TAG が全部である (`src/bt/store.c:13-16`)。`BTL`（BTstack鍵 28B）は bcon TAG ではなく BTstack 管理であり、non-bonding のため fresh 鍵は保存されない設計だが当該セッションは生存した唯一例である (`docs/history/2026-09-15-wdt/verification-status.md:64-69`)
- `BCCL` は 13B 保存・復元 12Bであり 13B 目は仕様値のため戻さない (`src/bt/store.c:165-187`)。由来と色値の確定経緯は [References](References.md) R3・R6 を見ること

## 全書込経路（現行ツリーの呼び出し元）

| # | 呼び出し元 | TAG/DB | 文脈 | 備考 |
|---|---|---|---|---|
| W1 | `handle_hid_meta` の `HID_SUBEVENT_CONNECTION_OPENED` → `store_host(a)` (`src/main.c:687-718`) | `BCHO` 6B | HID/HCI イベントコールバック、HID 接続直後 | BT 動作中の直接書込。WDT 死の主経路 |
| W2 | `exec_fx` の `FX_COLOR_SET` → `store_color()` (`src/main.c:404-408`) | `BCCL` 13B | `poll_tick`（1ms timer 文脈）。Switch 接続中があり得る | BT 動作中の直接書込 |
| W3 | `exec_fx` の `FX_KEY_DELETE` → `gap_delete_all_link_keys()` + `store_host_forget()` (`src/main.c:409-415`) | 鍵 DB + `BCHO` 削除 | `poll_tick`。BT 動作中の可能性あり | 二重書込（DB＋TAG） |
| W4 | `exec_fx` の `FX_WIRED_MODE` → `store_wired(w)` 後に 500ms で自発再起動 (`src/main.c:416-433`) | `BCWR` 1B | `poll_tick`。BT 動作中の可能性あり | 再起動が約 2 秒の死亡窓を先取りするため歴史的に生存してきたが、quiesced 化または reboot-safe 化の対象 |
| W5 | `link_cap_tick` の `CAP-DONE best>=0` → `store_cap_save()` (`src/bt/link_cap.c:100-132`) | `BCW1` blob | `link_poll` 経由で `poll_tick` から。スキャン停止直後、BT 稼働中 | BT 稼働中の書込 |
| W6 | `link_cap_clear()` → `store_cap_forget()` (`src/bt/link_cap.c:91-98`, `src/bt/store.c:193-205`) | `BCW1` 削除 | 呼び出し元依存（現状は同期的） | W5 と同クラス。なお現ツリーで `link_cap_clear` 自体にリポジトリ内呼び出し元がゼロとの指摘がある（U3 関連。未解決点として記録） |
| W7 | `packet_handler` の `HCI_EVENT_AUTHENTICATION_COMPLETE` 認証失敗 → `gap_delete_all_link_keys()` (`src/main.c:804-817`) | 鍵 DB | HCI イベントコールバック、ペアリング失敗直後。ACL 残存の可能性あり | BT 動作中の DB 変更であり、W3 と同様に遅延対象 |

上表は恒久修正設計書の現行経路一覧 (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:86-97`) をコード行付きで再構成したものである。読込系（`store_*_load`、`link_key_count`）は Core1 起動前の XIP 直読であり現状維持である (`src/main.c:903-910`, `docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:96-97`)。

## 重複排除（dedupe）

`store_host` は変化時のみ保存する。`probe_host_known` かつ `memcmp == 0` なら早期復帰し、Flash 摩耗も timer 危険も起こさない (`src/bt/store.c:110-118`)。この dedupe により、同一 host への再接続では書込が起きず生存する（Phase-3 S3、W7 対照 boot 等）。恒久 worker 設計でも set 時と flush 時の二重検査として温存する方針である (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:354-369`)。

## `flash_safe_execute` / Core1-lockout 規則

- 書込 op は単発（single-level）の `flash_safe_execute` でのみ包む。**入れ子禁止**：SDK pico_flash はコア毎に単一スロット `irq_state[]` で PRIMASK を退避するため（`sdk/.../pico_flash/flash.c:106,203,210`）、外側の中から内側を呼ぶと共有スロットが上書きされ、外側 exit が PRIMASK=1 を復元して全 store が IRQ 禁止を置き去りにする（Death-B の確定機序。W10c で `pm=1` を 3/3 実測）。自前の外側ラッパは付けない。BTstack TLV/HAL が全 mutation を内側で包む（erase・page program・delete-via-zero。read は XIP 直読 — 9b safety case）。
- `tag_store_safe` は `tag_store_fn` を直接呼び、復帰時に `store_irq_guard()` で PRIMASK 不変条件を検査・自己修復する（`src/bt/store.c:71-107`）。削除側も直接呼び＋guard（`src/bt/store.c:146-147,241-242`）。guard 発火行（`irq leak healed`）が出たら即報告すること。
- Core1 は `flash_safe_execute_core_init()` を呼び、内部で victim init される。起動 log の `victim=1` は生値であり、Core1 lockout 参加の証拠である (`src/main.c:274-278`, `src/main.c:929-931`)。Core1 自体は flash 書込を行わないが XIP 上で実行するため、mutation 毎の内側 lockout で退避される。
- TLV 読込は Core1 起動前に済ませる (`src/main.c:903-910`)。TLV 永続化の初期化（`btstack_tlv_flash_bank_init_instance`＋`btstack_tlv_set_instance`＋鍵 DB 設定）は Core1 起動前の純 RAM 設定のため Flash 保護不要である (`src/main.c:892-901`)
- BCON に `pm=`（PRIMASK。0=IRQ 開）を恒久出力する (`src/main.c:573-592`)。store 直後の `pm=1` はリークの証拠であり、guard と対で監視する。

## WDT 危険の要約（詳細は Troubleshooting と history へ）

確定事実は次の通り。BT 動作中の Flash 書込のうち**入れ子（二重 `flash_safe_execute`）のものは、完走しながら core0 の IRQ を置き去りにし、約 2.0 秒後に run-loop を殺す（Death-B）**。W10c の 2 秒窓計測で最終 poll 完走・timer list 健全・全 timer 発火中・HCI RX 生存のもと `PRIMASK=1` を 3/3 実測し、SDK 共有スロットの上書き機序と一致を確認した。修正（外側除去＋guard＋BCON `pm`）は検証 run（2026-09-16）で PASS：両 store 完走→SUB 完走→20 秒生存、`pm=0` 全行、guard 沈黙。詳細は台帳 task-12–14（`docs/history/2026-09-15-wdt/sdd/plan-a-task-{12,13,14}-report.md`）。

機序の旧本命（Flash 書込時の割込みブラックアウト＋CYW43 連携の状態機械破損、at-time alarm 再予約経路・低優先度 IRQ 処理の疑域）は上記により置換された。op-in 死（若い bank での S3-lockout 不応答）は別機序として未決・分離追跡中である。

> 🚧 In-progress: 恒久修正（quiesced flash worker）は設計のみであり未実装・未承認である。方針は「BT 動作中の Flash 変更を全面禁止し、dirty-flag＋安全条件付き単一 worker へ集約」である。ゲート条件（ACL=0・HID 切断・outgoing なし・pairing なし・L2CAP なし・TX 待ちなし）、固定排出順（鍵→host 削除→cap 削除→host→cap→色→有線）、`WIRED_MODE` 期限規則、失敗時意味論の全文は設計書を見ること (`docs/history/2026-09-15-wdt/plans/quiesced-worker-design.md:45-67,228-353`)。Stage 2（BT 停止→保存→再開）は将来課題であり、本設計は hook を残すのみである。

## Flash に関する禁止事項

- BT コールバック内に新規の書込経路を足さない。延期/quiesce し、HW で検証すること (`AGENTS.md:37`)
- `flash_safe_execute` を入れ子にしない（外側自前＋内側 HAL の二重包み禁止）。単一スロット共有のため静かに PRIMASK を置き去りにする。疑わしい場合は BCON の `pm=` と guard 行を見ること
- 秘密鍵バイトをログ・文書に出さない。`hci_dump` は一時診断限定である (`AGENTS.md:38`)
- 起動直後の wipe 削除（Flash 消去）は生存するが、BT 動作中の program のうち**入れ子のものは致命的**である。単発（HAL 内側のみ）の program は完走する。「コネクション有効時の書込がダメ」という旧整理は「入れ子がダメ」に置換された
- BTstack 由来の鍵保存セッションが生存した非対称（自前 host 保存は死ぬのに）は解決済み（U4 closure）：鍵保存は単発・host 保存は入れ子だったため。恒久 worker 設計では単発性の維持を前提とすること
