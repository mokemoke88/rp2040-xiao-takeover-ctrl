/* SPDX-License-Identifier: MIT */
/**
 * @file takeover_logic.h
 * @brief 乗っ取り(SEL)とレンジ指令(D0:D1)の状態遷移ロジック(docs/spec.md 2〜3 章)。
 *
 * ハードウェアに依存しない純粋な状態機械で、Pico SDK を include しない。
 * 時刻は呼び出し側がミリ秒単位の uint32_t で渡す。内部では「現在時刻 - 開始時刻」の差分だけで
 * 経過時間を判定するので、時刻が 0xFFFFFFFF から 0 に戻っても(約 49.7 日ごと)正しく動く。
 *
 * 使い方:
 * 1. takeover_init() で初期化する(状態 S0、出力はすべて L。WARN は引数で指定)
 * 2. 起動時のボタン状態を takeover_on_buttons() で1回渡し、以後はボタンの状態が変化するたびに呼ぶ
 * 3. メインループの毎周で takeover_tick() を呼ぶ(時間経過による遷移を処理する)
 * 4. takeover_get_outputs() で得た値をピンへ出力する
 */
#ifndef TAKEOVER_LOGIC_H
#define TAKEOVER_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

/** @brief ボタン状態ビット: RB(押下 = 1)。PIO が返すビット位置(bit0 = RB)と合わせてある。 */
#define TAKEOVER_BUTTON_RB 0x1u
/** @brief ボタン状態ビット: LB(押下 = 1)。PIO が返すビット位置(bit1 = LB)と合わせてある。 */
#define TAKEOVER_BUTTON_LB 0x2u
/** @brief 有効なボタン状態ビットすべて。 */
#define TAKEOVER_BUTTON_MASK 0x3u

/** @brief 状態(docs/spec.md 2 章)。 */
typedef enum {
  TAKEOVER_STATE_S0,    /**< 解除。SEL=L、D0:D1=L:L。初期状態 */
  TAKEOVER_STATE_S1,    /**< 乗っ取り中・非マニュアル。SEL=H、D0:D1=L:L */
  TAKEOVER_STATE_S2,    /**< マニュアル待機。SEL=H、D0:D1=H:H */
  TAKEOVER_STATE_PULSE, /**< P: パルス出力中。SEL=H、D0:D1=L:H(LB)または H:L(RB) */
  TAKEOVER_STATE_SEL_DELAY, /**< トグル中: D0:D1 を L:L にした後、SEL 反転まで T_SEL_DELAY 待っている。入力はロック */
  TAKEOVER_STATE_LOCK /**< T: トグル後ロック。SEL は反転済み。D0:D1=L:L(SEL=H で同時押しが T_MANUAL に達した後は H:H)。両入力が離されるまで入力を受け付けない。SEL を反転した時刻と同じ時刻には D0:D1 を変えず、抜けもしない */
} takeover_state_t;

/** @brief ピンへ出す値。true = H。warn だけは論理値(true = 警告ラッチ中)で、LED の極性は含まない。 */
typedef struct {
  bool sel;  /**< SEL 出力 */
  bool d0;   /**< D0 出力 */
  bool d1;   /**< D1 出力 */
  bool warn; /**< WARN(警告ラッチ中なら true) */
} takeover_outputs_t;

/**
 * @brief 状態機械のコンテキスト。
 *
 * フィールドは takeover_*() 関数だけが読み書きする。呼び出し側は直接触らない
 * (構造体の中身を公開しているのは、呼び出し側が静的に確保できるようにするため)。
 */
