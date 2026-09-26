/* SPDX-License-Identifier: MIT */
#include "takeover_logic.h"

#include "takeover_config.h"

/**
 * @brief 同着時に先押しとみなすボタン。
 *
 * 切り替えは takeover_config.h の TAKEOVER_TIE_PREFER_LB で行う。
 */
#if TAKEOVER_TIE_PREFER_LB != 0
static const uint8_t TIE_FIRST_BUTTON = TAKEOVER_BUTTON_LB;
#else
static const uint8_t TIE_FIRST_BUTTON = TAKEOVER_BUTTON_RB;
#endif

/**
 * @brief start_ms から now_ms までの経過時間を返す。
 *
 * uint32_t 同士の引き算は 2^32 を法として計算されるので、途中で時刻が 0xFFFFFFFF から 0 に
 * 戻っていても、経過時間が 2^32 ms 未満であれば正しい値になる。
 * @param now_ms 現在時刻
 * @param start_ms 開始時刻
 * @return 経過時間(ミリ秒)
 */
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t start_ms) {
  return (uint32_t)(now_ms - start_ms);
}

/**
 * @brief もう片方のボタンのビットを返す。
 * @param button TAKEOVER_BUTTON_LB または TAKEOVER_BUTTON_RB
 * @return button でないほうのビット
 */
static uint8_t other_button(uint8_t button) {
  return (uint8_t)(button ^ TAKEOVER_BUTTON_MASK);
}

/**
 * @brief P / SEL_DELAY に入ってから limit_ms を「超えた」かどうか(経過 > limit_ms)。
 *
 * now_ms はメインループの各周の先頭で読んでミリ秒に切り捨てた時刻(サンプリング時刻)なので、開始時刻 timer_start_ms
 * の本当の値は timer_start_ms 〜 timer_start_ms + 1 未満のどこかにある。経過 >= limit_ms で判定すると、
 * サンプリング時刻の本当の差が limit_ms - 1 ms を少し超えただけで終わる場合がある。
 * 経過 > limit_ms(= limit_ms + 1 以上)で判定すれば、サンプリング時刻の本当の差は必ず limit_ms より長くなり、
 * limit_ms + 1 ms + メインループ1周分より短くなる。ピンが実際に変わるのは各周の出力処理の時点なので、
 * ピンの変化の間隔は、2つの周の処理時間の差(µs 程度)だけこれからずれうる。
 * T_TOGGLE / T_MANUAL はこの保証を求めていないので、そちらは経過 >= 定数で判定している。
 * @param ctx コンテキスト
 * @param now_ms 現在時刻
 * @param limit_ms 定数(T_PULSE または T_SEL_DELAY)
 * @return 経過 > limit_ms なら true
 */
static bool timer_elapsed_strictly(const takeover_ctx_t *ctx, uint32_t now_ms, uint32_t limit_ms) {
  return elapsed_ms(now_ms, ctx->timer_start_ms) > limit_ms;
}

/**
 * @brief 押されていて、かつ無視対象でないボタンを返す(同時押しの判定と優先入力の引き継ぎに使う)。
 * @param ctx コンテキスト
 * @return 有効な押下ビット
 */
static uint8_t effective_pressed(const takeover_ctx_t *ctx) {
  return (uint8_t)(ctx->pressed & (uint8_t)~ctx->ignored);
}

/**
 * @brief 入力を受け付ける状態(S0 / S1 / S2)かどうか。
 * @param state 状態
 * @return S0 / S1 / S2 なら true
 */
static bool is_accepting_state(takeover_state_t state) {
  return state == TAKEOVER_STATE_S0 || state == TAKEOVER_STATE_S1 || state == TAKEOVER_STATE_S2;
}

/**
 * @brief パルスを開始して P に移る(docs/spec.md 3.2)。
 *
 * 呼ぶ時点で button の解放は ctx->pressed に反映済みであること。まだ押されているもう片方の
 * ボタンがあれば、それを次の優先入力にする。
 * @param ctx コンテキスト
 * @param button パルスを出すボタン
 * @param now_ms 現在時刻
 */
static void start_pulse(takeover_ctx_t *ctx, uint8_t button, uint32_t now_ms) {
  const uint8_t other = other_button(button);

  ctx->state = TAKEOVER_STATE_PULSE;
  ctx->pulse_button = button;
  ctx->timer_start_ms = now_ms;
  ctx->pressed_in_pulse = 0;
  ctx->priority = (effective_pressed(ctx) & other) ? other : 0;
}

/**
 * @brief T(LOCK)で両入力が離されていれば、SEL と同時押しの長さに応じた状態へ移る(docs/spec.md 3.1 の 4)。
 *
 * SEL を反転したのと同じ時刻には LOCK を抜けない。メインループは1周の中で同じ時刻を使い、出力は1周に1回
 * まとめて行うので、同じ時刻に S2 へ移ると SEL の変化と D0:D1 = H:H が1回の出力で同時に出てしまう。
 * 1 ms 以上後に抜けることで、SEL が変わる出力の時点では D0:D1 = L:L が保証される。
 * @param ctx コンテキスト
 * @param now_ms 現在時刻
 */
