#include <stdio.h>
#include <string.h>

#include "pico/flash.h"

#include "btstack_tlv.h"
#include "cap.h"
#include "spi.h"
#include "link.h"
#include "store.h"
#include "bt_compat.h"

#define TAG_HOST 0x4243484Fu  /* 'BCHO' */
#define TAG_COLOR 0x4243434Cu /* 'BCCL' */
#define TAG_CAP 0x42435731u   /* 'BCW1' */
#define TAG_WIRED 0x42435752u /* 'BCWR' */
/* 注意: wakeconとは別名前空間にする。同一Picoで共存しBT MACも同一導出の
 * ため、NXxx系TAGを共有するとwakecon保存値 (特にWIRED) を拾ってしまう。
 * Classicリンク鍵 (BTstack管理) は同一機器として共有するのが正しいため
 * 対象外。 */

static bool get_tlv(const btstack_tlv_t **t, void **c)
{
    btstack_tlv_get_instance(t, c);
    return *t != NULL;
}

/* 書込opを flash_safe_execute のcallbackで包む。BTstack TLVは生の
 * flash_range_* を呼ぶため、dual-coreではCore1のlockoutが要る。
 * 読込はXIP直読のため包まない (起動時Core1起動前に呼ぶ事)。 */
typedef struct {
    const btstack_tlv_t *tlv;
    void *ctx;
    uint32_t tag;
    const uint8_t *data;
    uint32_t len;
    int rc;
} tag_store_op_t;

static void tag_store_fn(void *param)
{
    tag_store_op_t *op = (tag_store_op_t *)param;
    op->rc = op->tlv->store_tag(op->ctx, op->tag, op->data, op->len);
}

typedef struct {
    const btstack_tlv_t *tlv;
    void *ctx;
    uint32_t tag;
} tag_delete_op_t;

static void tag_delete_fn(void *param)
{
    tag_delete_op_t *op = (tag_delete_op_t *)param;
    op->tlv->delete_tag(op->ctx, op->tag);
}

/* Death-B恒久対策の保険 (出荷用・診断ではない)。
 * 本FWでは全store呼出元がIRQ許可状態で動くため、復帰時にPRIMASK!=0なら
 * リークと断定して大声 (log)＋自己修復 (cpsie i) する。呼び出し元が禁止
 * 状態だった場合 (pm_entry!=0) は触らない。 */
static uint32_t s_irq_leak_healed;

static uint32_t irq_pm_rd(void)
{
    uint32_t r;
    __asm volatile ("mrs %0, primask" : "=r" (r) ::);
    return r;
}

static void store_irq_guard(uint32_t pm_entry)
{
    if (pm_entry == 0u && irq_pm_rd() != 0u) {
        char msg[40];
        s_irq_leak_healed++;
        snprintf(msg, sizeof(msg), "irq leak healed (n=%lu)",
                 (unsigned long)s_irq_leak_healed);
        probe_line(msg);
        __asm volatile ("cpsie i" ::: "memory");
    }
}

static int tag_store_safe(const btstack_tlv_t *tlv, void *ctx, uint32_t tag,
                          const uint8_t *data, uint32_t len)
{
    tag_store_op_t op;
    op.tlv = tlv;
    op.ctx = ctx;
    op.tag = tag;
    op.data = data;
    op.len = len;
    op.rc = -1;
    /* NOTE (Death-B恒久対策 2026-09-16): 外側flash_safe_executeでは包まない。
     * SDK pico_flashはコア毎に単一スロットirq_state[]でPRIMASKを退避する
     * (sdk src/rp2_common/pico_flash/flash.c:106,203,210)。外側の中からHAL
     * 内側のflash_safe_executeを呼ぶ入れ子では内側enterが共有スロットを上書
     * きするため、外側exitがPRIMASK=1を復元して全storeがIRQ禁止を置き去りに
     * する (task-13の計装runでpm=1を3/3実測。thread-modeの1ms loopは回るがtimer dispatch
     * が止まり約2秒後WDT=Death-B)。HALが全mutationを内側で包む (erase :82、
     * page program :170、delete-via-zero、readはXIP直読 — 9b safety case) の
     * で外側は冗長かつ有害。単発(single-level)を不変条件とする。 */
    uint32_t pm0 = irq_pm_rd();
    tag_store_fn(&op); /* 直接呼出し (入れ子禁止) */
    store_irq_guard(pm0);
    return op.rc;
}

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

