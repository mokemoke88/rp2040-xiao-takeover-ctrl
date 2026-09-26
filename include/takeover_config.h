/* SPDX-License-Identifier: MIT */
/**
 * @file takeover_config.h
 * @brief ピン番号・タイミング定数・同着時の優先・ウォッチドッグ設定をまとめた設定ヘッダ。
 *
 * docs/spec.md 5 章「定数はコード内の1か所で定義し、変えやすくする」に従い、
 * 調整しうる値はすべてこのファイルに置く。ただしチャタリング除去の時間(T_DEBOUNCE)は
 * PIO の命令列と一体なので src/debounce.pio 側で定義している。
 *
 * このヘッダは Pico SDK に依存しない(ホストの単体テストからも include するため)。
 */
#ifndef TAKEOVER_CONFIG_H
#define TAKEOVER_CONFIG_H

/* ------------------------------------------------------------------------- */
/* ピン割り当て(docs/spec.md 1 章、7 章)。値は RP2040 の GPIO 番号。      */
/* ------------------------------------------------------------------------- */

/** @brief 出力 D0(XIAO ピン D0)。 */
#define TAKEOVER_PIN_D0 26u
/** @brief 出力 D1(XIAO ピン D1)。 */
#define TAKEOVER_PIN_D1 27u
/** @brief 出力 SEL(XIAO ピン D2)。H = 乗っ取り中、L = 元系統。 */
#define TAKEOVER_PIN_SEL 28u

/** @brief WARN 表示に使うオンボード RGB LED の赤。アクティブ Low(L で点灯)。 */
#define TAKEOVER_PIN_LED_RED 17u
/**
 * @brief オンボード RGB LED の緑。正常動作表示に使う。アクティブ Low(L で点灯)。
 *
 * 初期化がすべて成功してメインループに入る直前に点灯し、panic 時に消灯する(docs/spec.md 7 章)。
 */
#define TAKEOVER_PIN_LED_GREEN 16u
/** @brief オンボード RGB LED の青。使わないので消灯(H)に固定する。 */
#define TAKEOVER_PIN_LED_BLUE 25u

/** @brief 入力 RB(XIAO ピン D10)。押下で L。 */
#define TAKEOVER_PIN_RB 3u
/**
 * @brief 入力 LB(XIAO ピン D9)。押下で L。
 *
 * PIO の `in pins, 2` で RB と LB をまとめて読むため、LB は RB の次の GPIO 番号でなければならない
 * (src/button_input.c で静的に検査している)。
 */
#define TAKEOVER_PIN_LB 4u

/* ------------------------------------------------------------------------- */
/* タイミング定数(docs/spec.md 5 章)。単位はすべてミリ秒。                */
/* T_TOGGLE / T_MANUAL は「経過 >= 定数」、T_PULSE / T_SEL_DELAY は         */
/* 「経過 > 定数」で判定する(docs/spec.md 5 章)。                            */
/* #ifndef で囲んでいるのは、ホストのテストで仕様値と異なる値(-D で指定)でも  */
/* 検査するため(Makefile の ALT_TIMING)。テストの時刻はこれらの定数から計算    */
/* しているので、値を調整してもテストは追従する。ファームウェアではここに書いた */
/* 値が使われる。定数の間の前提は下の TAKEOVER_STATIC_CHECK で検査する。       */
/* ------------------------------------------------------------------------- */

/** @brief T_TOGGLE: 同時押しをこの時間続けたらトグルする。 */
#ifndef TAKEOVER_T_TOGGLE_MS
#define TAKEOVER_T_TOGGLE_MS 2000u
#endif
/**
 * @brief T_MANUAL: 乗っ取り側へのトグルで、同時押しを途切れずにこの時間続けたら(同時押し開始から、経過 >= この値)、
 * 解放を待たずに D0:D1 = H:H(マニュアル待機)にする。
 */
#ifndef TAKEOVER_T_MANUAL_MS
#define TAKEOVER_T_MANUAL_MS 4000u
#endif
/**
 * @brief T_PULSE: 単押しパルス(D0:D1 = L:H / H:L)の長さの下限。
 *
 * パルスを始めた周と終えた周の、メインループ先頭で時刻を読んだ時点(サンプリング時刻)の差は、この値より長く、
 * この値 + 1 ms + メインループ1周分より短い。実際のピンの変化の間隔は、2つの周でサンプリングから出力までに
 * かかった処理時間の差(µs 程度)だけずれうる。
 */