static void finish_lock_if_released(takeover_ctx_t *ctx, uint32_t now_ms) {
  if (ctx->state != TAKEOVER_STATE_LOCK || ctx->pressed != 0) return;
  if (elapsed_ms(now_ms, ctx->timer_start_ms) == 0) return;

  if (!ctx->sel) {
    ctx->state = TAKEOVER_STATE_S0;
  } else if (ctx->manual_reached) {
    ctx->state = TAKEOVER_STATE_S2;
  } else {
    ctx->state = TAKEOVER_STATE_S1;
  }
  ctx->both_active = false;
  ctx->manual_reached = false;
  ctx->priority = 0;
  ctx->ignored = 0;
}

/**
 * @brief now_ms までの時間経過による遷移を、起きる順に処理する。
 *
 * 1回の呼び出しで連鎖する遷移(SEL 反転 → 両入力が既に離されていれば LOCK 終了、など)もまとめて処理する。
 * @param ctx コンテキスト
 * @param now_ms 現在時刻
 */
static void advance(takeover_ctx_t *ctx, uint32_t now_ms) {
  /* P の終了。P の間に新たに押され、まだ押されているボタンは離されるまで無視する(3.3)。
     経過 > T_PULSE の厳密比較にしている理由は timer_elapsed_strictly() を参照 */
  if (ctx->state == TAKEOVER_STATE_PULSE && timer_elapsed_strictly(ctx, now_ms, TAKEOVER_T_PULSE_MS)) {
    const uint8_t newly_ignored = (uint8_t)(ctx->pressed_in_pulse & ctx->pressed);
    if (newly_ignored != 0) ctx->warn = true;
    ctx->ignored |= newly_ignored;
    ctx->pressed_in_pulse = 0;
    ctx->state = TAKEOVER_STATE_S2;
  }

  /* 同時押しが T_TOGGLE に達したら、D0:D1 を L:L にして SEL 反転待ちに入る(3.1 の 2-1, 2-2) */
  if (is_accepting_state(ctx->state) && ctx->both_active &&
      elapsed_ms(now_ms, ctx->both_start_ms) >= TAKEOVER_T_TOGGLE_MS) {
    ctx->state = TAKEOVER_STATE_SEL_DELAY;
    ctx->timer_start_ms = now_ms;
    ctx->priority = 0;
  }

  /* T_SEL_DELAY を超えたら SEL を反転し、T に移る(3.1 の 2-3, 2-4)。厳密比較の理由は timer_elapsed_strictly() を参照 */
  if (ctx->state == TAKEOVER_STATE_SEL_DELAY && timer_elapsed_strictly(ctx, now_ms, TAKEOVER_T_SEL_DELAY_MS)) {
    ctx->sel = !ctx->sel;
    ctx->state = TAKEOVER_STATE_LOCK;
    ctx->timer_start_ms = now_ms;
  }

  /* 両方押されたまま T_MANUAL に達したことを記録する。解放時の計算だけに頼らず、ここでも記録しておく */
  if ((ctx->state == TAKEOVER_STATE_SEL_DELAY || ctx->state == TAKEOVER_STATE_LOCK) && ctx->both_active &&
      elapsed_ms(now_ms, ctx->both_start_ms) >= TAKEOVER_T_MANUAL_MS) {
    ctx->manual_reached = true;
  }

  finish_lock_if_released(ctx, now_ms);
}

/**
 * @brief 1つのボタンの押下を処理する。
 * @param ctx コンテキスト
 * @param button 押されたボタン
 * @param now_ms 現在時刻
 */
static void handle_press(takeover_ctx_t *ctx, uint8_t button, uint32_t now_ms) {
  ctx->pressed |= button;

  switch (ctx->state) {
  case TAKEOVER_STATE_PULSE:
    /* P 中の変化は捨てて WARN をラッチする(3.3) */
    ctx->warn = true;
    ctx->pressed_in_pulse |= button;
    break;

  case TAKEOVER_STATE_SEL_DELAY:
  case TAKEOVER_STATE_LOCK:
    /* 両入力が離されるまで受け付けない。WARN にもしない(3.1 の 3) */
    break;

  case TAKEOVER_STATE_S0:
  case TAKEOVER_STATE_S1:
  case TAKEOVER_STATE_S2:
    /* 押下を受け付けた瞬間に WARN を解除する(3.4) */
    ctx->warn = false;
    if (ctx->state == TAKEOVER_STATE_S2 && ctx->priority == 0) ctx->priority = button;
    if (effective_pressed(ctx) == TAKEOVER_BUTTON_MASK) {
      ctx->both_active = true;
      ctx->both_start_ms = now_ms;
      ctx->manual_reached = false;
    }
    break;
  }
}

/**
 * @brief 1つのボタンの解放を処理する。
 * @param ctx コンテキスト
 * @param button 離されたボタン
 * @param now_ms 現在時刻
 */
