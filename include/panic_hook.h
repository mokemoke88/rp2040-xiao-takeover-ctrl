/* SPDX-License-Identifier: MIT */
/**
 * @file panic_hook.h
 * @brief Pico SDK の panic を置き換える関数(外部出力を安全側に倒し、緑 LED を消して停止する)。
 *
 * 仕組み(Pico SDK 2.3.1 の src/rp2_common/pico_platform_panic/ で確認):
 * - コンパイル時定義 PICO_PANIC_FUNCTION を定義すると、panic.c の既定の panic()(メッセージを出力して _exit する)は
 *   コンパイルされなくなり、代わりに custom_panic_function.S の panic が PICO_PANIC_FUNCTION で指定した関数へ転送する
 * - PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1 のときは `ldr r4, =関数; bx r4` で直接飛ぶので、panic() に渡された引数が
 *   そのまま届く。指定した関数は戻ってはいけない
 * - CMakeLists.txt で PICO_PANIC_FUNCTION=takeover_panic と PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1 を定義している
 * - hard_assert() が失敗したときは、リリースビルド(NDEBUG 定義あり。Pico SDK の既定で、CMAKE_BUILD_TYPE 未指定時は
 *   Release になる)では hard_assertion_failure() → panic("Hard assert") の順に呼ばれるので、ここに来る。
 *   デバッグビルドでは hard_assert() は assert() になり、失敗すると __assert_func() → _exit() に進むので、ここには来ない
 * - pio_claim_unused_sm(pio, true) などが確保に失敗したときの panic もここに来る
 *
 * panic を経由しない停止(HardFault など)ではこの関数は呼ばれず、外部出力も緑 LED もそのままになる。
 */
#ifndef PANIC_HOOK_H
#define PANIC_HOOK_H

/**
 * @brief panic の置き換え。外部出力を安全側に倒し、緑 LED を消灯してから無限ループで停止する。
 *
 * 処理の順序(docs/spec.md 6.2):
 * 1. outputs_force_safe_state(): D0:D1 = L:L → (SEL が H なら)T_SEL_DELAY 以上の空ループ → SEL = L。
 *    WARN LED は変えない
 * 2. 緑 LED を消灯する
 * 3. 無限ループで停止する。ウォッチドッグが有効なら watchdog_update() が呼ばれないのでタイムアウト後にリセットされ、
 *    再起動後は WARN(赤)が点いた状態で始まり、初期化に成功すれば緑が点く(docs/spec.md 6.1、7 章)。
 *    ウォッチドッグを開始する前(初期化中)の panic や、ウォッチドッグ無効のビルドでは、停止したままになる
 *
 * outputs_init() より前の panic では、ピンがまだ SIO の出力になっていないので 1・2 は何も変えず、
 * 外部出力はリセット直後の状態(内蔵プルダウンで L)のままになる。
 * @param fmt panic() に渡された書式文字列(使わない)
 */
void __attribute__((noreturn)) takeover_panic(const char *fmt, ...);

#endif // PANIC_HOOK_H