#ifndef TAKEOVER_T_PULSE_MS
#define TAKEOVER_T_PULSE_MS 200u
#endif
/**
 * @brief T_SEL_DELAY: トグル時に D0:D1 を L:L にしてから SEL を反転するまでの待ち時間の下限(暫定値)。
 *
 * サンプリング時刻の差についての保証と、ピンの変化の間隔のずれは T_PULSE と同じ。
 */
#ifndef TAKEOVER_T_SEL_DELAY_MS
#define TAKEOVER_T_SEL_DELAY_MS 10u
#endif

/**
 * @brief コンパイル時に条件を検査する。条件が偽なら配列の大きさが -1 になり、コンパイルエラーになる。
 *
 * C11 の _Static_assert はホストのテスト(-std=c99)で使えないので、C99 でも使えるこの方法にしている。
 * エラーメッセージには name(typedef の名前)が出るので、名前で何を検査しているかが分かるようにする。
 */
#define TAKEOVER_STATIC_CHECK(cond, name) typedef char name[(cond) ? 1 : -1]

/* タイミング定数の前提(docs/spec.md 5 章) */
/** @brief T_SEL_DELAY は 1 ms 以上(D0:D1 を L:L にしてから SEL を反転するまでに待つ時間が必要) */
TAKEOVER_STATIC_CHECK(TAKEOVER_T_SEL_DELAY_MS >= 1u, takeover_check_sel_delay_positive);
/** @brief T_PULSE は 1 ms 以上 */
TAKEOVER_STATIC_CHECK(TAKEOVER_T_PULSE_MS >= 1u, takeover_check_pulse_positive);
/** @brief T_TOGGLE は 1 ms 以上 */
TAKEOVER_STATIC_CHECK(TAKEOVER_T_TOGGLE_MS >= 1u, takeover_check_toggle_positive);
/**
 * @brief T_MANUAL は T_TOGGLE + T_SEL_DELAY より長い。マニュアル待機の表示(H:H)は、トグルで SEL を反転した後に
 * 出るものなので(docs/spec.md 3.1)、4 秒の到達が SEL 反転より前に来る設定は想定しない。
 */
TAKEOVER_STATIC_CHECK(TAKEOVER_T_MANUAL_MS > TAKEOVER_T_TOGGLE_MS + TAKEOVER_T_SEL_DELAY_MS,
                      takeover_check_manual_after_toggle);

/* ------------------------------------------------------------------------- */
/* 同着時の優先(docs/spec.md 3.5)                                          */
/* ------------------------------------------------------------------------- */

/**
 * @brief LB と RB の押下がチャタリング除去後に1件の変化として同時に届いたとき、LB を先押しとみなすなら 1。
 *
 * RB を先押しとみなすように切り替えるには、この値を 0 にする。変更箇所はここだけでよい
 * (src/takeover_logic.c はこの定数だけを見て処理順を決めている)。
 * #ifndef で囲んでいるのは、ホストのテストで -DTAKEOVER_TIE_PREFER_LB=0 を付けて RB 優先版も検査するため。
 */
#ifndef TAKEOVER_TIE_PREFER_LB
#define TAKEOVER_TIE_PREFER_LB 1
#endif

/* ------------------------------------------------------------------------- */
/* ウォッチドッグ(docs/spec.md 6 章)                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief ウォッチドッグを有効にするなら 1。
 *
 * 通常は CMake のオプション TAKEOVER_ENABLE_WATCHDOG(既定 ON)からコンパイル時定義として渡される。
 * 定義が無い場合(CMake を通さないビルド)は有効とする。
 */
#ifndef TAKEOVER_ENABLE_WATCHDOG
#define TAKEOVER_ENABLE_WATCHDOG 1
#endif

/** @brief ウォッチドッグのタイムアウト。メインループがこの時間止まるとリセットされる。 */
#define TAKEOVER_WATCHDOG_TIMEOUT_MS 100u

#endif // TAKEOVER_CONFIG_H
