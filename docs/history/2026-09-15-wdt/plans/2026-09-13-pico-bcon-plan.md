# pico-bcon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** バイナリ専用・最速・公式ProCon認識の単一UF2（USB＋Classic BT＋wake再生）を新規 `C:\pico-bcon` に構築する。

**Architecture:** protocol v3（u32 VIIPER・LEN8・CONFIG面）をSSOTにし、USBはwakecon写し（057E:2009・bInterval 8）、BT/BLE wakeはwakecon移植、コア構成はdual-core PoC合否で確定（不合格時はsingle-core＋1ms poll）。

**Tech Stack:** C11 / Pico SDK / BTstack Classic+BLE / CYW43 / TinyUSB / Python PC sender / CTest host tests.

**Spec:** `spec/protocol_v3.md`（PROTO_VER=3）

## Global Constraints

- boardは`pico2_w`固定。成果物は`build/`と`build-verify/`（git管理外）。
- 秘密（LTK/IRK/AES鍵）をログに出さない。
- USB記述子は実機写し（bInterval 8）。変更時は実機再検証。
- hostテストはvcvars64経由cmd＋フルパスctest。
- Switch 2 BLE入力は対象外。

---

### Task 1: protocol v3 基盤（DONE足場）

**Files:**
- Modify: `src/proto/protocol.h`, `src/proto/protocol.c`
- Test: `tests/host/test_protocol.c`, `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `spec/protocol_v3.md`
- Produces: `crc8()`, `proto_expected_len()`, `parser_init/feed/feed_buf()`, `frame_build()`（len>32で0）

- [x] **Step 1: spec v3策定** — STATE u32・CONFIG・STATUS・RUMBLE予約を固定
- [x] **Step 2: protocol.h/c実装** — LEN厳密・1Bスライド・mod256 SEQ・errcode保持
- [ ] **Step 3: hostテスト実行**

Run: `ctest --test-dir build-host -V`（vcvars64済みcmd）
Expected: `ALL PASS (0 failures)`

- [ ] **Step 4: Commit**

```bash
git init
git add README.md .gitignore spec/protocol_v3.md src/proto/ tests/host/ CMakeLists.txt
git commit -m "feat: protocol v3 base (u32 STATE, CONFIG, host tests)"
```

### Task 2: dual-core PoC（合否基準つき）

**Files:**
- Create: `src/poc_dualcore/main.c`, `docs/poc_dualcore_result.md`
- Test: 目視＋負荷ログ（再接続＋取込＋1kHz STATE＋TLV書込の同時負荷でハング・欠落なし）

**Interfaces:**
- Consumes: Task 1 parser（Core1はUART DMA排出＋parser＋mutex pushのみ、Flash/USB/BT禁止）
- Produces: 合否判定（合格＝dual-core採用、不合格＝single-core＋1ms pollへ）

- [ ] **Step 1: PoCファーム作成** — Core0＝BTstack＋TinyUSB＋CYW43（threadsafe_background検討）、Core1＝UART DMA＋parser。Core1で`multicore_lockout_victim_init()`、全Flash書込は`flash_safe_execute`経由
- [ ] **Step 2: 負荷試験** — 上記同時負荷10分。判定：ハング0・STATE欠落がSEQ統計のみで説明可能・TLV破損0
- [ ] **Step 3: 結果記録** — `docs/poc_dualcore_result.md`に合否と選択を記録
- [ ] **Step 4: Commit**

### Task 3: 公式ProCon USB移植

**Files:**
- Create: `src/usb/*`（wakecon `usb_hid.c`/`usb_descriptors.c`相当の写し＋帰属注記）
- Modify: `src/main.c`（64B report・handshake・unmount中立化）
- Test: `tests/host/test_usb.c`（移植）

**Interfaces:**
- Consumes: Task 1 `ctrl_state_t`（u32→`btn[0..2]`＋12bit stick pack）
- Produces: `usb_build_81/21_reply()`、`usb_build_30_report()`、`handshake_done`

- [ ] **Step 1: 写像表の固定** — u32→BT 3B／u32→USB `btn[0..2]`対応表をspec追記
- [ ] **Step 2: 記述子移植** — 203B・Config 41B・057E:2009・bInterval 8・文字列。SPI色・シリアル0xFF（2162-0002回避）を含む
- [ ] **Step 3: ドック検証** — Switch 2ドックで`cfg=1 hs=1`
- [ ] **Step 4: Commit**

### Task 4: CONFIG面＋BT/BLE wake移植

**Files:**
- Create: `src/bt/*`（wakecon `link*.c`/`hid.c`/`cap.c`/`spi.c`/`store.c`相当）
- Modify: `src/main.c`（初期化順はwakecon踏襲、UARTはバイナリ専用1Mbpsへ置換）
- Test: `tests/host/test_config.c`、`test_cap.c`移植

**Interfaces:**
- Consumes: Task 2 コア構成、Task 3 USB
- Produces: CAPTURE/BEACON/COLOR/KEY/WIRED/STATUS_REQ処理、TLV保存、再接続5s・電波quiet管理

- [ ] **Step 1: CONFIG受信実装** — 範囲検証＋ERRCODE（0x10-0x1F）＋UNSUPPORTED後STATE拒否
- [ ] **Step 2: RUMBLE受け口** — 出力受信→ACK＋破棄、カウンタ＋STATUS bit（将来0x22送出へ）
- [ ] **Step 3: 実機検証** — Switch 1ペアリング・再接続・wake C/B・PING/PONG RTT
- [ ] **Step 4: Commit**

### Task 5: PC送信ラッパ＋リリース

**Files:**
- Create: `pc/bcon_send.py`（frame_build・u32写像・Y反転・差分＋60Hz・単一write・方向別SEQ・HELLO・FTDI 1ms）
- Test: 生成バイト列が`frame_build` C実装と一致（ゴールデン比較）

- [ ] **Step 1: ラッパ実装**
- [ ] **Step 2: C/Python一致テスト**
- [ ] **Step 3: build-verifyクリーン確認＋`C:\pico-bcon`リリース手順記録**
- [ ] **Step 4: Commit**
