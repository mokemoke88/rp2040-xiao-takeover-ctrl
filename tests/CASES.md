# テストケース対応表

`tests/test_takeover_logic.c` の各テストが検証している入力と期待値。

## ビルド構成

`make test` は同じテストを 3 つの構成でビルドして実行する。

| 実行ファイル | 構成 |
|---|---|
| `build-host/test_takeover_logic` | 仕様どおりの定数、同着時 LB 優先(既定) |
| `build-host/test_takeover_logic_rb` | `-DTAKEOVER_TIE_PREFER_LB=0` で RB 優先 |
| `build-host/test_takeover_logic_alt` | タイミング定数を仕様値と違う値にした構成(Makefile の `ALT_TIMING`: T_TOGGLE = 700、T_MANUAL = 1500、T_PULSE = 50、T_SEL_DELAY = 3) |

- テスト中の時刻はすべて `takeover_config.h` の定数から計算している。定数を調整しても、前提(下記)を満たす限りテストはそのまま通る
- 同着に関わるテストは、期待値を優先側の定数から決めているので、LB / RB どちらの設定でも PASS する

## 表記

- 入力 `btn(X, t)`: 時刻 t [ms] に、チャタリング除去済みの押下状態 X を渡す(`LB` / `RB` / `BOTH` = 両方押下 / `NONE` = 両方解放)。前回との差分が押下・解放になる
- 入力 `tick(t)`: 時刻 t にメインループの時間処理を行う
- 出力 `SEL D0 D1 WARN` を 1 / 0 で書く(1 = H。WARN は 1 = 警告ラッチ中)
- 各テストの前に、初期化と「起動時は何も押されていない」状態(`btn(NONE, 0)`)を渡してある。起動時の押下を扱うテストだけは、自分で初期化し直す
- 定数は `T_TOGGLE` / `T_MANUAL` / `T_PULSE` / `T_SEL_DELAY`(`TAKEOVER_T_*_MS`)と書く。テストで使う派生値は次のとおり。( )内は仕様値での値

| 記号 | 式 | 意味 | 仕様値 |
|---|---|---|---|
| `s` | — | 同時押しの開始時刻 | — |
| `T_FLIP` | T_TOGGLE + T_SEL_DELAY + 1 | 同時押し開始から SEL が反転するまで(T_TOGGLE で SEL_DELAY に入り、経過 > T_SEL_DELAY で反転) | 2011 |
| `T_PULSE_END` | T_PULSE + 1 | パルス開始から終了まで(経過 > T_PULSE) | 201 |
| `T_IN_PULSE` | T_PULSE / 2 + 1 | P の途中の操作を入れる、パルス開始からの時間 | 101 |
| `T_LONG` | 2 × T_MANUAL | どのタイマーの期限よりも長い時間 | 8000 |
| `T_COARSE` | T_MANUAL / 4 + 1 | 呼び出し間隔が粗い場合の遅れ | 1001 |
| `r` | — | パルスの開始時刻 | — |
| `t` | — | `S1 から` / `S2 から` のテストで、補助関数 enter_s1 / enter_s2 で S1 / S2 に遷移させた後の起点時刻 | — |

- 補助関数: enter_s1(t) は btn(BOTH, t) → tick(t + T_TOGGLE) → tick(t + T_FLIP) → btn(NONE, t + T_FLIP + 1) で S1 にする。enter_s2(t) は同様に btn(NONE, t + T_MANUAL) で S2 にする。start_lb_pulse(t) は S2 で btn(LB, t) → btn(NONE, t + 1) としてパルスを始め、r = t + 1 を返す
- `先押し側` / `後押し側` は同着時に先押しとみなす側 / もう片方(LB 優先版では LB / RB)
- 境界: トグル・マニュアルは経過 ≥ 定数。パルス終了・SEL 反転は経過 > 定数

### 定数の前提

| 前提 | 検査している場所 | 理由 |
|---|---|---|
| T_SEL_DELAY ≥ 1、T_PULSE ≥ 1、T_TOGGLE ≥ 1 | `takeover_config.h`(TAKEOVER_STATIC_CHECK) | 待ち時間として意味を持つ値 |
| T_MANUAL > T_TOGGLE + T_SEL_DELAY | `takeover_config.h` | マニュアル待機の表示(H:H)は SEL 反転の後に出るもの(spec 3.1) |
| T_MANUAL ≥ T_FLIP + 2 | テストファイル | SEL 反転の直後(s + T_FLIP + 1)に離して S1 になることを確かめるため |
| T_PULSE ≥ 2 | テストファイル | P の途中(r + 1、r + 2、r + T_IN_PULSE)に操作を置くため |

