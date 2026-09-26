/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <stdint.h>

#include "button_input.h"
#include "hardware/watchdog.h"
#include "outputs.h"
#include "pico/time.h"
#include "takeover_config.h"
#include "takeover_logic.h"

/**
 * @brief 起動からの経過時間をミリ秒で返す。
 *
 * get_absolute_time() は起動からの時刻をマイクロ秒単位で持つ値を返し、to_ms_since_boot() はそれを
 * ミリ秒の uint32_t に変換する。uint32_t なので約 49.7 日で 0 に戻るが、takeover_logic は
 * 差分で経過時間を判定するので問題ない。
 * @return 現在時刻(ミリ秒)
 */
static uint32_t now_ms(void) {
  return to_ms_since_boot(get_absolute_time());
}

/** @brief 状態機械のコンテキスト。main() の外に置いてスタックを使わないようにする。 */
static takeover_ctx_t ctx;

int main(void) {
  /* 最初に出力を安全な初期値(SEL / D0 / D1 = L、LED 消灯)にする */
  outputs_init();

  /* 前回のリセットがウォッチドッグのタイムアウト(watchdog_enable() で開始したもの)によるものかを調べる。
     watchdog_enable_caused_reboot() は、リセット理由のレジスタに加えて、watchdog_enable() が
     スクラッチレジスタ 4 に書く目印も確認する。SDK の watchdog_reboot() による再起動では
     目印が消されるので false になり、単なる watchdog_caused_reboot() と違って区別できる(SDK のソースで確認)。
     電源投入やリセットボタン(RUN ピン)による起動でも false になる。
     UF2 の書き込み後の再起動は SDK の watchdog_reboot() ではなくブート ROM が行う。SDK のドキュメントは
     この場合も false になると書いているが、ブート ROM の挙動はこのワークスペースでは未検証 */
  const bool rebooted_by_watchdog = watchdog_enable_caused_reboot();

  /* ウォッチドッグによる再起動なら WARN(赤)を点けた状態で始める(docs/spec.md 6.1) */
  takeover_init(&ctx, rebooted_by_watchdog);
  takeover_outputs_t out = takeover_get_outputs(&ctx);
  outputs_apply(&out);

  button_input_init();

#if TAKEOVER_ENABLE_WATCHDOG
  /* ウォッチドッグを開始する。以後 TAKEOVER_WATCHDOG_TIMEOUT_MS 以内に watchdog_update() を呼ばないとリセットされ、
     リセット中は全 GPIO が L(SEL = L で元系統、D0:D1 = L:L でドライブレンジ)になる。
     第2引数 true は「デバッガでコアを止めている間はウォッチドッグも止める」の意味で、
     ブレークポイントで止めたときにリセットされないようにする */
  watchdog_enable(TAKEOVER_WATCHDOG_TIMEOUT_MS, true);
#endif

  /* 初期化がすべて終わったので正常動作表示(緑)を点ける。ここより前で panic した場合は点かない。
     メインループ中に panic すると src/panic_hook.c で消灯され、ウォッチドッグが有効ならリセット後に
     赤が点いた状態で再起動し、初期化に成功すると再びここで緑が点く */
  outputs_set_running_led(true);

  for (;;) {
    const uint32_t now = now_ms();

    /* PIO に溜まっている変化を、FIFO が空になるまで発生順にすべて処理する。
       起動後の最初の1件は起動時のボタン状態で、takeover_on_buttons() がそのように扱う
       (押されていたボタンは離されるまで無視。ウォッチドッグによる再起動後も同じ) */
    uint8_t pressed_bits;
    while (button_input_poll(&pressed_bits)) takeover_on_buttons(&ctx, pressed_bits, now);

    takeover_tick(&ctx, now);

    out = takeover_get_outputs(&ctx);
    outputs_apply(&out);

#if TAKEOVER_ENABLE_WATCHDOG
    /* ループが回っていることをウォッチドッグに知らせ、タイムアウトまでの残り時間を元に戻す */
    watchdog_update();
#endif
  }
}
