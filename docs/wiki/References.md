# References — Provenance of every build-critical value

Private reference trees are **not required** to reproduce this work.
Every build-critical value below is traceable to a **public analysis repository or
public reverse-engineering document** named in this repo's own records, pinned by an
in-repo spec/code location, and (where stated) cross-checked on hardware.
Where the repo record names **no** public source for a value, it is listed
explicitly in the gap list instead of guessed.

Rule followed while writing this page: values were taken only from
`spec/protocol_v3.md`, `src/*/…` comments, `docs/history/2026-09-15-wdt/trial-history.md`,
`docs/wdt_phenomena_brief_20260915.md`, `docs/handoff_bt_20260914.md`, and the
plans/briefs they point to. No secret key bytes are recorded anywhere
(link keys / LTK never appear; peer BD_ADDR is OK).

## Note on ported code (honest history)

Several files carry `移植元: pico-wakecon` comments (`src/bt/hid.h:4`,
`src/bt/store.h:4`, `src/bt/switch_hid.h:4`, `src/bt/cap.c:2`,
`src/bt/bt_compat.h:1-2`, `src/main.c:3`): structure and scaffolding were
historically ported from that tree. This page's claim is narrower and still
holds: **no private-tree knowledge is required to verify any build-critical
value** — every value group in the table below has an independent public
analysis-repo source plus an in-repo pin and (where stated) a hardware
cross-check. An AI rebuilding equivalent firmware from this wiki + the cited
public repos + its own hardware verification does not need the private tree.

## Excluded candidates (ruled out in-repo, not used)

- Pokémon Automation Pico firmware (closed binary), HOJA library BT (stub),
  debugprobe (debug-probe fork): comparison target rejected
  (`docs/history/2026-09-15-wdt/trial-history.md:12`,
  `docs/history/2026-09-15-wdt/plans/2026-09-14-phase3-permanent.md:413`).
- Origin / cross-check stack kept: BTstack source + DavidPagels/retro-pico-switch
  origin + Wilstride + NXBT (`trial-history.md:13-14`,
  `phase3-permanent.md:413`); handshake cross-checked against
  dekuNukem / NXBT / SDL / Chromium documents (`docs/handoff_bt_20260914.md:33`).
- Black-box A/B witness role only: same Switch 2 used for bcon-vs-witness
  comparison logs (empty-report equivalence, SDP/descriptor/CoD/name/OUI/MTU/CID
  sameness) — comparison logs only, never code reuse
  (`docs/handoff_bt_20260914.md:11,31`, `docs/wdt_phenomena_brief_20260915.md:14,65-66`).

## Provenance table (8 rows)