前提に反する値にすると、ホストのテスト・ファームウェアのどちらもコンパイルエラーになる(C99 でも使える「大きさが負の配列」の方法で検査している)。

## 初期状態、起動時の押下(4.1)、ウォッチドッグ再起動時の WARN(6.1)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_init_is_s0_all_low | 初期化のみ | S0、`0000` | 2 章 |
| test_boot_pressed_button_ignored_until_release | 起動時 btn(LB,20) → btn(BOTH,100) → tick(100 + T_LONG) → btn(RB, 同)(LB 解放)→ btn(BOTH, s = 101 + T_LONG) → tick s + T_TOGGLE − 1 / s + T_TOGGLE | 起動時: S0 `0000`、100 + T_LONG: S0(LB は無視中で同時押しにならない)、LB 解放: `0000`(WARN なし)、s + T_TOGGLE − 1: S0、s + T_TOGGLE: SEL_DELAY | 4.1 |
| test_boot_both_pressed_never_toggles | 起動時 btn(BOTH,20) → tick(20 + T_LONG) → btn(NONE, 同) → btn(BOTH, +1) → tick(+ T_TOGGLE) | T_LONG 後も S0、解放: S0 `0000`、離した後の同時押しで SEL_DELAY | 4.1 |
| test_init_with_warn_latched_cleared_by_press_in_s0 | takeover_init(true) → 起動時 btn(NONE,20) → tick(20 + T_LONG) → btn(LB, 同) | 初期化直後: `0001`、T_LONG 後: S0 `0001`、LB 押下: `0000`(S0 での押下で解除) | 6.1、3.4 |
| test_init_with_warn_latched_not_cleared_by_boot_pressed_button | takeover_init(true) → 起動時 btn(LB,20) → btn(NONE,21) → btn(RB,22) | 起動時: `0001`、LB 解放: `0001`(無視中のボタンの解放では解除しない)、RB 押下: `0000` | 6.1、4.1 |
| test_extra_bits_are_ignored | btn(0xFC,1000), tick(1000 + T_LONG) | S0、`0000`(bit0/1 以外は無視) | API |

## トグル(3.1)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_toggle_s0_to_s1_sequence | btn(BOTH, s) → tick s + T_TOGGLE − 1 / s + T_TOGGLE / s + T_TOGGLE + T_SEL_DELAY / s + T_FLIP → btn(NONE, s + T_FLIP + 1) | S0 `0000` → SEL_DELAY `0000` → SEL_DELAY `0000`(経過 = T_SEL_DELAY ではまだ) → LOCK `1000` → S1 `1000` | 3.1 の 2、境界 ≥ T_TOGGLE / > T_SEL_DELAY |
| test_toggle_not_before_t_toggle | btn(BOTH, s), tick(s + T_TOGGLE − 1), btn(LB, 同), tick(s + T_LONG) | S0 `0000`(T_TOGGLE − 1 で離すとトグルしない) | 3.1 |
| test_release_event_at_exactly_t_toggle_toggles_first | btn(BOTH, s), btn(LB, s + T_TOGGLE)(直前に tick なし) | SEL_DELAY → tick(s + T_FLIP) で LOCK `1000` → btn(NONE) で S1 | 経過 ≥ T_TOGGLE の時刻に届いた解放より、トグルが先 |
| test_toggle_timer_restarts_on_repress | btn(BOTH, s), btn(LB, s + T_TOGGLE − 1), btn(BOTH, s2 = s + T_TOGGLE), tick s2 + T_TOGGLE − 1 / s2 + T_TOGGLE | S0 → SEL_DELAY(計時は押し直しから) | 3.1 の 1 |
| test_lock_waits_for_both_released_without_warn | トグル後 LOCK で RB だけ残す → 両方押し直し → LB だけ → NONE(s + T_FLIP + 1〜4) | 途中はすべて LOCK `1000`(WARN にならない)、NONE で S1 | 3.1 の 3 |
| test_release_during_sel_delay_finishes_after_sel_flip | btn(BOTH, s), tick(s + T_TOGGLE), btn(NONE, s + T_TOGGLE + T_SEL_DELAY), tick s + T_FLIP / s + T_FLIP + 1 | 解放: SEL_DELAY `0000`、s + T_FLIP: LOCK `1000`(同じ時刻には抜けない)、s + T_FLIP + 1: S1 `1000` | 3.1 |
| test_toggle_s1_to_s0 | S1 から btn(BOTH, t), tick t + T_TOGGLE / t + T_FLIP, btn(NONE, t + T_FLIP + 1) | SEL_DELAY `1000` → LOCK `0000` → S0 `0000` | 3.1 |
| test_toggle_s1_to_s0_even_if_held_t_manual | S1 から同時押し、tick t + T_TOGGLE / t + T_FLIP / t + T_MANUAL / t + T_LONG, btn(NONE, t + T_LONG) | T_MANUAL・T_LONG とも LOCK `0000`(SEL=L 側では何もしない)、解放: S0 `0000` | 3.1 |
| test_toggle_s2_to_s0_drive_range_before_sel | S2 から btn(BOTH, t), tick t + T_TOGGLE − 1 / t + T_TOGGLE / t + T_TOGGLE + T_SEL_DELAY / t + T_FLIP, btn(NONE, t + T_FLIP + 1) | S2 `1110` → `1000`(D0:D1 が先に L:L) → `1000` → `0000`(SEL 反転) → S0。パルスは出ない | 3.1 の 2、3.2 |
| test_s2_toggle_release_during_sel_delay | S2 から btn(BOTH, t), tick(t + T_TOGGLE), btn(NONE, t + T_TOGGLE + T_SEL_DELAY), tick t + T_FLIP / t + T_FLIP + 1 | SEL_DELAY `1000` → 解放しても SEL_DELAY `1000`(パルスなし) → LOCK `0000` → S0 `0000` | 3.1 |
| test_toggle_after_pulse_with_carried_priority | S2 から LB → BOTH → LB 解放(r、パルス、RB が次の優先入力)→ tick(r + T_PULSE_END) → btn(BOTH, s = r + T_PULSE_END + 1) → tick s + T_TOGGLE − 1 / s + T_TOGGLE / s + T_FLIP → btn(NONE) | S2 → SEL_DELAY `1000` → LOCK `0000` → S0 | 3.1、3.2(P 後に他方を押して同時押し) |

