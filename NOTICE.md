# NOTICE — License scope and third-party references (switch-bcon)

This file explains, in human terms, which licence applies to which part.
It is not legal advice. The binding texts are `LICENSE` (PolyForm
Noncommercial 1.0.0, this repo's own files) and the verbatim copies
under `LICENSES/`.

## 1. Scope: what the PolyForm Noncommercial License covers

Source code authored specifically for this repository is licensed under
the PolyForm Noncommercial License 1.0.0 (`LICENSE`) unless otherwise
noted: noncommercial use, modification and distribution are permitted;
commercial use requires a separate licence from the author. This includes
`src/`, `tests/host/` (self-authored, BTstack-independent pure C),
`spec/`, build files, and docs.

## 2. Third-party components (NOT MIT — supplied separately via the SDK)

This repository does not include third-party code. At build time it
depends on Raspberry Pi Pico SDK 2.3.0, which supplies:

- **BTstack** (BlueKitchen GmbH) — terms: `LICENSES/BTstack.txt`
  (base licence, incl. its clause 4) **and**
  `LICENSES/BTstack-Raspberry-Pi-Supplemental.txt` (the Raspberry Pi
  supplemental grant). The supplemental grant names Pico W, Pico WH,
  **Pico 2 W**, Pico 2 WH and RM2 as covered Products and permits using,
  integrating and distributing BTstack as part of those Products or
  Customer Products derived from them. This project's Bluetooth firmware
  targets **Raspberry Pi Pico 2 W only** and is intended to stay inside
  that hardware scope.
- **Raspberry Pi Pico SDK itself** (incl. TinyUSB via the SDK) — terms:
  `LICENSES/Raspberry-Pi-Pico-SDK.txt`. The SDK is an external build
  dependency, not redistributed here.
- The `LICENSES/` copies were taken from the exact SDK build revision
  (2.3.0), not from SDK master.

Consequence for binaries: a linked UF2 contains third-party components,
so "this firmware is PolyForm licensed" would be wrong. The correct
statement is: *the original source of this project is licensed for
noncommercial use; the firmware binary includes third-party components
under their own terms* (see the release notice in §5).

## 3. Project policy (current, not a perpetual pledge)

This is a personal, non-commercial, open-source project. Commercial use
of this repository's own files is not permitted, and no separate
commercial licence is offered: no sales, no paid support, no
pre-flashed hardware sales, no commissioned development.
If that policy ever changes, the BTstack supplemental terms, sales form,
Nintendo identifiers and hardware regulations will be re-evaluated
first.

## 4. Disclaimer (unofficial project)

This project is an independent, unofficial open-source project. It is
not affiliated with, endorsed by, sponsored by, or approved by Nintendo.
Nintendo Switch and Pro Controller are trademarks of Nintendo. All
trademarks are the property of their respective owners. The repository
does controller emulation for compatibility testing — it does not claim
to be a genuine Pro Controller (it does not use the HORIPAD name either).

Japanese: 本プロジェクトは個人が開発する非公式のオープンソースプロジェクト
です。任天堂株式会社またはその関連会社との提携、後援、承認その他の関係は
ありません。Nintendo SwitchおよびPro Controllerなどの名称は、それぞれの
権利者に帰属します。

## 5. Release notice text (attach to UF2 releases)

```text
### Third-party software notice

This UF2 binary includes BTstack supplied through the Raspberry Pi Pico SDK.

The binary is intended only for Raspberry Pi Pico 2 W and is provided as
part of a personal, non-commercial open-source project.

The PolyForm Noncommercial License applies only to the original portions
of this project. Third-party components remain subject to their
respective licence terms. See `LICENSES/` and `NOTICE.md`.
```

## 6. Code provenance in this tree (reference vs copy)

- **Referenced only (no code copied):** Switch controller protocol behaviour
  was implemented against public reverse-engineering documentation, notably
  dekuNukem/Nintendo_Switch_Reverse_Engineering and the NXBT/SDL/Chromium
  materials cross-checked during development. Per-value provenance is tracked
  in `docs/wiki/References.md` (table R1–R8) with explicit gaps (G1–G6)
  where no public source is named — ideas re-implemented independently do
  not inherit the reference's licence, but verify terms upstream if you copy
  text, tables or descriptors verbatim.
- **Ported scaffolding:** several files carry `移植元: pico-wakecon`
  comments (the author's own separate reference tree:
  `src/bt/hid.*`, `src/bt/store.*`, `src/bt/switch_hid.h`, `src/bt/cap.c`,
  `src/bt/bt_compat.h`, parts of `src/main.c`). They are distributed under
     this repository's PolyForm Noncommercial Licence by owner intent. The public analysis repos
  above remain sufficient to verify every value independently
  (`docs/wiki/References.md` states this explicitly).
- **Upstream-licensed excerpts believed present:** `src/bt/switch_hid.h`
  descriptor bytes attributed to the retro-pico-switch origin. If you
  redistribute, confirm the upstream file's terms in its home repository;
  the pin is recorded so the check is one lookup, not archaeology.
- If you find copied third-party code in this tree whose origin is not
  recorded here, treat §1 as not covering it and open an issue noting:
  source repository, file name, commit hash, copy date, original licence,
  and what was changed.
