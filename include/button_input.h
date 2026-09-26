/* SPDX-License-Identifier: MIT */
/**
 * @file button_input.h
 * @brief LB / RB の入力。PIO によるチャタリング除去の初期化と、除去済みの状態変化の読み出し(docs/spec.md 4 章)。
 */
#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 入力ピンと PIO0 のステートマシン1つを設定し、チャタリング除去を開始する。
 *
 * 入力ピンは内蔵プルアップを有効にする。起動後の最初の1件(除去時間の約 21.5 ms 後)は、ボタンが押されて
 * いてもいなくても(入力が暴れ続けていない限り)届き、起動時の状態を表す。2件目以降は状態が変化したときだけ届く。
 * PIO の命令メモリやステートマシンが確保できない場合は panic する。
 */
void button_input_init(void);

/**
 * @brief チャタリング除去済みの状態変化を1件取り出す。
 *
 * PIO の RX FIFO が空なら何もせず false を返す。FIFO には変化が発生順に並んでいるので、
 * false が返るまで繰り返し呼べば、溜まっている変化をすべて発生順に受け取れる。
 * @param pressed_bits 取り出した状態の書き込み先。押下 = 1 に変換済みで、bit1 = LB、bit0 = RB
 *                     (TAKEOVER_BUTTON_LB / TAKEOVER_BUTTON_RB と同じ並び)
 * @return 1件取り出したら true、FIFO が空なら false
 */
bool button_input_poll(uint8_t *pressed_bits);

#endif // BUTTON_INPUT_H
