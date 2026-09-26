/* SPDX-License-Identifier: MIT */
#include "button_input.h"

#include "debounce.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/assert.h"
#include "takeover_config.h"

_Static_assert(TAKEOVER_PIN_LB == TAKEOVER_PIN_RB + 1u,
               "PIO reads RB and LB with one `in pins, 2`, so LB must be the GPIO right after RB");

/** @brief PIO が読む先頭のピン(bit0 = RB、bit1 = LB)。 */
#define BUTTON_PIN_BASE TAKEOVER_PIN_RB
/** @brief PIO が読むピンの本数。 */
#define BUTTON_PIN_COUNT 2u
/** @brief PIO が返す値のうち有効なビット。 */
#define BUTTON_RAW_MASK 0x3u
/**
 * @brief PIO の確定値 X の初期値。
 *
 * ピンから読める値(0〜3)のどれとも違う値にしておくことで、起動後の最初の push が必ず起動時の状態になる
 * (src/debounce.pio 冒頭のコメントを参照)。set 命令の即値は 5 ビット(0〜31)なので 4 を使える。
 */
#define BUTTON_X_INITIAL_UNKNOWN 0x4u

/** @brief 使用する PIO ブロック(docs/spec.md 4 章で PIO0 と決めている)。 */
static const PIO button_pio = pio0;

/** @brief button_input_init() で確保したステートマシンの番号。 */
static uint button_sm;

/**
 * @brief 1本の入力ピンを設定する。
 *
 * gpio_pull_up() で内蔵プルアップを有効にする。外付けの 10kΩ プルアップと並列になるが、
 * 外付けが外れてもピンが浮かないようにするためと、RP2040 のリセット既定である内蔵プルダウンを
 * 解除するため。pio_gpio_init() はピンの機能を PIO0 に切り替える(入力として読むだけなら必須ではないが、
 * このピンを PIO が使っていることを明示するために呼ぶ)。
 * @param pin GPIO 番号
 */
static void init_input_pin(uint pin) {
  gpio_pull_up(pin);
  pio_gpio_init(button_pio, pin);
}

/**
 * @brief SM のクロックを debounce_CLOCK_HZ にするための分周比を設定する。
 *
 * 分周比は「clk_sys の周波数 / debounce_CLOCK_HZ」で、整数部 16 ビット・小数部 8 ビット(1/256 単位)で指定する。
 * 既定の clk_sys 125 MHz では 125000000 / 4000 = 31250 でちょうど割り切れる。
 * 整数部が 16 ビットに収まらない(clk_sys が約 262 MHz を超える)と正しく設定できないので、hard_assert で止める。
 * hard_assert はリリースビルドでも有効な assert で、Release ビルド(Pico SDK の既定)では失敗すると panic する
 * (この時点で出力は安全側に初期化済み。panic 時の処理は include/panic_hook.h)。
 * @param c 設定を書き込む SM 設定構造体
 */
static void set_debounce_clock(pio_sm_config *c) {
  const uint32_t sys_hz = clock_get_hz(clk_sys);
  const uint32_t div_int = sys_hz / debounce_CLOCK_HZ;
  const uint32_t div_frac8 = ((sys_hz % debounce_CLOCK_HZ) * 256u) / debounce_CLOCK_HZ;

  hard_assert(div_int >= 1u && div_int <= 0xFFFFu);
  sm_config_set_clkdiv_int_frac8(c, div_int, (uint8_t)div_frac8);
}

void button_input_init(void) {
  init_input_pin(TAKEOVER_PIN_RB);
  init_input_pin(TAKEOVER_PIN_LB);

  /* PIO の命令メモリ(32 命令分)にプログラムを書き込む。戻り値は書き込んだ先頭アドレスで、
     空きが足りなければ負の値になるので hard_assert で止める */
  const int offset = pio_add_program(button_pio, &debounce_program);
  hard_assert(offset >= 0);
  /* 空いている SM を1つ確保する。第2引数 true は「確保できなければ panic する」の意味 */
  button_sm = (uint)pio_claim_unused_sm(button_pio, true);

  /* pioasm が生成した既定設定(wrap 位置など)を元に、この用途向けの設定を足す */
  pio_sm_config c = debounce_program_get_default_config((uint)offset);
  /* `in pins, n` が読む先頭のピン */
  sm_config_set_in_pins(&c, BUTTON_PIN_BASE);
  /* ISR は左シフト(新しいビットが下位に入る)、自動 push はしない(push はプログラム中で明示的に行う) */
  sm_config_set_in_shift(&c, false, false, 32);
  /* TX FIFO は使わないので RX FIFO に連結し、RX 側を 4 段から 8 段に増やす(取りこぼしにくくする) */
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  set_debounce_clock(&c);

  /* PIO から見たピンの向きを入力にする */
  int rc = pio_sm_set_consecutive_pindirs(button_pio, button_sm, BUTTON_PIN_BASE, BUTTON_PIN_COUNT, false);
  hard_assert(rc == PICO_OK);

  /* SM に設定を書き込み、FIFO を空にして、開始位置(ラベル start)に PC を合わせる。この時点ではまだ停止している */
  rc = pio_sm_init(button_pio, button_sm, (uint)offset + debounce_offset_start, &c);
  hard_assert(rc == PICO_OK);

  /* 確定値 X の初期値を「どのピン状態とも一致しない値(4)」にする。pio_sm_exec は1命令をその場で実行させる関数で、
     SM が停止中でも実行される。プログラム中で X を初期化する命令を持たずに済み、命令数を節約できる */
  pio_sm_exec(button_pio, button_sm, pio_encode_set(pio_x, BUTTON_X_INITIAL_UNKNOWN));

  pio_sm_set_enabled(button_pio, button_sm, true);
}

bool button_input_poll(uint8_t *pressed_bits) {
  if (pio_sm_is_rx_fifo_empty(button_pio, button_sm)) return false;

  /* pio_sm_get は FIFO が空でないことを確認済みなので待たずに1語取り出す */
  const uint32_t raw = pio_sm_get(button_pio, button_sm);
  /* 生の値は押下 = 0 なので反転し、下位 2 ビットだけを残す */
  *pressed_bits = (uint8_t)(~raw & BUTTON_RAW_MASK);
  return true;
}
