/* SPDX-License-Identifier: MIT */
#include "outputs.h"

#include "hardware/gpio.h"
#include "pico.h"
#include "takeover_config.h"

/** @brief GPIO 番号からピンマスク(その番号のビットだけ 1 の値)を作る。 */
#define PIN_BIT(pin) (1u << (pin))

/** @brief outputs_apply() が毎回書き換えるピン(SEL / D0 / D1 / WARN LED)。 */
static const uint32_t APPLY_MASK =
    PIN_BIT(TAKEOVER_PIN_SEL) | PIN_BIT(TAKEOVER_PIN_D0) | PIN_BIT(TAKEOVER_PIN_D1) | PIN_BIT(TAKEOVER_PIN_LED_RED);

/** @brief RGB LED の3色。アクティブ Low なので H が消灯。 */
static const uint32_t LED_MASK =
    PIN_BIT(TAKEOVER_PIN_LED_RED) | PIN_BIT(TAKEOVER_PIN_LED_GREEN) | PIN_BIT(TAKEOVER_PIN_LED_BLUE);

/** @brief D0 / D1 のピン。 */
static const uint32_t RANGE_MASK = PIN_BIT(TAKEOVER_PIN_D0) | PIN_BIT(TAKEOVER_PIN_D1);

/**
 * @brief outputs_force_safe_state() で T_SEL_DELAY 待つためのクロックサイクル数。
 *
 * SYS_CLK_HZ はビルド時に決まる clk_sys の周波数(RP2040 の既定は 125 MHz)。実際のクロックがこれより遅い場合
 * (例: ランタイムがクロックを設定する前の panic)は、同じサイクル数でも待ち時間が長くなるだけなので、
 * 「少なくとも T_SEL_DELAY 待つ」は保たれる。clk_sys をこれより速く設定し直すと短くなるが、このファームは設定しない。
 */
#define SAFE_STATE_WAIT_CYCLES ((uint32_t)(SYS_CLK_HZ / 1000u) * TAKEOVER_T_SEL_DELAY_MS)

/** @brief outputs_init() が出力に設定するピンすべて。 */
static const uint32_t ALL_OUTPUT_MASK =
    PIN_BIT(TAKEOVER_PIN_SEL) | PIN_BIT(TAKEOVER_PIN_D0) | PIN_BIT(TAKEOVER_PIN_D1) | LED_MASK;

void outputs_init(void) {
  /* gpio_init_mask は、指定したピンを SIO(CPU がソフトウェアで直接読み書きする機能)に割り当て、
     入力方向・出力値 L に初期化する。この時点ではまだ入力なので、ピンには何も出ていない */
  gpio_init_mask(ALL_OUTPUT_MASK);

  /* 出力値を先に決めておく。SEL / D0 / D1 = L、LED 3色 = H(消灯)。
     出力方向に切り替える前に値を書いておくことで、切り替えた瞬間に意図しないレベルが出ない */
  gpio_put_masked(ALL_OUTPUT_MASK, LED_MASK);

  /* ここで初めて出力方向にする */
  gpio_set_dir_out_masked(ALL_OUTPUT_MASK);
}

void outputs_set_running_led(bool on) {
  /* gpio_put は1本のピンの出力値を変える。緑 LED はアクティブ Low なので、点灯は L(false) */
  gpio_put(TAKEOVER_PIN_LED_GREEN, !on);
}

void outputs_force_safe_state(void) {
  /* 先に D0:D1 を L:L(ドライブレンジ)にする */
  gpio_put_masked(RANGE_MASK, 0);

  /* SEL が H(乗っ取り中)なら、T_SEL_DELAY 以上待ってから L に戻す。gpio_get_out_level は
     SIO の出力レジスタの値(ピンに出そうとしている値)を読む。
     待ちには busy_wait_at_least_cycles を使う。これはクロックサイクル数だけ空ループする関数で、タイマーも
     割り込みも使わない。busy_wait_us などはハードウェアタイマーの値を読んで待つので、タイマーが動き出す前の
     panic では終わらない可能性がある */
  if (gpio_get_out_level(TAKEOVER_PIN_SEL)) {
    busy_wait_at_least_cycles(SAFE_STATE_WAIT_CYCLES);
    gpio_put(TAKEOVER_PIN_SEL, false);
  }
}

void outputs_apply(const takeover_outputs_t *out) {
  uint32_t value = 0;
  if (out->sel) value |= PIN_BIT(TAKEOVER_PIN_SEL);
  if (out->d0) value |= PIN_BIT(TAKEOVER_PIN_D0);
  if (out->d1) value |= PIN_BIT(TAKEOVER_PIN_D1);
  /* WARN LED はアクティブ Low: 警告なしのときに H(消灯) */
  if (!out->warn) value |= PIN_BIT(TAKEOVER_PIN_LED_RED);

  /* gpio_put_masked は mask のビットが立っているピンだけを value の値にし、他のピンは変えない。
     内部では現在の出力値と value を比べて反転が必要なビットを求め、SIO の反転用レジスタ(GPIO_OUT_XOR)に
     1回だけ書き込む。そのため D0 と D1 は同時に変わり、途中の組み合わせ(例: L:L から H:H に変わる途中の H:L)が
     外に出ない。gpio_put を1本ずつ呼ぶとこの保証がなくなる */
  gpio_put_masked(APPLY_MASK, value);
}
