// bt_compat.h -- wakecon ui.c 由来 probe_line の互換shim。
// BT移植モジュール (hid/link_*) のログ出力先。実体は main.c (UART0 printf)。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void probe_line(const char *s);

#ifdef __cplusplus
}
#endif
