# P3 Task 2 brief: Dedupe host save (replaces open-time write storm)

`store_host()` writes flash only when the address actually changed; re-enable the open-time call. Change is PERMANENT (stays in tree). The tree already contains Task 1's SNIFF hunk — do not touch it.

## Steps

- [ ] **Step 1: Read the current code**

Read `src/bt/store.c` `store_host()` and `src/main.c` `handle_hid_meta` `HID_SUBEVENT_CONNECTION_OPENED` success branch (currently RAM-only with the diagnostic comment + `// store_host(a);`).

- [ ] **Step 2: Guard `store_host` + count saves (exact code)**

In `src/bt/store.c`, replace the body of `store_host` with:

```c
static uint32_t s_host_save_count;

void store_host(bd_addr_t addr)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    if (probe_host_known && memcmp(probe_host_addr, addr, 6) == 0) {
        return; /* unchanged: no flash wear, no timer risk */
    }
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    if (tag_store_safe(tlv, ctx, TAG_HOST, addr, 6) != 0) {
        return;
    }
    memcpy(probe_host_addr, addr, 6);
    probe_host_known = true;
    s_host_save_count++;
    {
        char msg[48];
        snprintf(msg, sizeof(msg), "host saved (n=%lu)",
                 (unsigned long)s_host_save_count);
        probe_line(msg);
    }
}
```

Requirements: `memcmp` needs `string.h` (already included in store.c — verify); `probe_line` must be visible (via already-included `link.h`/`bt_compat.h` — verify, do not add duplicate includes silently: if `snprintf` needs `stdio.h`, add `#include <stdio.h>` and report it). No other changes in store.c.

- [ ] **Step 3: Re-enable the open-time call (exact replacement)**

In `src/main.c`, replace:

```c
                // Task 4診断用の一時措置: flash保存を止めRAMのみ。
                // open時TLV書込とタイマ死亡の因果切り分け (書込嵐の停止も兼ねる)。
                // store_host(a);
                memcpy(probe_host_addr, a, 6);
                probe_host_known = true;
                snprintf(msg, sizeof(msg), "hid open. host cached (no flash)");
                probe_line(msg);
```

with:

```c
                store_host(a);
                snprintf(msg, sizeof(msg), "hid open. host %s saved",
                         bd_addr_to_str(a));
                probe_line(msg);
```

(`bd_addr_to_str` is already used in main.c. The `memcpy` fallback is deleted — `store_host` does it.)

- [ ] **Step 4: Compile the wireless build**

```powershell
cmake -S . -B $env:TEMP/p3t2 -G Ninja -DPOC_DATA_BAUD=115200 -DWIRED_DEFAULT=0 -DBD_ADDR_BUMP=1
cmake --build $env:TEMP/p3t2 --target pico-bcon
```

Toolchain discovery as in Task 1 (SDK cmake/ninja full paths if needed). Never reuse `build/`. Allow >= 600000 ms. Expected: exit 0.

- [ ] **Step 5: Leave the change in place + record hunks**

Do NOT revert. Record which `git diff` hunks are Task 2's (store.c guard + main.c call-site) vs earlier state.

- [ ] **Step 6: Write the 5-line flash/test note** (owner executes hardware later)

Include: UF2 location, Switch prep (pair once → expect exactly one `host saved (n=1)`; reboot Pico → boot shows stored host with NO new save line; reconnect uses stored host), pass/fail signals.

- [ ] **Step 7: Report** — write `C:\Users\moilo\AppData\Local\Temp\opencode\bcon-phase3\task-2-report.md` (implementation incl. any include added, build command + exit code, hunk description, flash/test note, self-review, concerns). DO NOT commit.
