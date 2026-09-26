/* SPDX-License-Identifier: MIT */
/* takeover_logic の単体テスト。入力と期待値の対応は tests/CASES.md を参照 */
#include <stdint.h>

#include "takeover_config.h"
#include "takeover_logic.h"
#include "unity.h"

#define NONE 0u
#define LB TAKEOVER_BUTTON_LB
#define RB TAKEOVER_BUTTON_RB
#define BOTH TAKEOVER_BUTTON_MASK

/* 同着時に先押しとみなす側(TAKEOVER_TIE_PREFER_LB で変わる。make test は両方の設定でビルドして実行する) */
#if TAKEOVER_TIE_PREFER_LB != 0
#define TIE_FIRST LB
#else
#define TIE_FIRST RB
#endif
#define TIE_SECOND (TIE_FIRST ^ BOTH)

/* ボタン b のパルス中の D0 / D1(LB: L:H、RB: H:L) */
#define PULSE_D0(b) ((b) == RB)
#define PULSE_D1(b) ((b) == LB)

static takeover_ctx_t ctx;

/* 押下状態 bits を時刻 t に渡す */
static void btn(uint8_t bits, uint32_t t) {
  takeover_on_buttons(&ctx, bits, t);
}

static void tick(uint32_t t) {
  takeover_tick(&ctx, t);
}

/* 各テストの前に、初期化して「起動時は何も押されていない」状態を渡しておく */
void setUp(void) {
  takeover_init(&ctx, false);
  btn(NONE, 0);
}
void tearDown(void) {}

#define ASSERT_STATE(expected) TEST_ASSERT_EQUAL_INT((expected), takeover_get_state(&ctx))

/* 出力 SEL / D0 / D1 / WARN を 1 = H(WARN は 1 = 警告ラッチ中)で比較する */
#define ASSERT_OUT(e_sel, e_d0, e_d1, e_warn)                                                                          \
  do {                                                                                                                 \
    const takeover_outputs_t o_ = takeover_get_outputs(&ctx);                                                          \
    TEST_ASSERT_EQUAL_INT_MESSAGE((e_sel), o_.sel, "SEL");                                                             \
    TEST_ASSERT_EQUAL_INT_MESSAGE((e_d0), o_.d0, "D0");                                                                \
    TEST_ASSERT_EQUAL_INT_MESSAGE((e_d1), o_.d1, "D1");                                                                \
    TEST_ASSERT_EQUAL_INT_MESSAGE((e_warn), o_.warn, "WARN");                                                          \
  } while (0)

