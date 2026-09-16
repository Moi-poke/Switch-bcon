# Plan A Task 2 Report — Rumble 0x10 Layout Spike

Status: **BLOCKED (vectors missing; mapping defined, awaiting one non-zero HW capture)**
Date: 2026-09-15 · Worker: Plan A Task 2 · Read-only (no repo file touched)

## 1. Method

- Searched all `C:\pico-bcon\log\COM3_*.txt` for `A2 10` (Switch→Pico HID-output on the HID-Interrupt CID): **762 hits, 5 files**.
- Grouped the bytes after `A2 10`; grouped `A2 01` embedded-rumble bytes as a cross-check (129 hits).
- Reference: dekuNukem `bluetooth_hid_notes.md` §OUTPUT 0x01/0x10 + §Rumble data, `rumble_data_table.md` amplitude table, and Linux `hid-nintendo.c` (`joycon_rumble_amplitudes[]`, `struct joycon_rumble_output { u8 output_id; u8 packet_num; u8 rumble_data[8]; }`).
- FW side: `src/bt/hid.c:377-433` + registration `src/main.c:979` (`hid_device_register_report_data_callback`).

## 2. Distinct 0x10 payloads found (ALL neutral — 1 distinct rumble payload)

Raw bytes after `A2 10` are always 9 bytes = **1 counter byte + 8 rumble bytes**.
The 8 rumble bytes are identical in all 762 lines:

```
neutral rumble8 = 00 01 40 40 | 00 01 40 40   (left motor | right motor)
```

This exactly matches the reference neutral `[00 01 40 40 00 01 40 40] (320 Hz 0.0 / 160 Hz 0.0)`.
The counter byte cycles `0x00–0x0F` (counts per file: 46–50 each, 16 variants, total 762).
`A2 01` cross-check: embedded rumble is either all-`00` (110×, pre-handshake filler) or neutral (19×) — **zero non-zero rumble bytes anywhere in logs**.

| # | File | Line | ACL tail (after `A2 10`) | Counter | Rumble8 |
|---|------|------|--------------------------|---------|---------|
| 1 | `log/COM3_2026_09_14.22.32.18.050_ab1.txt` | 417 | `0F 00 01 40 40 00 01 40 40` | `0F` | neutral |
| 2 | `log/COM3_2026_09_14.22.32.18.050_ab1.txt` | 452 | `02 00 01 40 40 00 01 40 40` | `02` | neutral |
| 3 | `log/COM3_2026_09_14.22.32.18.050_ab1.txt` | 759 | `00 00 01 40 40 00 01 40 40` | `00` | neutral |
| 4 | `log/COM3_2026_09_15.01.59.21.411.txt` | 349 | `04 00 01 40 40 00 01 40 40` | `04` | neutral |
| 5 | `log/COM3_2026_09_15.02.06.03.677.txt` | 487 | `03 00 01 40 40 00 01 40 40` | `03` | neutral |
| 6 | `log/COM3_2026_09_15.02.42.39.163.txt` | 354 | `00 00 01 40 40 00 01 40 40` | `00` | neutral |
| 7 | `log/COM3_2026_09_15.02.43.25.327.txt` | 510 | `0B 00 01 40 40 00 01 40 40` | `0B` | neutral |

Per-file `A2 10` counts: ab1 = 207, `01.59.21.411` = 445, `02.06.03.677` = 63, `02.42.39.163` = 46, `02.43.25.327` = 1.
FW correlation: each first-intake e.g. ab1:417 (ACL) → ab1:418 (`RUMBLE 0x10 intake`) confirms this is the Switch→Pico 0x10 path. No secret key bytes involved.

## 3. Byte offsets

ACL line token map (example `ACL <= 0B 20 0F 00 0B 00 42 00 A2 10 0F 00 01 40 40 00 01 40 40`):

- `0B 20` HCI handle+flags · `0F 00` ACL len = 15 · `0B 00` L2CAP len = 11 · `42 00` CID = HID-Interrupt · `A2` HID-DATA · `10` report ID
- Byte after `10` = packet counter (`0x0–0xF`, matches dekuNukem `GlobalPacketNumber`)
- Next 8 = rumble payload: `L[0..3] R[0..3]` = hex tokens 13–20 after `A2 10` + 1 counter byte.
- Per-motor layout (reference): `mb[0]` HF-freq-lo · `mb[1]` bit0 = HF-freq-hi, bits7..1 = **HF amplitude** (even `0x00–0xC8`, 101 steps) · `mb[2]` bit7 = LF-intermediate flag, bits6..0 = LF-freq · `mb[3]` LF amplitude (`0x40–0x72`).