## マニュアル待機の表示(3.1 の 3、4)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_manual_hh_while_held_at_t_manual | btn(BOTH, s) … tick s + T_MANUAL − 1 / s + T_MANUAL → btn(NONE, s + T_MANUAL + 1) | T_MANUAL − 1: LOCK `1000`、T_MANUAL: LOCK `1110`(解放を待たずに H:H)、解放: S2 `1110` | 3.1 の 3、境界 ≥ T_MANUAL |
| test_manual_at_exactly_t_manual_release_event | btn(BOTH, s) … tick(s + T_MANUAL − 1), btn(NONE, s + T_MANUAL)(tick なし) | S2 `1110` | 3.1、境界 ≥ T_MANUAL(解放イベントの時刻で到達) |
| test_manual_one_released_at_exactly_t_manual | btn(BOTH, s) … tick(s + T_MANUAL − 1), btn(RB, s + T_MANUAL)(LB を解放), btn(NONE, +1) | LB 解放: LOCK `1110`(到達とみなして H:H、LOCK は続く)、両解放: S2 `1110` | 3.1 の 3、境界 |
| test_no_manual_at_t_manual_minus_1 | btn(BOTH, s) … btn(NONE, s + T_MANUAL − 1) | S1 `1000` | 3.1 の 4 |
| test_manual_not_reached_if_one_released_at_t_manual_minus_1 | btn(BOTH, s) … btn(RB, s + T_MANUAL − 1)(LB 解放), tick s + T_MANUAL / s + T_LONG, btn(NONE, s + T_LONG) | T_MANUAL・T_LONG とも LOCK `1000`(RB を押し続けても H:H にならない)、解放: S1 | 3.1「最初に離された時点まで」 |
| test_manual_hh_input_locked_until_both_released | H:H になった後、RB だけ残す → 両方押し直し → tick(+ T_LONG) → LB だけ → NONE | 途中はすべて LOCK `1110`(パルスも WARN もなし)、NONE: S2 `1110` | 3.1 の 3 |
| test_manual_measured_until_first_release | btn(BOTH, s) … btn(RB, s + T_FLIP + 1)(LB 解放), tick(s + T_LONG), btn(NONE, s + T_LONG) | T_LONG: LOCK `1000`(H:H にならない)、解放: S1 `1000` | 3.1 |
| test_manual_not_extended_by_repress_in_lock | btn(BOTH, s) … btn(RB, s + T_FLIP + 1), btn(BOTH, s + T_FLIP + 2), tick(+ T_LONG), btn(NONE) | `1000`、解放: S1(LOCK 中の押し直しで T_MANUAL の判定は再開しない) | 3.1 |
| test_lock_not_left_at_same_ms_as_sel_flip | btn(BOTH, s), tick(s + T_TOGGLE), btn(NONE, L = s + T_MANUAL + T_COARSE)(この1回で SEL 反転・T_MANUAL 到達・両解放), tick(L + 1) | L: LOCK `1000`(H:H は出さない)、L + 1: S2 `1110` | 3.1 安全上の考え方(呼び出し間隔が粗い場合) |
| test_manual_hh_not_at_same_ms_as_sel_flip_while_held | btn(BOTH, s), tick(s + T_TOGGLE), tick(L)(SEL 反転と T_MANUAL 到達が同じ tick), tick(L + 1), btn(NONE, L + 2) | L: LOCK `1000`、L + 1: LOCK `1110`、解放: S2 `1110` | 3.1 安全上の考え方(押し続けている場合) |
| test_manual_hh_not_at_same_ms_as_sel_flip_one_released | btn(BOTH, s), tick(s + T_TOGGLE), btn(RB, L)(SEL 反転・T_MANUAL 到達・LB 解放が同じ呼び出し), tick(L + 1), btn(NONE, L + 2) | L: LOCK `1000`、L + 1: LOCK `1110`、両解放: S2 `1110` | 3.1 安全上の考え方(片方を離した場合) |