| # | Value group | Public source (as named in-repo) | In-repo pin (spec / code) | HW cross-check (log / behavior) |
|---|---|---|---|---|
| R1 | USB VID:PID / descriptors / strings | ToadKing genuine dump copy (`src/usb/usb_descriptors.c:1-4`, `spec/protocol_v3.md:233-239`) | VID `057E` PID `2009`, USB 2.00, EP0 64B, `bcdDevice 0x0200` (`src/usb/usb_descriptors.c:13-28`); config 41B Remote-Wakeup 500mA HID 1IF IN `0x81`/OUT `0x01` 64B (`src/usb/usb_descriptors.c:140-157`, `_Static_assert` 41B at `:157`); HID report 203B input `0x30`/`0x21`/`0x81` output `0x01`/`0x10`/`0x80`/`0x82` (`src/usb/usb_descriptors.c:35-36`, `_Static_assert` 203B at `:132`); strings `Nintendo Co., Ltd` / `Pro Controller` / `000000000001` (`src/usb/usb_descriptors.c:173-178`, `spec/protocol_v3.md:239`) | Dock determination **pending**: `bcdDevice 0x0200` adopted from ToadKing wire bytes `0x00,0x02` (LE) with `0x0210` theory open (`src/usb/usb_descriptors.c:10-12,23`); finalize on dock verification (`spec/protocol_v3.md:234`) |
| R2 | HID report layouts + fixed processing bytes | knflrpn/2wiCC ControllerData compat (USB) + dekuNukem / GP2040-CE SwitchProDriver / NXBT example pairing session (BT) (`src/usb/usb_hid.h:3-6`, `src/usb/usb_hid.c:56-68,81-82`, `src/bt/hid.c:1-4`, `spec/protocol_v3.md:222-224`) | USB ControllerData 12B: timer, battery `0x91`, `btn[1]\|=0x80`, `btn[2]&=0xCF`, sticks 12-bit, vibration `0x09` (`src/usb/usb_hid.c:56-68`, tested at `tests/host/test_usb.c:154-166`); USB `0x30` 64B = ID + 12B + 36B IMU(0) + 15B pad (`src/usb/usb_hid.c:70-79`, `src/usb/usb_hid.h:33-35`); USB `0x21` 64B + `0x81` 64B zero-padded ID+63 (`src/usb/usb_hid.c:19-54,123-143`); BT `0x30` 14B `A1 30 timer 80 btn3 stick6 08` and `0x21` reply `A1 21 timer 80 btn3 stick6 08 ack sub …` padded to 50B (`src/bt/hid.c:131-145,148-179,411-416`); BT keeps raw 3B, battery `0x80`, vibration `0x08` (`spec/protocol_v3.md:224`) | AB1 victory log: SUB full run accepted by Switch 2 (`log/COM3_2026_09_14.22.32.18.050_ab1.txt`, `docs/history/2026-09-15-wdt/verification-status.md:14-19`); host test `[6] 30 input report layout` guards masks (`tests/host/test_usb.c:154-166`) |
| R3 | SPI flash map incl. `0x6050` / `0x601B` / `0x6000`–`0xFF` rule | NXBT pairing session + dekuNukem + CTCaer/jc_toolkit#28 (`src/proto/spi.h:4-7`); `0x601B` rule dekuNukem (`src/proto/spi.c:15-16`); `0x6020` IMU cal borrowed from 2wiCC working values (`src/proto/spi.c:53-55`); `0x6000` `0xFF` rule + 2wiCC serial-none operation (`spec/protocol_v3.md:240-242`, `src/usb/usb_hid.c:200-205,210-212`) | Table `0x6010`/16, `0x601B`/1 (`0x01`), `0x6000`/16, `0x6050`/13 mutable, `0x6080`/`0x6098`/`0x603D`/`0x6020`, `0x8010`/`0x8028`→`0xFF` (`src/proto/spi.c:62-76`); `0x601B=0x01` required or Switch ignores `0x6050` (`src/proto/spi.c:15-19`, `spec/protocol_v3.md:243-246`); USB answers `0x6000` range with `0xFF`, unknown `0x60xx` with `0xFF` fill, out-of-range no reply (`src/usb/usb_hid.c:180-216`); BT unknown/insufficient → no reply, never fake cal (`src/bt/hid.c:243-275`) | Switch 2 reads all 13B of `0x6050`, proven accepted in AB1 victory log (`plans/2026-09-14-phase3-permanent.md:297`); genuine-looking `0x6000` serial makes Switch 2 fall `2162-0002`, hence `0xFF` (`spec/protocol_v3.md:240-242`) |
| R4 | Handshake SUB order + `0x02` tail | dekuNukem / NXBT / SDL / Chromium documents cross-check (`docs/handoff_bt_20260914.md:33`); NXBT v12 diff (MAC mask, SDP cleanup, first-time slowdown) reflected into plan (`trial-history.md:14`); `0x02` tail `01 02` proven on Switch 2, NXBT-conformance alone no reason to touch (`plans/2026-09-14-phase3-permanent.md:345,412`) | BT SUB dispatch `0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x10,0x21,0x30,0x31,0x33,0x40,0x43,0x48,0x50` + default (`src/bt/hid.c:277-378`); `0x03` starts `0x30` full mode (`src/bt/hid.c:295-304`); `0x30` latches player lamp (`src/bt/hid.c:328-334`); device-info `fw 03 8B`, type `03` Pro, self addr, tail `01 02` (`src/bt/hid.c:201-215`); USB `80 04` starts input, `80 05`/unmount re-waits (`src/usb/usb_wired.c:217-221,315-322`, `spec/protocol_v3.md:249`); USB `0x02` tail `01 02` — `0x01` would leave Switch ignoring SPI colors (`src/usb/usb_hid.c:162-175`); USB `0x01`-pairing templates from 2wiCC `procon_data.c bt_data_01/02/03` (`src/usb/usb_hid.c:81-91`) | AB1 (SNIFF restored): SUB full run × 2 sessions, zero disconnects; AB5 (BUMP only, SNIFF off): open → no SUB → `0x13` × 4, SUB zero by grep (`verification-status.md:14-19`, AB1 log 680KB `…_ab1.txt`, AB5 log `…_ab5.txt`); hci_dump era proved SUB absent on wire (Switch sent nothing), not dropped (`verification-status.md:30-33`) |
| R5 | Rumble `0x10` encoding | dekuNukem `bluetooth_hid_notes.md` §OUTPUT `0x01`/`0x10` + §Rumble data, `rumble_data_table.md` amplitude table, Linux `hid-nintendo.c` (`joycon_rumble_amplitudes[]`, `struct joycon_rumble_output`) (`docs/history/2026-09-15-wdt/sdd/plan-a-task-2-report.md:10,44,50,75`) | v3 reserves `0x22` LEN2, no transmit; parser intake only, ACK + drop (`spec/protocol_v3.md:152-154`, `src/bt/hid.c:424-431`, `src/usb/usb_wired.c:297-300`); STATUS bit7 counts reception only (`src/main.c:378-382`); decode layout spike defined, formula fabrication forbidden (`docs/superpowers/specs/2026-09-15-uart-features-design.md:28-31`) | No HW yet: needs ≥1 non-zero `A2 10` line from hci_dump ACL rows; 3 payload kinds (neutral + non-zero) required before decoder work (`Roadmap.md` §1, `hw-batch-2026-09-15.md:20-24`); BTstack delivers report excluding report ID (`hid.c:394-395` reads `sub=report[9]`) — verified in-repo, not from logs |
| R6 | Grip / body colors | CTCaer/jc_toolkit `retail_colors.xml` ("Pro Black" / "Pro Black Buttons") + jc_toolkit issue #28 stock grips (`plans/2026-09-14-phase3-permanent.md:297,335-342`) | Layout body 3B + buttons 3B + left grip 3B + right grip 3B + spec byte = 13B; byte 12 stays `0x00` (`plans/2026-09-14-phase3-permanent.md:297`); current tree intentionally L/R split for verification (`src/proto/spi.c:21-27`); save 13B / restore 12B keeps spec byte (`src/bt/store.c:136-151`); `COLOR_SET` 12B RGB×4 (`spec/protocol_v3.md:243-244`, `src/proto/dispatch.c:76-80`) | Grips `464646` + R `FFFFFF` visually confirmed separated on Switch UI (`verification-status.md:60-62`); Switch caches colors at first connect — re-pair after change (`spec/protocol_v3.md:245-246`) |
| R7 | BT GAP / SDP / PnP params | DavidPagels/retro-pico-switch `SwitchConsts.h` descriptor bytes (MIT) (`src/bt/switch_hid.h:10-14`); VID/PID/CoD/OUI/name values per that origin + BTstack SDP/HID setup (`src/main.c:856-862,955-984`); NXBT v12 MAC-mask/SDP-cleanup diff noted (`trial-history.md:14`) | `SWITCH_VENDOR_ID 0x057E`, `SWITCH_PRODUCT_ID 0x2009`, version `0x0001`, CoD `0x2508`, OUI `7c:bb:8a`, GAP `Pro Controller`, HID `Wireless Gamepad` (`src/bt/switch_hid.h:59-76`); stable MAC = OUI + board unique low 3B, never bumped (`src/bt/link_conn.c:13-24`); GAP class/name, SNIFF+role-switch policy, SSP no-IO auto-accept (`src/main.c:957-965`); HID + PnP (USB-source) SDP records (`src/main.c:971-984`); truncated-report accept + report callback (`src/main.c:986-989`) | SDP/descriptor/CoD/name/OUI/MTU/CID differences vs witness rejected with evidence (`docs/wdt_phenomena_brief_20260915.md:65-66`); stable-MAC-only (BUMP without SNIFF) proven powerless AB5 0/4 (`trial-history.md:17`); SNIFF-on + stable MAC is the proven combination (`src/bt/link_conn.c:13`) |
| R8 | bInterval / polling / tick rates | ToadKing copy for `bInterval 8`, no shortening for latency (`spec/protocol_v3.md:170,234-236`, `src/usb/usb_descriptors.c:140-154`); BT empty/full pacing per materials (`src/bt/hid.c:171` "materials procedure", `src/bt/hid.c:123-126`); NXBT-style slow-start tried then explicitly dropped as unnecessary (`plans/2026-09-14-phase3-permanent.md:411`) | USB IN/OUT 64B interval 8 (`src/usb/usb_descriptors.c:149-154`); wired input send every 8 ms, arrival-immediate update on Core0 pull side (`src/usb/usb_wired.c:26-27,150-164`); BT `probe_send_interval_ms` full 7 ms / pre-full 100 ms empty `A1 00` (`src/bt/hid.c:123-126,148-179`); Core0 `poll_tick` 1 ms + stats 1 s + empty timer (`src/main.c:1003-1013`, `src/main.c:464-561`); UART 1 Mbps default, derated 115200 build flag (`spec/protocol_v3.md:29`, `src/main.c:51-56`) | AB1 victory used 100 ms empties — slow-start unnecessary (`phase3-permanent.md:411`); low latency via arrival-immediate report update, not shorter `bInterval` (`spec/protocol_v3.md:170`) |