typedef struct {
  takeover_state_t state; /**< 現在の状態 */
  bool sel;               /**< SEL の現在値(SEL_DELAY 中は反転前の値) */
  bool warn;              /**< WARN ラッチ */
  uint8_t pressed;        /**< ボタンの現在の押下状態(TAKEOVER_BUTTON_* の OR) */
  uint8_t ignored; /**< 離されるまで無視しているボタン(起動時に押されていた、または P 中に押された) */
  uint8_t pressed_in_pulse; /**< 今の P の間に押下があったボタン */
  uint8_t priority;         /**< S2 の優先入力(TAKEOVER_BUTTON_* のどれか。無ければ 0) */
  uint8_t pulse_button;     /**< P で出しているパルスのボタン */
  bool both_active;         /**< 同時押しを計時中 */
  bool manual_reached;      /**< 同時押しが途切れずに T_MANUAL 以上続いた */
  bool lock_manual_output;  /**< LOCK 中に D0:D1 = H:H(マニュアル待機の表示)を出している */
  uint32_t both_start_ms;   /**< 同時押し開始の時刻 */
  uint32_t timer_start_ms;  /**< P / SEL_DELAY / LOCK に入った時刻 */
  bool initial_received; /**< 起動時のボタン状態(初期化後の最初の takeover_on_buttons())を受け取った */
} takeover_ctx_t;

/**
 * @brief コンテキストを初期状態(S0、全ボタン解放)にする。
 *
 * warn_latched に true を渡すと、WARN をラッチした状態で始める(ウォッチドッグによる再起動を知らせるため。
 * docs/spec.md 6.1)。解除条件は通常の WARN と同じで、押下を受け付けた瞬間に消える。
 * @param ctx 初期化するコンテキスト
 * @param warn_latched WARN をラッチした状態で始めるなら true
 */
void takeover_init(takeover_ctx_t *ctx, bool warn_latched);

/**
 * @brief チャタリング除去済みのボタン状態を渡す。
 *
 * takeover_init() 後の最初の呼び出しは「起動時のボタン状態」として扱う。このとき押されているボタンは
 * 操作とみなさず、一度離されるまで無視する(同時押しの判定にも数えず、WARN にもしない。docs/spec.md 4.1)。
 * 以降の呼び出しでは、前回渡した状態と比べて変化したビットから押下・解放を判定する。処理の前に
 * takeover_tick() と同じ時間経過の処理を now_ms で行うので、同じ時刻に期限を迎えた
 * 遷移(パルス終了など)は、このイベントより先に起きたものとして扱われる。
 *
 * 2ビットが同時に変化していた場合は、2つの変化が同じ時刻に次の順で起きたものとして処理する:
 * 1. 解放(両方なら、S2 の優先入力ではないほうを先に)
 * 2. 押下(両方なら TAKEOVER_TIE_PREFER_LB で決まる側を先に。docs/spec.md 3.5)
 * (この順序は docs/spec.md 3.6)
 *
 * @param ctx コンテキスト
 * @param pressed_bits 現在の押下状態(TAKEOVER_BUTTON_LB / TAKEOVER_BUTTON_RB の OR、押下 = 1)。範囲外のビットは無視する
 * @param now_ms 現在時刻(ミリ秒)
 */
void takeover_on_buttons(takeover_ctx_t *ctx, uint8_t pressed_bits, uint32_t now_ms);

/**
 * @brief 時間経過による遷移(トグル開始、SEL 反転、パルス終了など)を処理する。
 *
 * メインループの毎周で呼ぶ。呼ぶ間隔が空くと、その分だけ遷移が遅れる。
 * @param ctx コンテキスト
 * @param now_ms 現在時刻(ミリ秒)
 */
void takeover_tick(takeover_ctx_t *ctx, uint32_t now_ms);

/**
 * @brief 現在の状態に対応する出力値を返す。
 * @param ctx コンテキスト
 * @return SEL / D0 / D1 / WARN の値
 */
takeover_outputs_t takeover_get_outputs(const takeover_ctx_t *ctx);

/**
 * @brief 現在の状態を返す(テストとデバッグ用)。
 * @param ctx コンテキスト
 * @return 現在の状態
 */
takeover_state_t takeover_get_state(const takeover_ctx_t *ctx);

#endif // TAKEOVER_LOGIC_H