## 単押し(3.2)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_s0_single_press_ignored | LB を T_LONG 押して離す → RB を T_LONG 押して離す | S0 `0000` | 3.2 |
| test_s1_single_press_ignored | S1 から LB を T_LONG 押して離す → RB を押して離す | S1 `1000` | 3.2 |
| test_s2_lb_pulse_on_release | S2 から btn(LB, t), btn(NONE, r = t + 100), tick r + T_PULSE / r + T_PULSE_END | 押下時: S2 `1110`、解放: P `1010`(L:H)、r + T_PULSE: `1010`(経過 = T_PULSE ではまだ P)、r + T_PULSE_END: S2 `1110` | 3.2、T_PULSE を超えて終わる |
| test_s2_rb_pulse_on_release | 上の RB 版 | 解放: P `1100`(H:L)、r + T_PULSE: `1100`、r + T_PULSE_END: S2 `1110` | 3.2 |
| test_s2_second_button_takes_over_as_priority | S2 から LB → BOTH → LB 解放(r)→ tick(r + T_PULSE_END) → RB 解放(r2 = r + T_PULSE_END + 1)→ tick(r2 + T_PULSE_END) | P `1010` → S2 → P `1100` → S2 `1110` | 3.2 次の優先入力 |
| test_s2_non_priority_release_gives_no_pulse | S2 から LB → BOTH → RB 解放 → LB 解放 | RB 解放: S2 `1110`(パルスなし)、LB 解放: P `1010` | 3.2 |
| test_s2_tie_press_first_side_is_priority | S2 から btn(BOTH, t)(同着)→ 後押し側を解放 → 先押し側を解放 | 後押し側の解放: S2 `1110`、先押し側の解放: 先押し側のパルス | 3.5 |
| test_s2_tie_press_second_side_becomes_next_priority | S2 から btn(BOTH, t) → 先押し側を解放(r)→ tick(r + T_PULSE_END) → 後押し側を解放 | 先押し側のパルス → 後押し側のパルス | 3.5、3.2 |
| test_s2_simultaneous_release_lb_priority_single_pulse | S2 から LB → BOTH → btn(NONE, r)(同時解放) | P `1010`、r + T_PULSE_END: S2 `1110`(WARN なし) | 3.2 同時解放、3.6 |
| test_s2_simultaneous_release_rb_priority_single_pulse | S2 から RB → BOTH → btn(NONE, r) | P `1100`、r + T_PULSE_END: S2 `1110`(WARN なし) | 同上 |
| test_event_at_pulse_end_is_handled_after_pulse | start_lb_pulse → btn(RB, r + T_PULSE_END)(tick なし)→ btn(NONE, +1) | r + T_PULSE_END: S2 `1110`(P 終了後の押下として受け付け)、解放: P `1100` | 3.3 境界 |

