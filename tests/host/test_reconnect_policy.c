// test_reconnect_policy.c -- 能動再接続(page out)/discoverable 判定の
// host テスト。No Pico SDK needed. CTest name: reconnect_policy.
// 狙いは「5秒周期で接続し続ける」を構造的に止めること。page 予算が
// 枯渇したら二度と page しないことが本テストの中心。
#include <stdio.h>
#include <stdint.h>

#include "../../src/bt/reconnect_policy.h"

static int fails = 0;
#define CHECK(c, msg) do { \
    if (c) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

/* 実際に link_conn.c が組むのと同じ状態形状。 */
static reconnect_state_t ok_state(void) {
    reconnect_state_t st;
    st.wired = false;
    st.beacon = false;
    st.hid_up = false;
    st.host_known = true;
    st.outgoing_tried = false;
    st.budget_left = RECONNECT_PAGE_BUDGET;
    st.rejects = 0u;
    return st;
}

int main(void) {
    printf("[0] 素の許可条件: 全部緩いときだけ page してよい\n");
    {
        reconnect_state_t st = ok_state();
        CHECK(reconnect_should_page(&st), "T-BASE all-permissive pages");
    }

    printf("[1] 構造的な禁止条件は予算より強い\n");
    {
        reconnect_state_t st = ok_state();
        st.wired = true;
        CHECK(!reconnect_should_page(&st), "T-BLOCK wired never pages");
    }
    {
        reconnect_state_t st = ok_state();
        st.beacon = true;
        CHECK(!reconnect_should_page(&st), "T-BLOCK beacon never pages");
    }
    {
        reconnect_state_t st = ok_state();
        st.hid_up = true;
        CHECK(!reconnect_should_page(&st), "T-BLOCK already-up never pages");
    }
    {
        reconnect_state_t st = ok_state();
        st.host_known = false;
        CHECK(!reconnect_should_page(&st), "T-BLOCK unknown host never pages");
    }
    {
        reconnect_state_t st = ok_state();
        st.outgoing_tried = true;
        CHECK(!reconnect_should_page(&st), "T-BLOCK in-flight never re-pages");
    }
    {
        /* 禁止条件は予算が残っていても有効であること。 */
        reconnect_state_t st = ok_state();
        st.budget_left = 100u;
        st.wired = true;
        CHECK(!reconnect_should_page(&st),
              "T-BLOCK wired blocks even with budget to spare");
    }
    {
        CHECK(!reconnect_should_page(NULL), "T-NULL null state is safe");
    }

    printf("[2] 予算切れは終端状態(これが本命の修正)\n");
    {
        reconnect_state_t st = ok_state();
        st.budget_left = 0u;
        CHECK(!reconnect_should_page(&st), "T-BUDGET zero budget never pages");
    }
    {
        /* giveup で outgoing_tried を戻しても、予算切れなら page しない。
         * これが修正前の無限ループ。再現Regression。 */
        reconnect_state_t st = ok_state();
        uint32_t budget = 1u;
        int pages = 0;
        int tick;
        for (tick = 0; tick < 1000; tick++) {
            st.budget_left = budget;
            st.outgoing_tried = false; /* giveup が戻す値 */
            if (!reconnect_should_page(&st)) {
                continue;
            }
            pages++;
            reconnect_consume_page(&budget);
            st.outgoing_tried = true; /* 応答待ち */
        }
        CHECK(pages == 1, "T-LOOP budget 1 -> exactly 1 page in 1000 ticks");
    }
    {
        uint32_t budget = RECONNECT_PAGE_BUDGET;
        reconnect_state_t st = ok_state();
        int pages = 0;
        int tick;
        for (tick = 0; tick < 10000; tick++) {
            st.budget_left = budget;
            st.outgoing_tried = false;
            if (!reconnect_should_page(&st)) {
                continue;
            }
            pages++;
            reconnect_consume_page(&budget);
            st.outgoing_tried = true;
        }
        CHECK(pages == (int)RECONNECT_PAGE_BUDGET,
              "T-LOOP default budget -> exactly N pages then permanent stop");
        CHECK(budget == 0u, "T-LOOP budget lands on 0");
    }

    printf("[2b] 非空虚性: 予算を無視した旧挙動は周回する(回帰を捕まえる対照)\n");
    {
        /* 修正前の判定 = giveup で outgoing_tried を戻すだけ。
         * 予算の概念が無いので、応答待ちが解けた瞬間は毎回 page する。 */
        reconnect_state_t st = ok_state();
        st.budget_left = 0u; /* 旧実装に予算は無い = 常に「残り無」相当 */
        int pages = 0;
        int tick;
        for (tick = 0; tick < 1000; tick++) {
            st.outgoing_tried = false; /* giveup が戻す値 */
            bool legacy = !st.wired && !st.beacon && !st.hid_up &&
                          st.host_known && !st.outgoing_tried;
            if (legacy) {
                pages++;
            }
        }
        CHECK(pages == 1000,
              "T-LEGACY without budget pages every single tick (the bug)");
    }

    printf("[2c] 拒否ループの再現 (2026-10-01 実測の列をそのまま再現する)\n");
    {
        /* 実測: page -> open成功 -> 0x13 (held 472ms) が約10秒周期で反復。
         * page 回数ではなく「拒否された回数」で budgets させる。 */
        uint32_t budget = RECONNECT_PAGE_BUDGET;
        uint32_t rejects = 0u;
        int cycles = 0;
        int tick;
        for (tick = 0; tick < 20000; tick++) {
            reconnect_state_t st = ok_state();
            st.budget_left = budget;
            st.rejects = rejects;
            st.outgoing_tried = false;
            if (!reconnect_should_page(&st)) {
                continue;
            }
            cycles++;
            reconnect_consume_page(&budget);
            if (reconnect_is_rejection(472u)) {
                rejects++;
            } else {
                rejects = 0u;
                budget = RECONNECT_PAGE_BUDGET;
            }
        }
        CHECK(cycles == (int)RECONNECT_REJECT_LIMIT,
              "T-REJECT stops after exactly N rejections");
        CHECK(rejects == RECONNECT_REJECT_LIMIT, "T-REJECT count is N");
        CHECK(budget == 0u, "T-REJECT budget zeroed on refusal");
    }

    printf("[2d] 健全なセッションなら拒否は貯まらない\n");
    {
        uint32_t budget = RECONNECT_PAGE_BUDGET;
        uint32_t rejects = 0u;
        int cycles = 0;
        int tick;
        for (tick = 0; tick < 50; tick++) {
            reconnect_state_t st = ok_state();
            st.budget_left = budget;
            st.rejects = rejects;
            st.outgoing_tried = false;
            if (!reconnect_should_page(&st)) {
                continue;
            }
            cycles++;
            reconnect_consume_page(&budget);
            if (reconnect_is_rejection(10000u)) {
                rejects++;
            } else {
                rejects = 0u;
                budget = RECONNECT_PAGE_BUDGET;
            }
        }
        CHECK(cycles == 50, "T-HEALTHY healthy sessions never accumulate");
        CHECK(rejects == 0u, "T-HEALTHY no rejection recorded");
    }

    printf("[2e] 拒否判定の境界\n");
    {
        CHECK(reconnect_is_rejection(0u), "T-BOUND held 0 = rejection");
        CHECK(reconnect_is_rejection(472u), "T-BOUND held 472ms = rejection");
        CHECK(reconnect_is_rejection(RECONNECT_HEALTHY_MS - 1u),
              "T-BOUND just under threshold = rejection");
        CHECK(!reconnect_is_rejection(RECONNECT_HEALTHY_MS),
              "T-BOUND at threshold = healthy");
        CHECK(!reconnect_is_rejection(5135u), "T-BOUND held 5135ms = healthy");
    }

    printf("[2f] 待機状態と明示再武装 (時間減衰では復活しない)\n");
    {
        CHECK(reconnect_is_parked(0u, RECONNECT_REJECT_LIMIT),
              "T-PARK budget0+limit = parked");
        CHECK(reconnect_is_parked(0u, 0u),
              "T-PARK budget0 alone = parked");
        CHECK(reconnect_is_parked(3u, RECONNECT_REJECT_LIMIT),
              "T-PARK reject limit alone = parked");
        CHECK(!reconnect_is_parked(RECONNECT_PAGE_BUDGET, 0u),
              "T-PARK fresh budget no rejects = not parked");
        CHECK(!reconnect_is_parked(1u, 1u),
              "T-PARK one page left one reject = not parked");
    }
    {
        /* 時間減衰での自動再試行はしない。待機状態で page は 0 回。 */
        uint32_t budget = 0u;
        uint32_t rejects = RECONNECT_REJECT_LIMIT;
        int pages = 0;
        int tick;
        for (tick = 0; tick < 100000; tick++) { /* 極端に長い待ち時間 */
            reconnect_state_t st = ok_state();
            st.budget_left = budget;
            st.rejects = rejects;
            st.outgoing_tried = false;
            if (reconnect_should_page(&st)) {
                pages++;
                reconnect_consume_page(&budget);
            }
        }
        CHECK(pages == 0, "T-PARK never self-restarts on time alone");
    }
    {
        /* ホストの T_RECONNECT が到着 → 予算が全量復活し、3回試せる。 */
        uint32_t budget = RECONNECT_PAGE_BUDGET;
        uint32_t rejects = 0u;
        int pages = 0;
        int tick;
        for (tick = 0; tick < 1000; tick++) {
            reconnect_state_t st = ok_state();
            st.budget_left = budget;
            st.rejects = rejects;
            st.outgoing_tried = false;
            if (reconnect_should_page(&st)) {
                pages++;
                reconnect_consume_page(&budget);
                st.outgoing_tried = true;
            }
        }
        CHECK(pages == (int)RECONNECT_PAGE_BUDGET,
              "T-PARK explicit re-arm restores the full budget");
    }

    printf("[3] reconnect_consume_page の境界\n");
    {
        uint32_t b = 3u;
        reconnect_consume_page(&b);
        reconnect_consume_page(&b);
        reconnect_consume_page(&b);
        reconnect_consume_page(&b); /* 0 のまま3回守らない */
        CHECK(b == 0u, "T-CONSUME never underflows at 0");
        reconnect_consume_page(NULL);
        CHECK(true, "T-CONSUME null pointer is a no-op");
    }

    printf("[4] discoverable はペアリング状態に従う(常時1をやめる)\n");
    {
        CHECK(reconnect_should_be_discoverable(false, false),
              "T-DISC unpaired+radio-on -> discoverable");
        CHECK(!reconnect_should_be_discoverable(true, false),
              "T-DISC paired+radio-on -> NOT discoverable");
        CHECK(!reconnect_should_be_discoverable(false, true),
              "T-DISC quiet forces off even when unpaired");
        CHECK(!reconnect_should_be_discoverable(true, true),
              "T-DISC quiet+paired -> off");
    }

    printf("[5] connectable は無条件に quiet 従う(受動接続の前提)\n");
    {
        CHECK(reconnect_should_be_connectable(false),
              "T-CONN radio-on -> connectable");
        CHECK(!reconnect_should_be_connectable(true),
              "T-CONN quiet -> not connectable");
    }

    printf(fails ? "\nFAILED %d\n" : "\nPASS\n", fails);
    return fails;
}
