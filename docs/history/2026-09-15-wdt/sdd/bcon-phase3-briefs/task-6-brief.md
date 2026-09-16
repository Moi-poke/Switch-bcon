# P3 Task 6 brief: Authentic grip colors, test-first (TDD)

Change `spi_color_6050` defaults to stock black Pro Controller values. Test FIRST (RED), then implement (GREEN). Change is PERMANENT. Tree contains Tasks 1–5 hunks — do not touch them.

## Steps

- [ ] **Step 1 (RED): Update the test expectation first**

In `tests/host/test_usb.c`, replace:

```c
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x82 && out[21] == 0x82, "10 SPI color 6050");
```

with:

```c
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x32 && out[21] == 0x32 && out[22] == 0x32 &&
               out[23] == 0xFF && out[24] == 0xFF && out[25] == 0xFF &&
               out[26] == 0x46 && out[27] == 0x46 && out[28] == 0x46,
               "10 SPI color 6050 (stock black)");
```

Run the host suite (vcvars64-ready shell: `cmake -S tests/host -B build-host` if needed, build Debug, `ctest -C Debug -V`).
Expected: FAIL on `10 SPI color 6050` ONLY (all other tests pass). Record the failing output as RED evidence. (The USB path shares the same `spi_color_6050` table via `spi_find`, so this test guards both USB and BT.)

- [ ] **Step 2 (GREEN): Change the defaults**

In `src/proto/spi.c`, replace:

```c
uint8_t spi_color_6050[13] = {
    0x82, 0x82, 0x82, 0x0F, 0x0F, 0x0F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
```

with:

```c
/* Stock black Pro Controller (jc_toolkit conformance):
 * body #323232 (retail "Pro Black"), buttons #FFFFFF ("Pro Black Buttons"),
 * grips #464646 (stock grips per jc_toolkit#28; newer Switch FW applies a
 * strict color-decision algorithm). Byte 12 is a spec value, never restored. */
uint8_t spi_color_6050[13] = {
    0x32, 0x32, 0x32, 0xFF, 0xFF, 0xFF,
    0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x00
};
```

Do NOT touch: byte 12, `store_color`/`store_color_load`, the 12B PC-driven COLOR_SET path (`dispatch.c`, `main.c` FX_COLOR_SET), the `0x02` reply tail `01 02` (proven working), `0x601B = 0x01`.

- [ ] **Step 3 (GREEN proof): Run the full host suite**

Expected: 3/3 ALL PASS. Record output.

- [ ] **Step 4: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/p3t6 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0
cmake --build $env:TEMP/p3t6 --target pico-bcon
```

No `-DBD_ADDR_BUMP` flag (mechanism deleted in Task 5 — if the flag is still accepted, report it as a finding). Expected: exit 0.

- [ ] **Step 5: Leave changes in place + record hunks** (test file + spi.c). Do NOT revert.

- [ ] **Step 6: Write the 5-line flash/test note** (owner executes hardware later)

Include: UF2 location; pair → `0x6050` SPI read in handshake carries the new bytes (verify in log); Switch UI shows black controller; handshake still completes with no `0x13`; pass/fail signals.

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-6-report.md` (RED evidence + GREEN evidence with commands/outputs, implementation, builds, hunk description, flash/test note, self-review, concerns). DO NOT commit.