void store_host_forget(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    tag_delete_op_t op;
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    op.tlv = tlv;
    op.ctx = ctx;
    op.tag = TAG_HOST;
    uint32_t pm0 = irq_pm_rd();
    tag_delete_fn(&op); /* 直接呼出し (tag_store_safe NOTE: 外側flash_safe_execute禁止) */
    store_irq_guard(pm0);
    probe_host_known = false;
}

bool store_host_load(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    if (!get_tlv(&tlv, &ctx)) {
        return false;
    }
    if (tlv->get_tag(ctx, TAG_HOST, probe_host_addr, 6) != 6) {
        return false;
    }
    probe_host_known = true;
    return true;
}

void store_color(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    (void)tag_store_safe(tlv, ctx, TAG_COLOR, spi_color_6050, 13);
}

void store_color_load(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t buf[13];
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    if (tlv->get_tag(ctx, TAG_COLOR, buf, 13) != 13) {
        return;
    }
    memcpy(spi_color_6050, buf, 12);  /* 13B 目は仕様値のため戻さない */
}

bool store_cap_save(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t blob[CAP_BLOB_SIZE];
    if (!get_tlv(&tlv, &ctx)) {
        return false;
    }
    if (!cap_encode(&probe_cap_saved, blob)) {
        memset(blob, 0, sizeof(blob));
        return false;
    }
    if (tag_store_safe(tlv, ctx, TAG_CAP, blob, (uint32_t)sizeof(blob)) != 0) {
        memset(blob, 0, sizeof(blob));
        return false;
    }
    memset(blob, 0, sizeof(blob));
    return true;
}

bool store_cap_load(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t blob[CAP_BLOB_SIZE];
    uint32_t got;
    if (!get_tlv(&tlv, &ctx)) {
        return false;
    }
    memset(blob, 0, sizeof(blob));
    got = tlv->get_tag(ctx, TAG_CAP, blob, (uint32_t)sizeof(blob));
    if (!cap_decode(blob, got, &probe_cap_saved)) {
        memset(blob, 0, sizeof(blob));
        return false;
    }
    memset(blob, 0, sizeof(blob));
    probe_cap_valid = true;
    return true;
}

void store_cap_forget(void)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    tag_delete_op_t op;
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    op.tlv = tlv;
    op.ctx = ctx;
    op.tag = TAG_CAP;
    uint32_t pm0 = irq_pm_rd();
    tag_delete_fn(&op); /* 直接呼出し (tag_store_safe NOTE: 外側flash_safe_execute禁止) */
    store_irq_guard(pm0);
}

/* 有線モード保持。起動時に復元し、電波を上げる前に USB 先行で列挙させる。 */
void store_wired(bool en)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t v = en ? 1u : 0u;
    if (!get_tlv(&tlv, &ctx)) {
        return;
    }
    (void)tag_store_safe(tlv, ctx, TAG_WIRED, &v, 1);
}

bool store_wired_load(void)
{
    return store_wired_load_def(false);
}

bool store_wired_load_def(bool dflt)
{
    const btstack_tlv_t *tlv = NULL;
    void *ctx = NULL;
    uint8_t v = 0u;
    if (!get_tlv(&tlv, &ctx)) {
        return dflt;
    }
    if (tlv->get_tag(ctx, TAG_WIRED, &v, 1) != 1) {
        return dflt;
    }
    return v != 0u;
}
