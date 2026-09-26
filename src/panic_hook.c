/* SPDX-License-Identifier: MIT */
#include "panic_hook.h"

#include "outputs.h"
#include "pico.h"

void __attribute__((noreturn)) takeover_panic(const char *fmt, ...) {
  (void)fmt;

  /* 外部出力を安全側に倒す(D0:D1 = L:L → T_SEL_DELAY 以上待つ → SEL = L)。WARN LED は変えない */
  outputs_force_safe_state();

  /* 正常動作表示を消す */
  outputs_set_running_led(false);

  /* 停止する。tight_loop_contents() は何もしない関数で、ここが意図した空ループであることを示す。
     watchdog_update() を呼ばないので、ウォッチドッグが有効ならタイムアウト後にリセットされる */
  for (;;) tight_loop_contents();
}