/* 時刻 t から S0 → S1 に遷移させ、最後の操作の時刻を返す */
static uint32_t enter_s1(uint32_t t) {
  btn(BOTH, t);
  tick(t + 2000u);
  tick(t + 2011u);
  btn(NONE, t + 2100u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  return t + 2100u;
}

/* 時刻 t から S0 → S2 に遷移させ、最後の操作の時刻を返す */
static uint32_t enter_s2(uint32_t t) {
  btn(BOTH, t);
  tick(t + 2000u);
  tick(t + 2011u);
  btn(NONE, t + 4000u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  return t + 4000u;
}

/* ---- 初期状態、起動時の押下(4.1)、ウォッチドッグ再起動時の WARN(6.1) ---- */

void test_init_is_s0_all_low(void) {
  takeover_init(&ctx, false);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_boot_pressed_button_ignored_until_release(void) {
  takeover_init(&ctx, false);
  btn(LB, 20); /* 起動時の状態: LB が押されていた */
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
  btn(BOTH, 100); /* RB を押しても、LB は無視中なので同時押しにならない */
  tick(5000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  btn(RB, 5000); /* LB を離す: 無視が解ける。WARN にはならない */
  ASSERT_OUT(0, 0, 0, 0);
  btn(BOTH, 5100); /* 離した後の LB の押下は受け付ける */
  tick(7099);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  tick(7100);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
}

void test_boot_both_pressed_never_toggles(void) {
  takeover_init(&ctx, false);
  btn(BOTH, 20);
  tick(10000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  btn(NONE, 10000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
  btn(BOTH, 11000);
  tick(13000);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
}

void test_init_with_warn_latched_cleared_by_press_in_s0(void) {
  takeover_init(&ctx, true); /* ウォッチドッグによる再起動 */
  ASSERT_OUT(0, 0, 0, 1);
  btn(NONE, 20);
  tick(5000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 1);
  btn(LB, 5000); /* S0 での押下を受け付けた瞬間に解除 */
  ASSERT_OUT(0, 0, 0, 0);
}

void test_init_with_warn_latched_not_cleared_by_boot_pressed_button(void) {
  takeover_init(&ctx, true);
  btn(LB, 20); /* 起動時に LB が押されていた(無視中) */
  ASSERT_OUT(0, 0, 0, 1);
  btn(NONE, 500); /* 無視中のボタンの解放では解除しない */
  ASSERT_OUT(0, 0, 0, 1);
  btn(RB, 600); /* 受け付けた押下で解除 */
  ASSERT_OUT(0, 0, 0, 0);
}

void test_extra_bits_are_ignored(void) {
  btn(0xFCu, 1000);
  tick(5000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

/* ---- トグル(3.1) ---- */

void test_toggle_s0_to_s1_sequence(void) {
  btn(BOTH, 1000);
  tick(2999);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
  tick(3000);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(0, 0, 0, 0);
  tick(3010); /* 経過 = T_SEL_DELAY ちょうどでは、まだ反転しない */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(0, 0, 0, 0);
  tick(3011);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, 3500);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_toggle_not_before_2000ms(void) {
  btn(BOTH, 1000);
  tick(2999);
  btn(LB, 2999);
  tick(10000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_release_event_at_exactly_2000ms_toggles_first(void) {
  btn(BOTH, 1000);
  btn(LB, 3000);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick(3011);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, 3100);
  ASSERT_STATE(TAKEOVER_STATE_S1);
}

void test_toggle_timer_restarts_on_repress(void) {
  btn(BOTH, 1000);
  btn(LB, 2500);
  btn(BOTH, 2600);
  tick(4599);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  tick(4600);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
}

void test_lock_waits_for_both_released_without_warn(void) {
  btn(BOTH, 1000);
  tick(3000);
  tick(3011);
  btn(RB, 3100);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  btn(BOTH, 3200);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  btn(LB, 3300);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  btn(NONE, 3400);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_release_during_sel_delay_finishes_after_sel_flip(void) {
  btn(BOTH, 1000);
  tick(3000);
  btn(NONE, 3005);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(0, 0, 0, 0);
  tick(3010);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick(3011); /* SEL を反転。同じ時刻には LOCK を抜けない */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(3012);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_lock_not_left_at_same_ms_as_sel_flip(void) {
  /* 呼び出しの間隔が空き、SEL 反転と解放が同じ時刻に処理される場合でも、S2(H:H)へは次の時刻まで移らない */
  btn(BOTH, 1000);
  tick(3000);
  btn(NONE, 5500); /* この呼び出しの中で SEL 反転(同時押しは 4 秒以上)と解放が起きる */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(5501);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_at_exactly_4000ms(void) {
  btn(BOTH, 1000);
  tick(3000);
  tick(3011);
  tick(4999);
  btn(NONE, 5000);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_no_manual_at_3999ms(void) {
  btn(BOTH, 1000);
  tick(3000);
  tick(3011);
  btn(NONE, 4999);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_manual_recorded_by_tick_while_held(void) {
  btn(BOTH, 1000);
  tick(3000);
  tick(3011);
  tick(5000);
  btn(NONE, 7000);
  ASSERT_STATE(TAKEOVER_STATE_S2);
}

void test_manual_measured_until_first_release(void) {
  btn(BOTH, 1000);
  tick(3000);
  tick(3011);
  btn(RB, 4000); /* LB を 3000ms で離す */
  tick(6000);
  btn(NONE, 6000); /* RB は 5000ms 押していた */
  ASSERT_STATE(TAKEOVER_STATE_S1);
}

void test_manual_not_extended_by_repress_in_lock(void) {
  btn(BOTH, 1000);
  tick(3000);
  tick(3011);
  btn(RB, 3500);
  btn(BOTH, 3600);
  tick(8000);
  btn(NONE, 8000);
  ASSERT_STATE(TAKEOVER_STATE_S1);
}

void test_toggle_s1_to_s0(void) {
  const uint32_t t = enter_s1(1000) + 100u;
  btn(BOTH, t);
  tick(t + 2000u);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + 2011u);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, t + 2100u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_toggle_s1_to_s0_even_if_held_4000ms(void) {
  const uint32_t t = enter_s1(1000) + 100u;
  btn(BOTH, t);
  tick(t + 2000u);
  tick(t + 2011u);
  tick(t + 5000u);
  btn(NONE, t + 5000u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_toggle_s2_to_s0_drive_range_before_sel(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  ASSERT_OUT(1, 1, 1, 0);
  btn(BOTH, t);
  tick(t + 1999u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  tick(t + 2000u); /* D0:D1 を先に L:L */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + 2010u);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + 2011u); /* T_SEL_DELAY を超えてから SEL を反転 */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, t + 2100u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_s2_toggle_release_during_sel_delay(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(BOTH, t);
  tick(t + 2000u);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, t + 2005u); /* SEL 反転前に両方離す: パルスは出ない */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + 2010u);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + 2011u); /* SEL を反転 */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  tick(t + 2012u); /* 両方離されているので LOCK を抜ける */
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_toggle_after_pulse_with_carried_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 50u);
  btn(RB, t + 100u); /* LB パルス。RB が次の優先入力 */
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  btn(BOTH, t + 400u); /* RB を押したまま LB を押す: ここから同時押しの計時 */
  tick(t + 2399u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  tick(t + 2400u);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + 2411u);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, t + 2500u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

/* ---- S0 / S1 の単押し(3.2) ---- */

void test_s0_single_press_ignored(void) {
  btn(LB, 1000);
  tick(4000);
  btn(NONE, 4000);
  btn(RB, 5000);
  tick(8000);
  btn(NONE, 8000);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_s1_single_press_ignored(void) {
  const uint32_t t = enter_s1(1000) + 100u;
  btn(LB, t);
  tick(t + 3000u);
  btn(NONE, t + 3000u);
  btn(RB, t + 3100u);
  btn(NONE, t + 3200u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

/* ---- S2 の単押しパルス(3.2) ---- */

void test_s2_lb_pulse_on_release(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 100u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(t + 300u); /* 経過 = T_PULSE ちょうどでは、まだパルス中 */
  ASSERT_OUT(1, 0, 1, 0);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_rb_pulse_on_release(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(RB, t);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 100u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(t + 300u);
  ASSERT_OUT(1, 1, 0, 0);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_second_button_takes_over_as_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 50u);
  btn(RB, t + 100u); /* 優先入力 LB を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  btn(NONE, t + 400u); /* P 終了後に RB を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(t + 601u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_non_priority_release_gives_no_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 50u);
  btn(LB, t + 100u); /* 後押しの RB を先に離す */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 200u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
}

void test_s2_tie_press_first_side_is_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(BOTH, t);             /* 同着 */
  btn(TIE_FIRST, t + 100u); /* 後押し扱いの側を離してもパルスは出ない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 200u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, PULSE_D0(TIE_FIRST), PULSE_D1(TIE_FIRST), 0);
}

void test_s2_tie_press_second_side_becomes_next_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(BOTH, t);
  btn(TIE_SECOND, t + 100u); /* 先押し扱いの側を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, PULSE_D0(TIE_FIRST), PULSE_D1(TIE_FIRST), 0);
  tick(t + 301u);
  btn(NONE, t + 400u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, PULSE_D0(TIE_SECOND), PULSE_D1(TIE_SECOND), 0);
}

void test_s2_simultaneous_release_lb_priority_single_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 50u);
  btn(NONE, t + 100u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_simultaneous_release_rb_priority_single_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(RB, t);
  btn(BOTH, t + 50u);
  btn(NONE, t + 100u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_event_at_pulse_end_is_handled_after_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 301u); /* P が終わる時刻ちょうどの押下。tick を挟まない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 400u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
}

/* ---- P 中の入力と WARN(3.3、3.4) ---- */

void test_carried_priority_released_in_pulse_warns_without_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 50u);
  btn(RB, t + 100u);
  btn(NONE, t + 200u); /* P 中に RB を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 1);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  tick(t + 2000u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
}

void test_press_at_pulse_last_ms_is_in_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 300u); /* 経過 = T_PULSE ちょうど: まだ P 中の押下 */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 1);
}

void test_press_in_pulse_warns_and_is_ignored_until_release(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 150u); /* P 中に押す */
  ASSERT_OUT(1, 0, 1, 1);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  btn(NONE, t + 400u); /* 無視中の RB を離してもパルスも WARN 解除もない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  btn(RB, t + 500u); /* 離した後の押下は受け付ける */
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 600u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
}

void test_ignored_button_not_counted_as_simultaneous(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 150u); /* P 中に押し、押しっぱなし */
  tick(t + 301u);
  btn(BOTH, t + 400u); /* LB の押下は受け付けて WARN 解除。RB は無視中なので同時押しではない */
  ASSERT_OUT(1, 1, 1, 0);
  tick(t + 3000u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  btn(RB, t + 3000u); /* LB を離す → LB パルス */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(t + 3201u);
  btn(NONE, t + 3300u); /* 無視中の RB を離してもパルスは出ない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_ignored_button_released_in_pulse_does_not_warn(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 150u); /* P 中に押し、押しっぱなし → 無視中 */
  tick(t + 301u);
  btn(BOTH, t + 400u); /* LB 押下で WARN 解除 */
  btn(RB, t + 500u);   /* LB 解放 → LB パルス。RB は無視中なので次の優先入力にならない */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  btn(NONE, t + 600u); /* P 中に無視中の RB を離す: 操作とみなさないので WARN にしない */
  ASSERT_OUT(1, 0, 1, 0);
  tick(t + 701u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_press_and_release_within_pulse_is_not_ignored(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 150u);
  btn(NONE, t + 200u);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  btn(RB, t + 400u); /* 受け付けられて WARN 解除 */
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 500u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
}

void test_release_priority_and_press_other_in_same_event(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(RB, t + 100u); /* LB 解放と RB 押下が1件で届く: 解放を先に処理 → パルス開始後の押下扱い */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 1);
  tick(t + 301u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  btn(NONE, t + 400u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
}

void test_warn_cleared_by_accepted_press_in_s2(void) {
  /* WARN ラッチ中でも、S2 で押下を受け付けた瞬間に解除される */
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(NONE, t + 100u);
  btn(RB, t + 150u);
  btn(NONE, t + 200u);
  tick(t + 301u);
  ASSERT_OUT(1, 1, 1, 1);
  btn(LB, t + 400u);
  ASSERT_OUT(1, 1, 1, 0);
}

/* ---- 時刻のラップアラウンド ---- */

void test_wraparound_toggle_and_manual(void) {
  const uint32_t t0 = UINT32_MAX - 999u; /* 1000ms 後に 0 に戻る */
  btn(BOTH, t0);
  tick((uint32_t)(t0 + 1999u));
  ASSERT_STATE(TAKEOVER_STATE_S0);
  tick((uint32_t)(t0 + 2000u));
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick((uint32_t)(t0 + 2010u));
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick((uint32_t)(t0 + 2011u));
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  btn(NONE, (uint32_t)(t0 + 4000u));
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_wraparound_pulse(void) {
  const uint32_t base = UINT32_MAX - 4099u;
  const uint32_t t = enter_s2(base) + 50u; /* t = UINT32_MAX - 49 */
  btn(RB, t);
  btn(NONE, (uint32_t)(t + 10u));
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  tick((uint32_t)(t + 210u));
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  tick((uint32_t)(t + 211u));
  ASSERT_STATE(TAKEOVER_STATE_S2);
}

/* ---- 乱択による安全不変条件の検査 ---- */

/* 再現できるよう固定シードの線形合同法を使う */
static uint32_t rng_state;

static uint32_t rng_next(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

static bool is_drive_range(takeover_outputs_t o) {
  return !o.d0 && !o.d1;
}

void test_random_safety_invariants(void) {
  /* 境界付近を多く含む時間間隔の候補 */
  static const uint32_t steps[] = {0,   1,   5,    9,    10,   11,   12,   50,   199,  200, 201,
                                   202, 500, 1000, 1999, 2000, 2001, 2500, 3999, 4000, 4001};
  const uint32_t n_steps = (uint32_t)(sizeof steps / sizeof steps[0]);

  rng_state = 20260926u;
  uint32_t now = UINT32_MAX - 50000000u; /* 途中で時刻が 0 に戻るようにする */
  unsigned sel_changes = 0, pulses = 0, manual_entries = 0, warn_latches = 0, warn_clears = 0, boot_pressed = 0,
           boot_warn = 0;

  /* 起動からやり直す区間を繰り返す。起動時の押下状態と WARN の初期値も乱択にする */
  for (uint32_t episode = 0; episode < 600u; episode++) {
    const uint32_t r0 = rng_next();
    const bool warn_at_boot = (r0 & 3u) == 0u;
    uint8_t bits = (uint8_t)((r0 >> 4) & BOTH);
    now += 1000u;
    takeover_init(&ctx, warn_at_boot);
    btn(bits, now); /* 起動時の状態 */
    if (bits != 0) boot_pressed++;
    if (warn_at_boot) boot_warn++;

    /* 起動時の状態: S0、出力は L、押されていたボタンは無視中、WARN は初期値のまま */
    TEST_ASSERT_EQUAL_INT(TAKEOVER_STATE_S0, takeover_get_state(&ctx));
    TEST_ASSERT_EQUAL_UINT8(bits, ctx.ignored);
    TEST_ASSERT_EQUAL_INT(warn_at_boot, takeover_get_outputs(&ctx).warn);

    uint32_t both_since = now;  /* 入力が両方押しになった時刻 */
    uint32_t range_since = now; /* D0:D1 = L:L が続いている開始時刻 */
    uint32_t pulse_start = 0;   /* 今のパルスの開始時刻 */
    takeover_outputs_t prev = takeover_get_outputs(&ctx);
    takeover_state_t prev_state = takeover_get_state(&ctx);

    for (uint32_t i = 0; i < 500u; i++) {
      const uint32_t r = rng_next();
      now += (r & 1u) ? steps[(r >> 1) % n_steps] : ((r >> 1) % 300u);
      const uint8_t prev_bits = bits;
      const uint8_t prev_ignored = ctx.ignored;
      const bool is_btn = (r >> 12) % 3u != 0u;

      if (is_btn) {
        bits = (uint8_t)((r >> 16) & BOTH);
        btn(bits, now);
      } else {
        tick(now);
      }

      const takeover_outputs_t out = takeover_get_outputs(&ctx);
      const takeover_state_t state = takeover_get_state(&ctx);

      /* ロジックが把握している押下状態は入力どおりで、無視中のボタンは押されているボタンの一部 */
      TEST_ASSERT_EQUAL_UINT8(bits, ctx.pressed);
      TEST_ASSERT_EQUAL_UINT8(0, ctx.ignored & (uint8_t)~ctx.pressed);

      /* SEL = L の間は D0:D1 = L:L */
      if (!out.sel) TEST_ASSERT_TRUE(is_drive_range(out));

      /* SEL が変わる瞬間は、その前後とも D0:D1 = L:L で、L:L になってから T_SEL_DELAY を超えている */
      if (out.sel != prev.sel) {
        TEST_ASSERT_TRUE(is_drive_range(prev));
        TEST_ASSERT_TRUE(is_drive_range(out));
        TEST_ASSERT_TRUE((uint32_t)(now - range_since) > TAKEOVER_T_SEL_DELAY_MS);
        sel_changes++;
      }
      if (is_drive_range(out) && !is_drive_range(prev)) range_since = now;

      /* SEL_DELAY に入った時点で、入力は両方押しが T_TOGGLE 以上続いていて、無視中のボタンはない */
      if (state == TAKEOVER_STATE_SEL_DELAY && prev_state != TAKEOVER_STATE_SEL_DELAY) {
        TEST_ASSERT_EQUAL_UINT8(BOTH, prev_bits);
        TEST_ASSERT_TRUE((uint32_t)(now - both_since) >= TAKEOVER_T_TOGGLE_MS);
        TEST_ASSERT_EQUAL_UINT8(0, prev_ignored);
      }

      /* パルスは、そのボタンの解放を処理した btn 呼び出しの中でだけ始まる */
      const bool new_pulse =
          state == TAKEOVER_STATE_PULSE && (prev_state != TAKEOVER_STATE_PULSE || ctx.timer_start_ms != pulse_start);
      if (new_pulse) {
        TEST_ASSERT_TRUE(is_btn);
        TEST_ASSERT_TRUE((prev_bits & ctx.pulse_button) != 0);
        TEST_ASSERT_TRUE((bits & ctx.pulse_button) == 0);
      }
      /* パルスは SEL = H の間だけ出て、T_PULSE を超えるまで続く */
      if (prev_state == TAKEOVER_STATE_PULSE && (state != TAKEOVER_STATE_PULSE || new_pulse)) {
        TEST_ASSERT_TRUE((uint32_t)(now - pulse_start) > TAKEOVER_T_PULSE_MS);
      }
      if (new_pulse) {
        pulse_start = ctx.timer_start_ms;
        pulses++;
      }
      if (state == TAKEOVER_STATE_PULSE) {
        TEST_ASSERT_TRUE(out.sel);
        TEST_ASSERT_TRUE(out.d0 != out.d1);
      }

      /* WARN の解除は押下を含む btn 呼び出しでだけ起きる */
      if (prev.warn && !out.warn) {
        TEST_ASSERT_TRUE(is_btn);
        TEST_ASSERT_TRUE((bits & (uint8_t)~prev_bits) != 0);
        warn_clears++;
      }
      /* WARN のラッチは P の間(または P の終了時)にだけ起きる */
      if (!prev.warn && out.warn) {
        TEST_ASSERT_TRUE(prev_state == TAKEOVER_STATE_PULSE || state == TAKEOVER_STATE_PULSE);
        warn_latches++;
      }

      if (state == TAKEOVER_STATE_S2 && prev_state == TAKEOVER_STATE_LOCK) manual_entries++;

      if (bits == BOTH && prev_bits != BOTH) both_since = now;
      prev = out;
      prev_state = state;
    }
  }

  /* 検査が形だけにならないよう、主要な遷移が十分な回数起きたことを確認する */
  TEST_ASSERT_TRUE(sel_changes >= 100u);
  TEST_ASSERT_TRUE(pulses >= 100u);
  TEST_ASSERT_TRUE(manual_entries >= 10u);
  TEST_ASSERT_TRUE(warn_latches >= 10u);
  TEST_ASSERT_TRUE(warn_clears >= 10u);
  TEST_ASSERT_TRUE(boot_pressed >= 100u);
  TEST_ASSERT_TRUE(boot_warn >= 50u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_is_s0_all_low);
  RUN_TEST(test_boot_pressed_button_ignored_until_release);
  RUN_TEST(test_boot_both_pressed_never_toggles);
  RUN_TEST(test_init_with_warn_latched_cleared_by_press_in_s0);
  RUN_TEST(test_init_with_warn_latched_not_cleared_by_boot_pressed_button);
  RUN_TEST(test_extra_bits_are_ignored);
  RUN_TEST(test_toggle_s0_to_s1_sequence);
  RUN_TEST(test_toggle_not_before_2000ms);
  RUN_TEST(test_release_event_at_exactly_2000ms_toggles_first);
  RUN_TEST(test_toggle_timer_restarts_on_repress);
  RUN_TEST(test_lock_waits_for_both_released_without_warn);
  RUN_TEST(test_release_during_sel_delay_finishes_after_sel_flip);
  RUN_TEST(test_lock_not_left_at_same_ms_as_sel_flip);
  RUN_TEST(test_manual_at_exactly_4000ms);
  RUN_TEST(test_no_manual_at_3999ms);
  RUN_TEST(test_manual_recorded_by_tick_while_held);
  RUN_TEST(test_manual_measured_until_first_release);
  RUN_TEST(test_manual_not_extended_by_repress_in_lock);
  RUN_TEST(test_toggle_s1_to_s0);
  RUN_TEST(test_toggle_s1_to_s0_even_if_held_4000ms);
  RUN_TEST(test_toggle_s2_to_s0_drive_range_before_sel);
  RUN_TEST(test_s2_toggle_release_during_sel_delay);
  RUN_TEST(test_toggle_after_pulse_with_carried_priority);
  RUN_TEST(test_s0_single_press_ignored);
  RUN_TEST(test_s1_single_press_ignored);
  RUN_TEST(test_s2_lb_pulse_on_release);
  RUN_TEST(test_s2_rb_pulse_on_release);
  RUN_TEST(test_s2_second_button_takes_over_as_priority);
  RUN_TEST(test_s2_non_priority_release_gives_no_pulse);
  RUN_TEST(test_s2_tie_press_first_side_is_priority);
  RUN_TEST(test_s2_tie_press_second_side_becomes_next_priority);
  RUN_TEST(test_s2_simultaneous_release_lb_priority_single_pulse);
  RUN_TEST(test_s2_simultaneous_release_rb_priority_single_pulse);
  RUN_TEST(test_event_at_pulse_end_is_handled_after_pulse);
  RUN_TEST(test_carried_priority_released_in_pulse_warns_without_pulse);
  RUN_TEST(test_press_at_pulse_last_ms_is_in_pulse);
  RUN_TEST(test_press_in_pulse_warns_and_is_ignored_until_release);
  RUN_TEST(test_ignored_button_not_counted_as_simultaneous);
  RUN_TEST(test_ignored_button_released_in_pulse_does_not_warn);
  RUN_TEST(test_press_and_release_within_pulse_is_not_ignored);
  RUN_TEST(test_release_priority_and_press_other_in_same_event);
  RUN_TEST(test_warn_cleared_by_accepted_press_in_s2);
  RUN_TEST(test_wraparound_toggle_and_manual);
  RUN_TEST(test_wraparound_pulse);
  RUN_TEST(test_random_safety_invariants);
  return UNITY_END();
}
