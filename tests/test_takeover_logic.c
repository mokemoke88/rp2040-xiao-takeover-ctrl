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

/* 時刻はすべて takeover_config.h の定数から計算する(定数を変えてもテストが追従するように)。
   make test は仕様値のほか、定数を変えた構成(Makefile の TEST_ALT)でも実行する */
#define T_TOGGLE TAKEOVER_T_TOGGLE_MS
#define T_MANUAL TAKEOVER_T_MANUAL_MS
#define T_PULSE TAKEOVER_T_PULSE_MS
#define T_SEL_DELAY TAKEOVER_T_SEL_DELAY_MS
/* 同時押し開始から SEL が反転するまで: T_TOGGLE で SEL_DELAY に入り、そこからの経過 > T_SEL_DELAY で反転 */
#define T_FLIP (T_TOGGLE + T_SEL_DELAY + 1u)
/* パルス開始から終了まで(経過 > T_PULSE) */
#define T_PULSE_END (T_PULSE + 1u)
/* パルス開始から、P の途中の操作を入れる時刻(P の範囲 0〜T_PULSE の中ほど) */
#define T_IN_PULSE (T_PULSE / 2u + 1u)
/* どのタイマーの期限よりも十分長い時間(押し続けても何も起きないことを確かめるのに使う) */
#define T_LONG (2u * T_MANUAL)
/* 呼び出し間隔が粗い場合の遅れ */
#define T_COARSE (T_MANUAL / 4u + 1u)

/* テストの前提。SEL 反転の直後(同時押し開始 + T_FLIP + 1)に離して S1 になることを確かめるので、
   T_MANUAL は T_FLIP + 1 より長い必要がある(takeover_config.h の前提より 2 ms 強い) */
TAKEOVER_STATIC_CHECK(T_MANUAL >= T_FLIP + 2u, test_check_manual_after_flip_margin);
/* P の途中(開始 + 1、+ 2、+ T_IN_PULSE)に操作を置けること */
TAKEOVER_STATIC_CHECK(T_PULSE >= 2u, test_check_pulse_room);

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
  tick(t + T_TOGGLE);
  tick(t + T_FLIP);
  btn(NONE, t + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  return t + T_FLIP + 1u;
}

/* 時刻 t から S0 → S2 に遷移させ、最後の操作の時刻(t + T_MANUAL)を返す */
static uint32_t enter_s2(uint32_t t) {
  btn(BOTH, t);
  tick(t + T_TOGGLE);
  tick(t + T_FLIP);
  btn(NONE, t + T_MANUAL);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  return t + T_MANUAL;
}