static void handle_release(takeover_ctx_t *ctx, uint8_t button, uint32_t now_ms) {
  const bool was_ignored = (ctx->ignored & button) != 0;

  ctx->pressed &= (uint8_t)~button;
  ctx->ignored &= (uint8_t)~button;

  switch (ctx->state) {
  case TAKEOVER_STATE_PULSE:
    /* P 中の変化は捨てて WARN をラッチする。優先入力を離した場合、そのパルスは出さない(3.3)。
       ただし「離されるまで無視」中のボタンの解放は操作とみなさず、WARN にしない */
    if (!was_ignored) ctx->warn = true;
    if (ctx->priority == button) ctx->priority = 0;
    break;

  case TAKEOVER_STATE_SEL_DELAY:
  case TAKEOVER_STATE_LOCK:
    /* 同時押しの長さは、どちらかが最初に離された時点までで測る(3.1) */
    if (ctx->both_active) {
      if (elapsed_ms(now_ms, ctx->both_start_ms) >= TAKEOVER_T_MANUAL_MS) ctx->manual_reached = true;
      ctx->both_active = false;
    }
    finish_lock_if_released(ctx, now_ms);
    break;

  case TAKEOVER_STATE_S0:
  case TAKEOVER_STATE_S1:
  case TAKEOVER_STATE_S2:
    if (was_ignored) break;
    /* T_TOGGLE 前の解放ではトグルしない(3.1) */
    ctx->both_active = false;
    if (ctx->state == TAKEOVER_STATE_S2 && ctx->priority == button) start_pulse(ctx, button, now_ms);
    break;
  }
}

void takeover_init(takeover_ctx_t *ctx, bool warn_latched) {
  ctx->state = TAKEOVER_STATE_S0;
  ctx->sel = false;
  ctx->warn = warn_latched;
  ctx->pressed = 0;
  ctx->ignored = 0;
  ctx->pressed_in_pulse = 0;
  ctx->priority = 0;
  ctx->pulse_button = 0;
  ctx->both_active = false;
  ctx->manual_reached = false;
  ctx->both_start_ms = 0;
  ctx->timer_start_ms = 0;
  ctx->initial_received = false;
}

void takeover_on_buttons(takeover_ctx_t *ctx, uint8_t pressed_bits, uint32_t now_ms) {
  advance(ctx, now_ms);

  const uint8_t bits = (uint8_t)(pressed_bits & TAKEOVER_BUTTON_MASK);

  /* 初期化後の最初の呼び出しは起動時の状態。押されているボタンは操作とみなさず、
     一度離されるまで無視する(同時押しにも数えない。WARN にもしない) */
  if (!ctx->initial_received) {
    ctx->initial_received = true;
    ctx->pressed = bits;
    ctx->ignored = bits;
    return;
  }

  const uint8_t changed = (uint8_t)(ctx->pressed ^ bits);
  const uint8_t released = (uint8_t)(changed & ctx->pressed);
  const uint8_t pressed = (uint8_t)(changed & bits);

  /* 同時に届いた変化の処理順(docs/spec.md 3.6)。解放を先に処理する。両方なら、優先入力でないほうを先に離したものとみなす
     (優先入力の解放でパルスを出すとき、もう片方が「まだ押されている」と誤って引き継がれないようにするため) */
  const uint8_t first_release = (ctx->priority != 0) ? other_button(ctx->priority) : TIE_FIRST_BUTTON;
  if (released & first_release) handle_release(ctx, first_release, now_ms);
  if (released & other_button(first_release)) handle_release(ctx, other_button(first_release), now_ms);

  /* 押下は同着優先側から処理する(3.5、3.6) */
  if (pressed & TIE_FIRST_BUTTON) handle_press(ctx, TIE_FIRST_BUTTON, now_ms);
  if (pressed & other_button(TIE_FIRST_BUTTON)) handle_press(ctx, other_button(TIE_FIRST_BUTTON), now_ms);
}

void takeover_tick(takeover_ctx_t *ctx, uint32_t now_ms) {
  advance(ctx, now_ms);
}

takeover_outputs_t takeover_get_outputs(const takeover_ctx_t *ctx) {
  takeover_outputs_t out = {.sel = ctx->sel, .d0 = false, .d1 = false, .warn = ctx->warn};

  switch (ctx->state) {
  case TAKEOVER_STATE_S2:
    out.d0 = true;
    out.d1 = true;
    break;
  case TAKEOVER_STATE_PULSE:
    /* LB: L:H、RB: H:L(docs/spec.md 1 章「出力の意味」) */
    out.d0 = (ctx->pulse_button == TAKEOVER_BUTTON_RB);
    out.d1 = (ctx->pulse_button == TAKEOVER_BUTTON_LB);
    break;
  case TAKEOVER_STATE_S0:
  case TAKEOVER_STATE_S1:
  case TAKEOVER_STATE_SEL_DELAY:
  case TAKEOVER_STATE_LOCK:
    break;
  }
  return out;
}

takeover_state_t takeover_get_state(const takeover_ctx_t *ctx) {
  return ctx->state;
}