## P 中の入力と WARN(3.3、3.4)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_carried_priority_released_in_pulse_warns_without_pulse | S2 から LB → BOTH → LB 解放(r)→ btn(NONE, r + T_IN_PULSE)(P 中に RB 解放)→ tick r + T_PULSE_END / + T_LONG | P `1011`、P 終了: S2 `1111`、以後パルスなし | 3.3 の例 |
| test_press_at_pulse_last_ms_is_in_pulse | start_lb_pulse → btn(RB, r + T_PULSE) | P `1011`(経過 = T_PULSE の押下はまだ P 中) | 3.3 境界 |
| test_press_in_pulse_warns_and_is_ignored_until_release | start_lb_pulse → btn(RB, r + T_IN_PULSE) → tick(e = r + T_PULSE_END) → RB 解放 → RB 押下 → RB 解放(e + 1〜3) | 押下: `1011`、P 終了: S2 `1111`、RB 解放: S2 `1111`(パルスも WARN 解除もなし)、再押下: `1110`、解放: P `1100` | 3.3 の 2 項、3.4 |
| test_ignored_button_not_counted_as_simultaneous | 上と同様に RB を無視中にし、LB を押して T_TOGGLE + 1 → LB 解放 → RB 解放 | LB 押下: `1110`(WARN 解除)、T_TOGGLE を過ぎても S2(トグルしない)、LB 解放: P `1010`、RB 解放: S2 `1110` | 3.3「同時押しの判定にも数えない」 |
| test_ignored_button_released_in_pulse_does_not_warn | RB を無視中にし、LB 押下 → LB 解放(r2、P)→ btn(NONE, r2 + T_IN_PULSE)(P 中に RB 解放) | LB 解放: P `1010`(RB は優先入力にならない)、RB 解放: `1010`(WARN なし)、P 終了: S2 `1110` | 3.3 最終項 |
| test_press_and_release_within_pulse_is_not_ignored | start_lb_pulse → RB 押下(r + 1)→ RB 解放(r + 2)→ tick(e = r + T_PULSE_END) → RB 押下 → 解放 | P 終了: `1111`、RB 押下: `1110`、解放: P `1100` | 3.3、3.4 |
| test_release_priority_and_press_other_in_same_event | S2 で btn(LB, t), btn(RB, r = t + 1)(LB 解放と RB 押下が1件) | P `1011`、P 終了後 RB 解放: S2 `1111` | 3.6 |
| test_warn_cleared_by_accepted_press_in_s2 | WARN をラッチした S2 で btn(LB) | `1111` → `1110` | 3.4 |

## 時刻のラップアラウンド

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_wraparound_toggle_and_manual | t0 = 0xFFFFFFFF − T_TOGGLE / 2 で btn(BOTH), tick t0 + T_TOGGLE − 1 / + T_TOGGLE / + T_TOGGLE + T_SEL_DELAY / + T_FLIP / + T_MANUAL − 1 / + T_MANUAL(途中で 0 をまたぐ), btn(NONE, t0 + T_MANUAL) | S0 → SEL_DELAY → SEL_DELAY → LOCK → `1000` → `1110` → S2 `1110` | 差分比較 |
| test_wraparound_pulse | パルス開始 r = 0xFFFFFFFF − T_PULSE / 2 になるよう逆算して S2 で RB パルス、tick r + T_PULSE / r + T_PULSE_END(途中で 0 をまたぐ) | P → S2 | 差分比較 |

## 乱択による安全不変条件(test_random_safety_invariants)

固定シード(20260926)の線形合同法で操作を生成する。

- 起動からやり直す区間を 600 回繰り返し、各区間で 500 回操作する(計 30 万回)
- 各区間の始めに、WARN の初期値(1/4 の確率で WARN ラッチ済み)と起動時の押下状態を乱択で決める
  - 起動直後に「S0、押されていたボタン = 無視中、WARN = 初期値」であることを確認する
- 操作: `tick` か、ランダムな押下状態での `btn`
- 時間の進め方(どれも定数から計算):
  - 1/4: 次に何かが起きうる期限の前後 1 ms(期限 − 1 / 期限 / 期限 + 1)にちょうど止まるように進める。期限は、同時押し開始 + T_TOGGLE / + T_MANUAL、トグルを起こした同時押しの開始 + T_MANUAL、P / SEL_DELAY / LOCK に入った時刻 + T_PULSE / + T_SEL_DELAY / + 0
  - 1/4: 境界付近の間隔の候補(0〜2、T_SEL_DELAY − 1〜+ 2、T_PULSE − 1〜+ 2、T_TOGGLE − 1〜+ 1、T_MANUAL − T_FLIP、T_MANUAL − T_TOGGLE、T_MANUAL − 1〜+ 1、T_COARSE)
  - 1/2: 0〜T_PULSE + T_SEL_DELAY + 1 の乱数