/* S2 で時刻 t から LB のパルスを始め、パルスを始めた時刻を返す */
static uint32_t start_lb_pulse(uint32_t t) {
  btn(LB, t);
  btn(NONE, t + 1u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  return t + 1u;
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
  const uint32_t t = 100u + T_LONG;
  tick(t);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  btn(RB, t); /* LB を離す: 無視が解ける。WARN にはならない */
  ASSERT_OUT(0, 0, 0, 0);
  const uint32_t s = t + 1u;
  btn(BOTH, s); /* 離した後の LB の押下は受け付ける */
  tick(s + T_TOGGLE - 1u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  tick(s + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
}

void test_boot_both_pressed_never_toggles(void) {
  takeover_init(&ctx, false);
  btn(BOTH, 20);
  const uint32_t t = 20u + T_LONG;
  tick(t);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  btn(NONE, t);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
  btn(BOTH, t + 1u);
  tick(t + 1u + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
}

void test_init_with_warn_latched_cleared_by_press_in_s0(void) {
  takeover_init(&ctx, true); /* ウォッチドッグによる再起動 */
  ASSERT_OUT(0, 0, 0, 1);
  btn(NONE, 20);
  tick(20u + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 1);
  btn(LB, 20u + T_LONG); /* S0 での押下を受け付けた瞬間に解除 */
  ASSERT_OUT(0, 0, 0, 0);
}

void test_init_with_warn_latched_not_cleared_by_boot_pressed_button(void) {
  takeover_init(&ctx, true);
  btn(LB, 20); /* 起動時に LB が押されていた(無視中) */
  ASSERT_OUT(0, 0, 0, 1);
  btn(NONE, 21); /* 無視中のボタンの解放では解除しない */
  ASSERT_OUT(0, 0, 0, 1);
  btn(RB, 22); /* 受け付けた押下で解除 */
  ASSERT_OUT(0, 0, 0, 0);
}

void test_extra_bits_are_ignored(void) {
  btn(0xFCu, 1000);
  tick(1000u + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

/* ---- トグル(3.1) ---- */

void test_toggle_s0_to_s1_sequence(void) {
  const uint32_t s = 1000u; /* 同時押し開始 */
  btn(BOTH, s);
  tick(s + T_TOGGLE - 1u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
  tick(s + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(0, 0, 0, 0);
  tick(s + T_TOGGLE + T_SEL_DELAY); /* 経過 = T_SEL_DELAY ちょうどでは、まだ反転しない */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(0, 0, 0, 0);
  tick(s + T_FLIP);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, s + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_toggle_not_before_t_toggle(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE - 1u);
  btn(LB, s + T_TOGGLE - 1u);
  tick(s + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_release_event_at_exactly_t_toggle_toggles_first(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  btn(LB, s + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick(s + T_FLIP);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, s + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
}

void test_toggle_timer_restarts_on_repress(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  btn(LB, s + T_TOGGLE - 1u);
  const uint32_t s2 = s + T_TOGGLE; /* 押し直した時刻が新しい同時押し開始 */
  btn(BOTH, s2);
  tick(s2 + T_TOGGLE - 1u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  tick(s2 + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
}

void test_lock_waits_for_both_released_without_warn(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  btn(RB, s + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  btn(BOTH, s + T_FLIP + 2u);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  btn(LB, s + T_FLIP + 3u);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  btn(NONE, s + T_FLIP + 4u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_release_during_sel_delay_finishes_after_sel_flip(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  btn(NONE, s + T_TOGGLE + T_SEL_DELAY); /* SEL 反転前(経過 = T_SEL_DELAY)に両方離す */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(0, 0, 0, 0);
  tick(s + T_FLIP); /* SEL を反転。同じ時刻には LOCK を抜けない */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(s + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

/* ---- マニュアル待機の表示(3.1 の 3、4) ---- */

void test_lock_not_left_at_same_ms_as_sel_flip(void) {
  /* 呼び出しの間隔が空き、SEL 反転・T_MANUAL 到達・両解放が同じ呼び出しで処理される場合でも、H:H は次の時刻まで出さない */
  const uint32_t s = 1000u;
  const uint32_t late = s + T_MANUAL + T_COARSE;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  btn(NONE, late);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(late + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_hh_not_at_same_ms_as_sel_flip_while_held(void) {
  /* 呼び出しの間隔が空き、SEL 反転と T_MANUAL 到達が同じ tick で処理される(押し続けている)場合 */
  const uint32_t s = 1000u;
  const uint32_t late = s + T_MANUAL + T_COARSE;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(late); /* SEL 反転。T_MANUAL は過ぎているが H:H は出さない */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(late + 1u); /* 次の時刻で H:H */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, late + 2u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_hh_not_at_same_ms_as_sel_flip_one_released(void) {
  /* 呼び出しの間隔が空き、SEL 反転・T_MANUAL 到達・片方の解放が同じ呼び出しで処理される場合 */
  const uint32_t s = 1000u;
  const uint32_t late = s + T_MANUAL + T_COARSE;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  btn(RB, late); /* LB を離す */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(late + 1u); /* 次の時刻で H:H(離した時点で同時押しは T_MANUAL を超えていた) */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, late + 2u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_hh_while_held_at_t_manual(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  tick(s + T_MANUAL - 1u); /* 同時押し T_MANUAL - 1: まだ L:L */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(s + T_MANUAL); /* T_MANUAL: 解放を待たずに H:H。LOCK のまま */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, s + T_MANUAL + 1u); /* 両解放で S2。出力は H:H のまま */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_at_exactly_t_manual_release_event(void) {
  /* tick を挟まず、T_MANUAL ちょうどの両解放イベントで到達が判定される場合 */
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  tick(s + T_MANUAL - 1u);
  btn(NONE, s + T_MANUAL);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_one_released_at_exactly_t_manual(void) {
  /* T_MANUAL ちょうどに片方だけ離す: 到達とみなして H:H。LOCK は続く */
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  tick(s + T_MANUAL - 1u);
  btn(RB, s + T_MANUAL); /* LB を離す */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, s + T_MANUAL + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_no_manual_at_t_manual_minus_1(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  btn(NONE, s + T_MANUAL - 1u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_manual_not_reached_if_one_released_at_t_manual_minus_1(void) {
  /* T_MANUAL - 1 で片方を離し、他方を押し続けて T_MANUAL 以降に進めても H:H にならない */
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  btn(RB, s + T_MANUAL - 1u); /* LB を離す */
  tick(s + T_MANUAL);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 0, 0, 0);
  tick(s + T_LONG);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, s + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_manual_hh_input_locked_until_both_released(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  tick(s + T_MANUAL);
  ASSERT_OUT(1, 1, 1, 0);
  btn(RB, s + T_MANUAL + 1u); /* 片方を離しても LOCK のまま。パルスも WARN もない */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(BOTH, s + T_MANUAL + 2u); /* 押し直しても何も起きない */
  tick(s + T_MANUAL + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(LB, s + T_MANUAL + T_LONG + 1u);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, s + T_MANUAL + T_LONG + 2u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_manual_measured_until_first_release(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  btn(RB, s + T_FLIP + 1u); /* LB を T_MANUAL より前に離す */
  tick(s + T_LONG);         /* RB は押し続けているが H:H にはならない */
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, s + T_LONG); /* RB は T_MANUAL より長く押していた */
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_manual_not_extended_by_repress_in_lock(void) {
  const uint32_t s = 1000u;
  btn(BOTH, s);
  tick(s + T_TOGGLE);
  tick(s + T_FLIP);
  btn(RB, s + T_FLIP + 1u);
  btn(BOTH, s + T_FLIP + 2u); /* 押し直しても T_MANUAL の判定は再開しない */
  tick(s + T_FLIP + 2u + T_LONG);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, s + T_FLIP + 2u + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

void test_toggle_s1_to_s0(void) {
  const uint32_t t = enter_s1(1000) + 100u;
  btn(BOTH, t);
  tick(t + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + T_FLIP);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, t + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_toggle_s1_to_s0_even_if_held_t_manual(void) {
  const uint32_t t = enter_s1(1000) + 100u;
  btn(BOTH, t);
  tick(t + T_TOGGLE);
  tick(t + T_FLIP);
  tick(t + T_MANUAL); /* SEL = L 側へのトグルでは T_MANUAL でも何もしない */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  tick(t + T_LONG);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, t + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_toggle_s2_to_s0_drive_range_before_sel(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  ASSERT_OUT(1, 1, 1, 0);
  btn(BOTH, t);
  tick(t + T_TOGGLE - 1u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  tick(t + T_TOGGLE); /* D0:D1 を先に L:L */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + T_TOGGLE + T_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + T_FLIP); /* T_SEL_DELAY を超えてから SEL を反転 */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, t + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_s2_toggle_release_during_sel_delay(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(BOTH, t);
  tick(t + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  btn(NONE, t + T_TOGGLE + T_SEL_DELAY); /* SEL 反転前に両方離す: パルスは出ない */
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(t + T_FLIP); /* SEL を反転 */
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  tick(t + T_FLIP + 1u); /* 両方離されているので LOCK を抜ける */
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_toggle_after_pulse_with_carried_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 1u);
  const uint32_t r = t + 2u;
  btn(RB, r); /* LB パルス。RB が次の優先入力 */
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  const uint32_t s = r + T_PULSE_END + 1u;
  btn(BOTH, s); /* RB を押したまま LB を押す: ここから同時押しの計時 */
  tick(s + T_TOGGLE - 1u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  tick(s + T_TOGGLE);
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  ASSERT_OUT(1, 0, 0, 0);
  tick(s + T_FLIP);
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  ASSERT_OUT(0, 0, 0, 0);
  btn(NONE, s + T_FLIP + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

/* ---- S0 / S1 の単押し(3.2) ---- */

void test_s0_single_press_ignored(void) {
  const uint32_t t = 1000u;
  btn(LB, t);
  tick(t + T_LONG);
  btn(NONE, t + T_LONG);
  btn(RB, t + T_LONG + 1u);
  tick(t + 2u * T_LONG);
  btn(NONE, t + 2u * T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S0);
  ASSERT_OUT(0, 0, 0, 0);
}

void test_s1_single_press_ignored(void) {
  const uint32_t t = enter_s1(1000) + 100u;
  btn(LB, t);
  tick(t + T_LONG);
  btn(NONE, t + T_LONG);
  btn(RB, t + T_LONG + 1u);
  btn(NONE, t + T_LONG + 2u);
  ASSERT_STATE(TAKEOVER_STATE_S1);
  ASSERT_OUT(1, 0, 0, 0);
}

/* ---- S2 の単押しパルス(3.2) ---- */

void test_s2_lb_pulse_on_release(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  const uint32_t r = t + 100u;
  btn(NONE, r);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(r + T_PULSE); /* 経過 = T_PULSE ちょうどでは、まだパルス中 */
  ASSERT_OUT(1, 0, 1, 0);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_rb_pulse_on_release(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(RB, t);
  ASSERT_OUT(1, 1, 1, 0);
  const uint32_t r = t + 100u;
  btn(NONE, r);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(r + T_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_second_button_takes_over_as_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 1u);
  const uint32_t r = t + 2u;
  btn(RB, r); /* 優先入力 LB を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  const uint32_t r2 = r + T_PULSE_END + 1u;
  btn(NONE, r2); /* P 終了後に RB を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(r2 + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_non_priority_release_gives_no_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 1u);
  btn(LB, t + 2u); /* 後押しの RB を先に離す */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 3u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
}

void test_s2_tie_press_first_side_is_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(BOTH, t);           /* 同着 */
  btn(TIE_FIRST, t + 1u); /* 後押し扱いの側を離してもパルスは出ない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, t + 2u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, PULSE_D0(TIE_FIRST), PULSE_D1(TIE_FIRST), 0);
}

void test_s2_tie_press_second_side_becomes_next_priority(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(BOTH, t);
  const uint32_t r = t + 1u;
  btn(TIE_SECOND, r); /* 先押し扱いの側を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, PULSE_D0(TIE_FIRST), PULSE_D1(TIE_FIRST), 0);
  tick(r + T_PULSE_END);
  btn(NONE, r + T_PULSE_END + 1u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, PULSE_D0(TIE_SECOND), PULSE_D1(TIE_SECOND), 0);
}

void test_s2_simultaneous_release_lb_priority_single_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 1u);
  const uint32_t r = t + 2u;
  btn(NONE, r);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_s2_simultaneous_release_rb_priority_single_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(RB, t);
  btn(BOTH, t + 1u);
  const uint32_t r = t + 2u;
  btn(NONE, r);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_event_at_pulse_end_is_handled_after_pulse(void) {
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + T_PULSE_END); /* P が終わる時刻ちょうどの押下。tick を挟まない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, r + T_PULSE_END + 1u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
}

/* ---- P 中の入力と WARN(3.3、3.4) ---- */

void test_carried_priority_released_in_pulse_warns_without_pulse(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  btn(BOTH, t + 1u);
  const uint32_t r = t + 2u;
  btn(RB, r);
  btn(NONE, r + T_IN_PULSE); /* P 中に RB を離す */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 1);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  tick(r + T_PULSE_END + T_LONG);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
}

void test_press_at_pulse_last_ms_is_in_pulse(void) {
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + T_PULSE); /* 経過 = T_PULSE ちょうど: まだ P 中の押下 */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 1);
}

void test_press_in_pulse_warns_and_is_ignored_until_release(void) {
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + T_IN_PULSE); /* P 中に押す */
  ASSERT_OUT(1, 0, 1, 1);
  const uint32_t e = r + T_PULSE_END;
  tick(e);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  btn(NONE, e + 1u); /* 無視中の RB を離してもパルスも WARN 解除もない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  btn(RB, e + 2u); /* 離した後の押下は受け付ける */
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, e + 3u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
}

void test_ignored_button_not_counted_as_simultaneous(void) {
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + T_IN_PULSE); /* P 中に押し、押しっぱなし */
  tick(r + T_PULSE_END);
  const uint32_t q = r + T_PULSE_END + 1u;
  btn(BOTH, q); /* LB の押下は受け付けて WARN 解除。RB は無視中なので同時押しではない */
  ASSERT_OUT(1, 1, 1, 0);
  const uint32_t r2 = q + T_TOGGLE + 1u;
  tick(r2);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  btn(RB, r2); /* LB を離す → LB パルス */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  tick(r2 + T_PULSE_END);
  btn(NONE, r2 + T_PULSE_END + 1u); /* 無視中の RB を離してもパルスは出ない */
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_ignored_button_released_in_pulse_does_not_warn(void) {
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + T_IN_PULSE); /* P 中に押し、押しっぱなし → 無視中 */
  tick(r + T_PULSE_END);
  const uint32_t q = r + T_PULSE_END + 1u;
  btn(BOTH, q); /* LB 押下で WARN 解除 */
  const uint32_t r2 = q + 1u;
  btn(RB, r2); /* LB 解放 → LB パルス。RB は無視中なので次の優先入力にならない */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 0);
  btn(NONE, r2 + T_IN_PULSE); /* P 中に無視中の RB を離す: 操作とみなさないので WARN にしない */
  ASSERT_OUT(1, 0, 1, 0);
  tick(r2 + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_press_and_release_within_pulse_is_not_ignored(void) {
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + 1u);
  btn(NONE, r + 2u);
  const uint32_t e = r + T_PULSE_END;
  tick(e);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
  btn(RB, e + 1u); /* 受け付けられて WARN 解除 */
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, e + 2u);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 1, 0, 0);
}

void test_release_priority_and_press_other_in_same_event(void) {
  const uint32_t t = enter_s2(1000) + 100u;
  btn(LB, t);
  const uint32_t r = t + 1u;
  btn(RB, r); /* LB 解放と RB 押下が1件で届く: 解放を先に処理 → パルス開始後の押下扱い */
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  ASSERT_OUT(1, 0, 1, 1);
  tick(r + T_PULSE_END);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  btn(NONE, r + T_PULSE_END + 1u);
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 1);
}

void test_warn_cleared_by_accepted_press_in_s2(void) {
  /* WARN ラッチ中でも、S2 で押下を受け付けた瞬間に解除される */
  const uint32_t r = start_lb_pulse(enter_s2(1000) + 100u);
  btn(RB, r + 1u);
  btn(NONE, r + 2u);
  tick(r + T_PULSE_END);
  ASSERT_OUT(1, 1, 1, 1);
  btn(LB, r + T_PULSE_END + 1u);
  ASSERT_OUT(1, 1, 1, 0);
}

/* ---- 時刻のラップアラウンド ---- */

void test_wraparound_toggle_and_manual(void) {
  const uint32_t t0 = UINT32_MAX - T_TOGGLE / 2u; /* 同時押しの途中で時刻が 0 に戻る */
  btn(BOTH, t0);
  tick((uint32_t)(t0 + T_TOGGLE - 1u));
  ASSERT_STATE(TAKEOVER_STATE_S0);
  tick((uint32_t)(t0 + T_TOGGLE));
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick((uint32_t)(t0 + T_TOGGLE + T_SEL_DELAY));
  ASSERT_STATE(TAKEOVER_STATE_SEL_DELAY);
  tick((uint32_t)(t0 + T_FLIP));
  ASSERT_STATE(TAKEOVER_STATE_LOCK);
  tick((uint32_t)(t0 + T_MANUAL - 1u));
  ASSERT_OUT(1, 0, 0, 0);
  tick((uint32_t)(t0 + T_MANUAL));
  ASSERT_OUT(1, 1, 1, 0);
  btn(NONE, (uint32_t)(t0 + T_MANUAL));
  ASSERT_STATE(TAKEOVER_STATE_S2);
  ASSERT_OUT(1, 1, 1, 0);
}

void test_wraparound_pulse(void) {
  /* パルスの途中で時刻が 0 に戻るように、パルス開始時刻 r から逆算して始める */
  const uint32_t r = UINT32_MAX - T_PULSE / 2u;
  const uint32_t t = enter_s2(r - 2u - T_MANUAL) + 1u; /* enter_s2 は開始 + T_MANUAL を返すので t = r - 1 */
  btn(RB, t);
  btn(NONE, r);
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  tick((uint32_t)(r + T_PULSE));
  ASSERT_STATE(TAKEOVER_STATE_PULSE);
  tick((uint32_t)(r + T_PULSE_END));
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

static bool is_hh(takeover_outputs_t o) {
  return o.d0 && o.d1;
}

void test_random_safety_invariants(void) {
  /* 境界付近を多く含む時間間隔の候補(定数から計算) */
  const uint32_t steps[] = {0u,
                            1u,
                            2u,
                            T_SEL_DELAY - 1u,
                            T_SEL_DELAY,
                            T_SEL_DELAY + 1u,
                            T_SEL_DELAY + 2u,
                            T_PULSE - 1u,
                            T_PULSE,
                            T_PULSE + 1u,
                            T_PULSE + 2u,
                            T_TOGGLE - 1u,
                            T_TOGGLE,
                            T_TOGGLE + 1u,
                            T_MANUAL - T_FLIP,
                            T_MANUAL - T_TOGGLE,
                            T_MANUAL - 1u,
                            T_MANUAL,
                            T_MANUAL + 1u,
                            T_COARSE};
  const uint32_t n_steps = (uint32_t)(sizeof steps / sizeof steps[0]);

  rng_state = 20260926u;
  uint32_t now = UINT32_MAX - 50000000u; /* 途中で時刻が 0 に戻るようにする */
  unsigned sel_changes = 0, pulses = 0, manual_entries = 0, warn_latches = 0, warn_clears = 0, boot_pressed = 0,
           boot_warn = 0, lock_manual_outputs = 0, manual_boundary_hits = 0;

  /* 起動からやり直す区間を繰り返す。起動時の押下状態と WARN の初期値も乱択にする */
  for (uint32_t episode = 0; episode < 600u; episode++) {
    const uint32_t r0 = rng_next();
    const bool warn_at_boot = (r0 & 3u) == 0u;
    uint8_t bits = (uint8_t)((r0 >> 4) & BOTH);
    now += T_LONG;
    takeover_init(&ctx, warn_at_boot);
    btn(bits, now); /* 起動時の状態 */
    if (bits != 0) boot_pressed++;
    if (warn_at_boot) boot_warn++;

    /* 起動時の状態: S0、出力は L、押されていたボタンは無視中、WARN は初期値のまま */
    TEST_ASSERT_EQUAL_INT(TAKEOVER_STATE_S0, takeover_get_state(&ctx));
    TEST_ASSERT_EQUAL_UINT8(bits, ctx.ignored);
    TEST_ASSERT_EQUAL_INT(warn_at_boot, takeover_get_outputs(&ctx).warn);

    uint32_t both_since = now;      /* 入力が両方押しになった時刻 */
    uint32_t toggle_both_start = 0; /* トグルを起こした同時押しの開始時刻 */
    uint32_t toggle_both_end = 0;   /* その同時押しが最初に途切れた時刻 */
    bool toggle_both_ended = true;
    uint32_t sel_changed_at = now - 1u; /* 最後に SEL が変わった呼び出しの時刻 */
    uint32_t range_since = now;         /* D0:D1 = L:L が続いている開始時刻 */
    uint32_t pulse_start = 0;           /* 今のパルスの開始時刻 */
    takeover_outputs_t prev = takeover_get_outputs(&ctx);
    takeover_state_t prev_state = takeover_get_state(&ctx);

    for (uint32_t i = 0; i < 500u; i++) {
      const uint32_t r = rng_next();

      /* 時間の進め方: 1/4 は、次に何かが起きうる期限(の前後 1 ms)にちょうど止まるように進める。
         期限は、同時押し開始 + T_TOGGLE / T_MANUAL、P / SEL_DELAY / LOCK に入った時刻 + T_PULSE / T_SEL_DELAY / 1。
         残りは境界付近の間隔の候補か、小さな乱数で進める */
      const uint32_t mode = (r >> 1) % 4u;
      uint32_t step;
      if (mode == 0u) {
        const uint32_t deadlines[] = {both_since + T_TOGGLE,
                                      both_since + T_MANUAL,
                                      toggle_both_start + T_MANUAL,
                                      ctx.timer_start_ms + T_PULSE,
                                      ctx.timer_start_ms + T_SEL_DELAY,
                                      ctx.timer_start_ms};
        const uint32_t n_deadlines = (uint32_t)(sizeof deadlines / sizeof deadlines[0]);
        const uint32_t target = deadlines[(r >> 3) % n_deadlines] + (r >> 6) % 3u - 1u; /* 期限 - 1 〜 期限 + 1 */
        const uint32_t diff = (uint32_t)(target - now);
        step = (diff <= T_LONG) ? diff : 1u; /* 過ぎた期限や遠すぎる期限は使わない */
      } else if (mode == 1u) {
        step = steps[(r >> 3) % n_steps];
      } else {
        step = (r >> 3) % (T_PULSE + T_SEL_DELAY + 2u);
      }
      now += step;

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
        TEST_ASSERT_TRUE((uint32_t)(now - range_since) > T_SEL_DELAY);
        sel_changed_at = now;
        sel_changes++;
      }

      /* D0:D1 が H:H になるのは、SEL が変わった呼び出しとは別の時刻 */
      if (is_hh(out) && !is_hh(prev)) {
        TEST_ASSERT_TRUE(out.sel == prev.sel);
        TEST_ASSERT_TRUE(now != sel_changed_at);
      }
      if (is_drive_range(out) && !is_drive_range(prev)) range_since = now;

      /* SEL_DELAY に入った時点で、入力は両方押しが T_TOGGLE 以上続いていて、無視中のボタンはない */
      if (state == TAKEOVER_STATE_SEL_DELAY && prev_state != TAKEOVER_STATE_SEL_DELAY) {
        TEST_ASSERT_EQUAL_UINT8(BOTH, prev_bits);
        TEST_ASSERT_TRUE((uint32_t)(now - both_since) >= T_TOGGLE);
        TEST_ASSERT_EQUAL_UINT8(0, prev_ignored);
        toggle_both_start = both_since;
        toggle_both_ended = false;
      }
      /* トグルを起こした同時押しが最初に途切れた時刻を記録する */
      if (!toggle_both_ended && bits != BOTH) {
        toggle_both_end = now;
        toggle_both_ended = true;
      }
      /* トグルを起こした同時押しを途切れずに続けた長さ(今も続いていれば今まで) */
      const uint32_t held = (uint32_t)((toggle_both_ended ? toggle_both_end : now) - toggle_both_start);

      if (state == TAKEOVER_STATE_LOCK) {
        /* LOCK 中の H:H は、SEL = H で、同時押しが途切れずに T_MANUAL 以上続いたときだけ */
        if (is_hh(out)) {
          TEST_ASSERT_TRUE(out.sel);
          TEST_ASSERT_TRUE(held >= T_MANUAL);
          lock_manual_outputs++;
        }
        /* 逆に、SEL = H で同時押しが途切れずに T_MANUAL 以上続いていれば、SEL 反転と別の時刻なら必ず H:H(遅れない) */
        if (out.sel && held >= T_MANUAL && now != sel_changed_at) {
          TEST_ASSERT_TRUE(is_hh(out));
          if (held == T_MANUAL) manual_boundary_hits++;
        }
        /* LOCK 中は H:H から L:L に戻らない */
        if (prev_state == TAKEOVER_STATE_LOCK && is_hh(prev)) TEST_ASSERT_TRUE(is_hh(out));
      }

      /* LOCK → S2 は同時押しが途切れずに T_MANUAL 以上続いたときだけ。続いていたなら LOCK → S1 にはならない */
      if (prev_state == TAKEOVER_STATE_LOCK && state == TAKEOVER_STATE_S2) {
        TEST_ASSERT_TRUE(held >= T_MANUAL);
        manual_entries++;
      }
      if (prev_state == TAKEOVER_STATE_LOCK && state == TAKEOVER_STATE_S1) TEST_ASSERT_TRUE(held < T_MANUAL);

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
        TEST_ASSERT_TRUE((uint32_t)(now - pulse_start) > T_PULSE);
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

      if (bits == BOTH && prev_bits != BOTH) both_since = now;
      prev = out;
      prev_state = state;
    }
  }

  /* 検査が形だけにならないよう、主要な遷移が十分な回数起きたことを確認する */
  TEST_ASSERT_TRUE(sel_changes >= 100u);
  TEST_ASSERT_TRUE(pulses >= 100u);
  TEST_ASSERT_TRUE(manual_entries >= 10u);
  TEST_ASSERT_TRUE(lock_manual_outputs >= 10u);
  TEST_ASSERT_TRUE(manual_boundary_hits >= 5u);
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
  RUN_TEST(test_toggle_not_before_t_toggle);
  RUN_TEST(test_release_event_at_exactly_t_toggle_toggles_first);
  RUN_TEST(test_toggle_timer_restarts_on_repress);
  RUN_TEST(test_lock_waits_for_both_released_without_warn);
  RUN_TEST(test_release_during_sel_delay_finishes_after_sel_flip);
  RUN_TEST(test_lock_not_left_at_same_ms_as_sel_flip);
  RUN_TEST(test_manual_hh_not_at_same_ms_as_sel_flip_while_held);
  RUN_TEST(test_manual_hh_not_at_same_ms_as_sel_flip_one_released);
  RUN_TEST(test_manual_hh_while_held_at_t_manual);
  RUN_TEST(test_manual_at_exactly_t_manual_release_event);
  RUN_TEST(test_manual_one_released_at_exactly_t_manual);
  RUN_TEST(test_no_manual_at_t_manual_minus_1);
  RUN_TEST(test_manual_not_reached_if_one_released_at_t_manual_minus_1);
  RUN_TEST(test_manual_hh_input_locked_until_both_released);
  RUN_TEST(test_manual_measured_until_first_release);
  RUN_TEST(test_manual_not_extended_by_repress_in_lock);
  RUN_TEST(test_toggle_s1_to_s0);
  RUN_TEST(test_toggle_s1_to_s0_even_if_held_t_manual);
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