## Gap list (repo names no source — do not guess)

- G1 — `bcdDevice 0x0200` vs `0x0210` theory: repo records the conflict and defers to dock verification (`src/usb/usb_descriptors.c:4,10-12,23`, `spec/protocol_v3.md:233-234`). Left open by design.
- G2 — HoriCon / Joy-Con real IDs and personality rows: out of scope; "measure then add one table row" repeats, no values in-repo (`Roadmap.md` §4). Not written here.
- G3 — Rumble amplitude map `ampL/R = f(raw8)`: no in-repo formula; needs real non-zero `0x10` capture first (`Roadmap.md` §1). Not written here.
- G4 — `0x6020` IMU calibration exact bytes: per-unit values differ; current table borrows 2wiCC working values as placeholder, `0xFF` fill is second candidate (`src/proto/spi.c:53-60`). Not a proven genuine dump.
- G5 — OUI `7c:bb:8a` origin document: repo records only the empirical note (own Pico address `88:A2:9E:…` was not picked up) plus stable-MAC proof (`src/bt/switch_hid.h:66-72`, `src/bt/link_conn.c:13-24`, AB5 result). No upstream doc named — listed as HW-derived.
- G6 — BT `0x02` device-info `fw 03 8B` (BT, `src/bt/hid.c:201-215`) vs USB `0x02` `fw 03 48` (USB, `src/usb/usb_hid.c:162-175`): both marked as working values in code; repo does not name a single upstream doc reconciling the difference. Listed as HW-accepted, not doc-derived.
