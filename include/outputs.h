/* SPDX-License-Identifier: MIT */
/**
 * @file outputs.h
 * @brief 外部出力(SEL / D0 / D1)とオンボード LED(WARN、未使用色の消灯)の GPIO 出力。
 */
#ifndef OUTPUTS_H
#define OUTPUTS_H

#include <stdbool.h>

#include "takeover_logic.h"

/**
 * @brief 出力ピンを初期化する。起動直後、他の処理より先に呼ぶ。
 *
 * SEL / D0 / D1 を L(S0 の出力)、RGB LED の赤・緑・青を H(消灯)にしてから出力に切り替える。
 * 緑(正常動作表示)は outputs_set_running_led() で別に点灯する。
 * NeoPixel の電源ピン(GPIO11)は触らない(リセット既定の L のままにして電源を入れない)。
 */
void outputs_init(void);

/**
 * @brief 正常動作表示(RGB LED の緑)を点灯・消灯する。
 *
 * LED はアクティブ Low なので、on = true で L を出す。SIO の出力レジスタに1回書くだけの処理なので、
 * panic 時の処理(src/panic_hook.c)からも呼べる。outputs_init() の前に呼んだ場合は、ピンがまだ出力に
 * なっていないので LED は点かない。
 * @param on 点灯するなら true
 */
void outputs_set_running_led(bool on);

/**
 * @brief 外部出力を安全側(SEL = L、D0:D1 = L:L)に倒す。panic 時に使う。
 *
 * docs/spec.md 3.1 のトグルと同じ順序で、先に D0:D1 を L:L にし、SEL が H なら T_SEL_DELAY 以上待ってから SEL を L にする。
 * 待ちはタイマーを使わない空ループなので、割り込みやタイマーの状態に関係なく動く(src/outputs.c 参照)。
 * WARN LED と緑 LED は変えない。outputs_init() の前に呼んだ場合は、ピンがまだ SIO の出力になっていないので
 * 何も変わらない(リセット直後のピンは内蔵プルダウンで L のまま)。
 */
void outputs_force_safe_state(void);

/**
 * @brief SEL / D0 / D1 / WARN LED をまとめて出力する。
 *
 * 4本のピンを1回のレジスタ書き込みで同時に変える。WARN LED はアクティブ Low なので、
 * warn = true のとき L を出す。
 * @param out 出力する値
 */
void outputs_apply(const takeover_outputs_t *out);

#endif // OUTPUTS_H
