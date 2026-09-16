# P3 Task 6R brief: Revert grip colors to original defaults

Revert the Task 6 color change: L/R grips must differ for log pass/fail judgment, and the owner directs restoring the original colors. Exact reverse of Task 6, same TDD shape.

## Steps

- [ ] **Step 1: Restore the original test expectation**

In `tests/host/test_usb.c`, replace the stock-black expectation:

```c
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x32 && out[21] == 0x32 && out[22] == 0x32 &&
               out[23] == 0xFF && out[24] == 0xFF && out[25] == 0xFF &&
               out[26] == 0x46 && out[27] == 0x46 && out[28] == 0x46,
               "10 SPI color 6050 (stock black)");
```

with the original:

```c
         CHECK(n == 64 && out[13] == 0x90 && out[14] == 0x10 && out[19] == 12 &&
               out[20] == 0x82 && out[21] == 0x82, "10 SPI color 6050");
```

Run the host suite (vcvars64-ready shell, build Debug, `ctest -C Debug -V`).
Expected: FAIL on `10 SPI color 6050` ONLY (current tree still has Task 6 table). Record as RED evidence.

- [ ] **Step 2: Restore the original defaults**

In `src/proto/spi.c`, replace the Task 6 table + comment:

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

with the original (no comment):

```c
uint8_t spi_color_6050[13] = {
    0x82, 0x82, 0x82, 0x0F, 0x0F, 0x0F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};
```

Do NOT touch: byte 12, store/load, COLOR_SET path, `0x02` tail, `0x601B`, or any other file.

- [ ] **Step 3: Full host suite** — expected 3/3 ALL PASS. Record output.

- [ ] **Step 4: Wireless compile** (`cmake -S . -B $env:TEMP/p3t6r -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0`, `--target pico-bcon`). Expected exit 0. No BUMP flag.

- [ ] **Step 5: Leave changes in place.** Do NOT revert. Record hunks.

- [ ] **Step 6: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-6r-report.md` (RED + GREEN evidence with commands/outputs, build evidence, hunk description, self-review, concerns). DO NOT commit.
