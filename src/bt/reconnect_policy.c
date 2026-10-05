// reconnect_policy.c -- reconnect_policy.h の実装。BTstack 非依存。
#include "reconnect_policy.h"

bool reconnect_should_page(const reconnect_state_t *st)
{
    if (st == NULL) {
        return false;
    }
    /* 二重認識・再生中の偽装名乗りを避ける条件は予算より先。 */
    if (st->wired || st->beacon || st->hid_up) {
        return false;
    }
    /* 宛先を知らない能動pageは意味がない。 */
    if (!st->host_known) {
        return false;
    }
    /* page 済みで応答待ちの窓中は重ねて page しない。 */
    if (st->outgoing_tried) {
        return false;
    }
    /* 拒否が上限に達したら終わり。予算と拒否回数の両方が
     * 許すときだけ page する。 */
    if (st->rejects >= RECONNECT_REJECT_LIMIT) {
        return false;
    }
    return st->budget_left > 0u;
}

void reconnect_consume_page(uint32_t *budget_left)
{
    if (budget_left == NULL || *budget_left == 0u) {
        return;
    }
    (*budget_left)--;
}

bool reconnect_is_rejection(uint32_t held_ms)
{
    /* HID を開けてから一定もたずに切られたものは、本体が受付けずに
     * 自分から切った (reason 0x13) とみなす。 */
    return held_ms < RECONNECT_HEALTHY_MS;
}

bool reconnect_is_parked(uint32_t budget_left, uint32_t rejects)
{
    return (rejects >= RECONNECT_REJECT_LIMIT) || (budget_left == 0u);
}

bool reconnect_should_be_discoverable(bool host_known, bool quiet)
{
    /* quiet(有線)なら回路ごと止まるので必ず 0。 */
    if (quiet) {
        return false;
    }
    /* ペア済みは discoverable でなくても Switch から page される。 */
    return !host_known;
}

bool reconnect_should_be_connectable(bool quiet)
{
    /* 受動接続は connectable が 1 でないと成立しない。 */
    return !quiet;
}