## 4. FW buffer semantics — off-by-one warning for Task 4

`hid_device_register_report_data_callback` delivers the report **excluding the report ID** (proven in-repo: `hid.c:394-395` reads `sub = report[9]` for report 0x01, i.e. `[0]` = counter, `[1..8]` = rumble, `[9]` = subcmd — exactly dekuNukem's ID-inclusive `buf` minus `buf[0]`).
Therefore for 0x10: `report[0]` = counter, rumble8 = `report[1..8]`, `report_size` = 9.
**Task 4's sketch `rumble_decode_010(report, …)` with `report_size >= 8` is off by one — it must pass `&report[1]` with a `report_size >= 9` guard**, else the counter becomes `L[0]` and R drops its last byte.

## 5. Mapping formula (defined against reference, neutral verified from logs)

For each motor block `M[0..3]` (L = rumble8[0..3], R = rumble8[4..7]):

```
idx = (M[1] & 0xFE) >> 1          // 0..100 nominal (LSB is a freq bit, must be masked)
t   = (idx * 255 + 50) / 100      // integer, round-half-up
amp = t > 255 ? 255 : t           // clamp (out-of-spec bytes >0xC8)
```

Neutral handling: `M[1] = 0x01` → `& 0xFE = 0x00` → **amp = 0** (verified against all 762 log lines). NULL `rep` → both 0 (per Task 3 contract).
Caveat: the Switch amplitude encoding is logarithmic, so this index-linear scale compresses the perceptual top end vs the kernel milli-table (e.g. table step 8 = 33/1003 → perceptual ~8/255, formula gives 20). Chosen because the plan asks for a scaled high-amp field; fine for change-detect telemetry, not a loudness meter.

## 6. Vectors (raw8 → ampL, ampR)

Real-log (the ONLY distinct payload in evidence):

| # | raw8 | ampL | ampR | source |
|---|------|------|------|--------|
| V0 (neutral) | `00 01 40 40 00 01 40 40` | 0 | 0 | 762 log lines, e.g. ab1:417, 01.59:349, 02.43:510 |

Reference-derived (kernel/dekuNukem amplitude rows — **NOT from logs**, for Task 3 test scaffolding only):

| # | raw8 | ampL | ampR | basis |
|---|------|------|------|-------|
| R1 | `00 02 80 40 00 02 80 40` | 3 | 3 | table row {0x02, 0x8040} → idx1 → (255+50)/100 = 3 |
| R2 | `00 10 00 44 00 10 00 44` | 20 | 20 | table row {0x10, 0x0044} → idx8 → (2040+50)/100 = 20 |
| R3 | `00 C8 00 72 00 01 40 40` | 255 | 0 | max row {0xC8, 0x0072} → idx100 → 255; R neutral → 0 (asymmetry check) |

## 7. Verdict: BLOCKED — what is missing

- **Have:** byte layout + counter semantics + neutral pattern (log-proven) + formula + reference cross-check. Real-log vector count = **1** (`00 01 40 40 00 01 40 40`); neutral bytes confirmed.
- **Missing:** the plan-mandated **≥3 distinct real-log vectors including one non-zero**. All 762 captures are idle/neutral; no session in `log/` exercised actual vibration.
- **To unblock:** one HW run that makes the Switch emit real rumble (e.g. System Settings → Controllers and Sensors → Test Input Devices → Test Vibration, or any game rumble), capture `log/COM3_*.txt` with hci_dump on, re-run the §1 search. One non-zero `A2 10` line is enough to confirm byte positions, then Task 3 proceeds with V0 + the new sample (+ R1–R3 as scaffolding).
- **Do NOT invent** non-zero "log" vectors. If the owner accepts reference-derived R1–R3 as stand-ins, say so explicitly and Task 3 may proceed as READY-FOR-TASK-3 under that waiver.