- 時刻の起点: 0xFFFFFFFF − 5000 万。途中で 0 に戻る

毎回の呼び出しの後に、次を検査する。「同時押しの長さ」は、トグルを起こした同時押しを途切れずに続けた長さ(最初の解放まで。まだ続いていれば今まで)。

| 不変条件 | 内容 |
|---|---|
| 押下状態の一致 | ロジックが持つ押下状態 = 入力、無視中のボタン ⊆ 押されているボタン |
| SEL=L ならドライブレンジ | SEL = L の間は D0:D1 = L:L |
| SEL 変化の前後 | SEL が変わった呼び出しでは、直前と直後の出力がともに D0:D1 = L:L で、L:L になってからの経過が T_SEL_DELAY を超えている |
| H:H 変化と SEL 変化の分離 | D0:D1 が H:H になった呼び出しでは SEL が変わっておらず、その時刻は最後に SEL が変わった呼び出しの時刻と異なる |
| LOCK 中の H:H(安全) | LOCK 中に H:H を出しているのは、SEL = H で、同時押しの長さが T_MANUAL 以上のときだけ |
| LOCK 中の H:H(遅れない) | LOCK 中、SEL = H、同時押しの長さ ≥ T_MANUAL、SEL 反転と別の時刻なら、必ず H:H |
| LOCK 中の H:H の保持 | LOCK 中は H:H から L:L に戻らない |
| LOCK → S2 | 同時押しの長さが T_MANUAL 以上のときだけ |
| LOCK → S1 | 同時押しの長さが T_MANUAL 未満のときだけ(T_MANUAL 以上続いていれば S1 にならない) |
| トグルの条件 | SEL_DELAY に入った呼び出しの直前の入力は両方押しで、両方押しになってから T_TOGGLE 以上経過しており、直前に無視中のボタンがない |
| パルスの開始 | パルスが始まるのは btn 呼び出しの中だけで、その呼び出しでパルスのボタンが押下 → 解放に変わっている |
| パルスの条件 | P の間は SEL = H かつ D0 ≠ D1。P が終わる(または次のパルスに替わる)ときは、開始から T_PULSE を超えている |
| WARN の解除 | WARN が消えるのは btn 呼び出しの中だけで、その呼び出しで新たに押されたボタンがある |
| WARN のラッチ | WARN が点くのは、呼び出しの前か後の状態が P のとき(P 中の変化、P の終了時、P 開始と同時の押下)だけ |
| 網羅の確認 | SEL の変化 100 回以上、パルス 100 回以上、LOCK → S2 10 回以上、LOCK 中の H:H 10 回以上、同時押しの長さがちょうど T_MANUAL の時刻で H:H を確認 5 回以上、WARN のラッチ・解除 各 10 回以上、起動時押下あり 100 区間以上、WARN 付き起動 50 区間以上 |

変異テスト(2026-09-26、使い捨てのスクリプトで実施)で、次の変異は 3 構成すべてでこの乱択テストが検出した。

- LOCK 中の T_MANUAL 判定を「途切れない同時押し」ではなく「今両方押されている」にする
- T_MANUAL の判定を ≥ から > にする

## 仕様 3.6 で決めた処理順(同時に届いた2ビットの変化)

解放を先、押下を後に処理する。

- 解放同士: S2 の優先入力でないほうを先
- 押下同士: 同着優先側を先

これにより、S2 での同時解放は、どちらが優先入力でもパルス1回・WARN なしになる。

## テストで確認できないもの

- S1 での押下による WARN 解除(3.4)は、状態として作れない
  - WARN がラッチされるのは S2 / P と起動時(6.1)だけで、S1 に移るには押下を受け付ける(= WARN が解除される)必要がある
  - S0 での解除は test_init_with_warn_latched_cleared_by_press_in_s0 で確認している。コードでは S0 / S1 / S2 の押下を同じ分岐で処理している
- ウォッチドッグ再起動の判定(watchdog_enable_caused_reboot())、緑 LED、panic フックはハードウェア側の処理なので、ホストでは確認できない(実機確認)
- PIO のチャタリング除去と起動時の最初の push
  - `tools/verification/verify_debounce_pio.py` で、ビルド生成物 `build/debounce.pio.h` の命令語を模擬実行して確認する(実機確認は別途)
