# テストケース対応表

`tests/test_takeover_logic.c` の各テストが検証している入力と期待値。

`make test` は同じテストを2回ビルドして実行する。

- `build-host/test_takeover_logic`: 同着時 LB 優先(既定)
- `build-host/test_takeover_logic_rb`: `-DTAKEOVER_TIE_PREFER_LB=0` で RB 優先

同着に関わるテストは、期待値を優先側の定数から決めているので、両方の設定で PASS する。

## 表記

- 入力 `btn(X, t)`: 時刻 t [ms] に、チャタリング除去済みの押下状態 X を渡す(`LB` / `RB` / `BOTH` = 両方押下 / `NONE` = 両方解放)。前回との差分が押下・解放になる
- 入力 `tick(t)`: 時刻 t にメインループの時間処理を行う
- 出力 `SEL D0 D1 WARN` を 1 / 0 で書く(1 = H。WARN は 1 = 警告ラッチ中)
- 各テストの前に、初期化と「起動時は何も押されていない」状態(`btn(NONE, 0)`)を渡してある。起動時の押下を扱うテストだけは、自分で初期化し直す
- `S1 から` / `S2 から` は、補助関数で S0 → S1(同時押し 2.1 秒)/ S0 → S2(同時押し 4 秒)に遷移させた後の時刻 t を起点にする
- `先押し側` / `後押し側` は同着時に先押しとみなす側 / もう片方(LB 優先版では LB / RB)
- 定数は仕様どおり T_TOGGLE = 2000、T_MANUAL = 4000、T_PULSE = 200、T_SEL_DELAY = 10 を前提にしている(テスト中の時刻はこの値で書いてある)
- 境界: トグル・マニュアルは経過 ≥ 定数。パルス終了・SEL 反転は経過 > 定数(パルスは開始 +201 ms で終わり、SEL は SEL_DELAY 開始 +11 ms で反転する)

## 初期状態、起動時の押下(4.1)、ウォッチドッグ再起動時の WARN(6.1)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_init_is_s0_all_low | 初期化のみ | S0、`0 0 0 0` | 2 章 |
| test_boot_pressed_button_ignored_until_release | 起動時 btn(LB,20) → btn(BOTH,100) → tick(5000) → btn(RB,5000)(LB 解放)→ btn(BOTH,5100) → tick 7099 / 7100 | 起動時: S0 `0000`、5000: S0(LB は無視中で同時押しにならない)、LB 解放: `0000`(WARN なし)、7099: S0、7100: SEL_DELAY | 4.1 |
| test_boot_both_pressed_never_toggles | 起動時 btn(BOTH,20) → tick(10000) → btn(NONE,10000) → btn(BOTH,11000) → tick(13000) | 10000: S0、解放: S0 `0000`、13000: SEL_DELAY(離した後は通常どおり) | 4.1 |
| test_init_with_warn_latched_cleared_by_press_in_s0 | takeover_init(true) → 起動時 btn(NONE,20) → tick(5000) → btn(LB,5000) | 初期化直後: `0001`、5000: S0 `0001`、LB 押下: `0000`(S0 での押下で解除) | 6.1、3.4 |
| test_init_with_warn_latched_not_cleared_by_boot_pressed_button | takeover_init(true) → 起動時 btn(LB,20) → btn(NONE,500) → btn(RB,600) | 起動時: `0001`、LB 解放: `0001`(無視中のボタンの解放では解除しない)、RB 押下: `0000` | 6.1、4.1 |
| test_extra_bits_are_ignored | btn(0xFC,1000), tick(5000) | S0、`0 0 0 0`(bit0/1 以外は無視) | API |

## トグル(3.1)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_toggle_s0_to_s1_sequence | btn(BOTH,1000) → tick 2999 / 3000 / 3010 / 3011 → btn(NONE,3500) | 2999: S0 `0000`、3000: SEL_DELAY `0000`、3010: SEL_DELAY `0000`、3011: LOCK `1000`、3500: S1 `1000` | 3.1 の 2、境界 ≥2000 / >10 |
| test_toggle_not_before_2000ms | btn(BOTH,1000), tick(2999), btn(LB,2999), tick(10000) | S0 `0000`(1999ms で離すとトグルしない) | 3.1 |
| test_release_event_at_exactly_2000ms_toggles_first | btn(BOTH,1000), btn(LB,3000)(直前に tick なし) | SEL_DELAY → tick(3011) で LOCK `1000` → btn(NONE) で S1 | 経過 ≥2000 の時刻に届いた解放より、トグルが先 |
| test_toggle_timer_restarts_on_repress | btn(BOTH,1000), btn(LB,2500), btn(BOTH,2600), tick 4599 / 4600 | 4599: S0、4600: SEL_DELAY(計時は押し直しから) | 3.1 の 1 |
| test_lock_waits_for_both_released_without_warn | トグル後 LOCK で RB だけ残す → 両方押し直し → LB だけ → NONE | 途中はすべて LOCK `1000`(WARN にならない)、NONE で S1 | 3.1 の 3 |
| test_release_during_sel_delay_finishes_after_sel_flip | btn(BOTH,1000), tick(3000), btn(NONE,3005), tick 3010 / 3011 / 3012 | 3005: SEL_DELAY `0000`、3010: SEL_DELAY、3011: LOCK `1000`、3012: S1 `1000` | 3.1、SEL 反転と同じ時刻には T を抜けない |
| test_lock_not_left_at_same_ms_as_sel_flip | btn(BOTH,1000), tick(3000), btn(NONE,5500)(この1回で SEL 反転・4 秒以上・解放), tick(5501) | 5500: LOCK `1000`(H:H は出さない)、5501: S2 `1110` | 3.1 安全上の考え方 |
| test_manual_at_exactly_4000ms | btn(BOTH,1000) … btn(NONE,5000) | S2 `1110` | 3.1 の 4、境界 ≥4000 |
| test_no_manual_at_3999ms | btn(BOTH,1000) … btn(NONE,4999) | S1 `1000` | 3.1 の 4 |
| test_manual_recorded_by_tick_while_held | btn(BOTH,1000) … tick(5000), btn(NONE,7000) | S2 | 3.1 の 4 |
| test_manual_measured_until_first_release | btn(BOTH,1000) … btn(RB,4000)(LB を 3 秒で解放), btn(NONE,6000)(RB は 5 秒) | S1 | 3.1「最初に離された時点まで」 |
| test_manual_not_extended_by_repress_in_lock | btn(BOTH,1000) … btn(RB,3500), btn(BOTH,3600), btn(NONE,8000) | S1(LOCK 中の押し直しは計時に含めない) | 3.1 |
| test_toggle_s1_to_s0 | S1 から btn(BOTH,t), tick t+2000 / t+2011, btn(NONE) | t+2000: SEL_DELAY `1000`、t+2011: LOCK `0000`、解放: S0 `0000` | 3.1 |
| test_toggle_s1_to_s0_even_if_held_4000ms | S1 から 5 秒同時押し | S0 `0000`(SEL=L なので 4 秒以上でも S0) | 3.1 の 4 |
| test_toggle_s2_to_s0_drive_range_before_sel | S2 から btn(BOTH,t), tick t+1999 / t+2000 / t+2010 / t+2011, btn(NONE) | t+1999: S2 `1110`、t+2000: `1000`(D0:D1 が先に L:L)、t+2010: `1000`、t+2011: `0000`(SEL 反転)、解放: S0。パルスは出ない | 3.1 の 2、3.2 |
| test_s2_toggle_release_during_sel_delay | S2 から btn(BOTH,t), tick(t+2000), btn(NONE,t+2005), tick t+2010 / t+2011 / t+2012 | t+2000: SEL_DELAY `1000`、解放: SEL_DELAY `1000`(パルスなし)、t+2011: LOCK `0000`、t+2012: S0 `0000` | 3.1 |
| test_toggle_after_pulse_with_carried_priority | S2 から LB → BOTH → LB 解放(パルス、RB が次の優先入力)→ tick(t+301) → btn(BOTH,t+400) → tick t+2399 / t+2400 / t+2411 → btn(NONE) | t+2399: S2、t+2400: SEL_DELAY `1000`、t+2411: LOCK `0000`、解放: S0 | 3.1、3.2(P 後に他方を押して同時押し) |

## 単押し(3.2)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_s0_single_press_ignored | LB を 3 秒、RB を 3 秒押して離す | S0 `0000` | 3.2 |
| test_s1_single_press_ignored | S1 から LB、RB を押して離す | S1 `1000` | 3.2 |
| test_s2_lb_pulse_on_release | S2 から btn(LB,t), btn(NONE,t+100), tick t+300 / t+301 | 押下時: S2 `1110`、解放: P `1010`(L:H)、t+300: `1010`(経過 = 200 ではまだ P)、t+301: S2 `1110` | 3.2、T_PULSE >200 |
| test_s2_rb_pulse_on_release | 上の RB 版 | 解放: P `1100`(H:L)、t+300: `1100`、t+301: S2 `1110` | 3.2 |
| test_s2_second_button_takes_over_as_priority | S2 から LB → BOTH → LB 解放(t+100) → tick(t+301) → RB 解放(t+400) | t+100: P `1010`、t+301: S2、t+400: P `1100`、t+601: S2 `1110` | 3.2 次の優先入力 |
| test_s2_non_priority_release_gives_no_pulse | S2 から LB → BOTH → RB 解放 → LB 解放 | RB 解放: S2 `1110`(パルスなし)、LB 解放: P `1010` | 3.2 |
| test_s2_tie_press_first_side_is_priority | S2 から btn(BOTH,t)(同着)→ 後押し側を解放 → 先押し側を解放 | 後押し側の解放: S2 `1110`、先押し側の解放: 先押し側のパルス | 3.5 |
| test_s2_tie_press_second_side_becomes_next_priority | S2 から btn(BOTH,t) → 先押し側を解放 → tick → 後押し側を解放 | 先押し側のパルス → 後押し側のパルス | 3.5、3.2 |
| test_s2_simultaneous_release_lb_priority_single_pulse | S2 から LB → BOTH → btn(NONE)(同時解放) | P `1010`、t+301: S2 `1110`(WARN なし) | 3.2 同時解放、3.6 |
| test_s2_simultaneous_release_rb_priority_single_pulse | S2 から RB → BOTH → btn(NONE) | P `1100`、t+301: S2 `1110`(WARN なし) | 同上 |
| test_event_at_pulse_end_is_handled_after_pulse | S2 で LB パルス(t+100 開始)→ btn(RB,t+301)(tick なし)→ btn(NONE,t+400) | t+301: S2 `1110`(P 終了後の押下として受け付け)、t+400: P `1100` | 3.3 境界 |

## P 中の入力と WARN(3.3、3.4)

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_carried_priority_released_in_pulse_warns_without_pulse | S2 から LB → BOTH → LB 解放(P 開始)→ P 中に RB 解放 | P `1011`、t+301: S2 `1111`、以後パルスなし | 3.3 の例 |
| test_press_at_pulse_last_ms_is_in_pulse | S2 で LB パルス(t+100 開始)→ btn(RB,t+300) | P `1011`(経過 = 200 の押下はまだ P 中) | 3.3 境界 |
| test_press_in_pulse_warns_and_is_ignored_until_release | S2 で LB パルス中に RB 押下 → P 終了 → RB 解放 → RB 押下 → RB 解放 | 押下: `1011`、P 終了: S2 `1111`、RB 解放: S2 `1111`(パルスも WARN 解除もなし)、再押下: `1110`、解放: P `1100` | 3.3 の 2 項、3.4 |
| test_ignored_button_not_counted_as_simultaneous | 上と同様に RB を無視中にし、LB を押して 2.6 秒 → LB 解放 → RB 解放 | LB 押下: `1110`(WARN 解除)、2.6 秒後も S2(トグルしない)、LB 解放: P `1010`、RB 解放: S2 `1110` | 3.3「同時押しの判定にも数えない」 |
| test_ignored_button_released_in_pulse_does_not_warn | RB を無視中にし、LB 押下 → LB 解放(P)→ P 中に RB 解放 | LB 解放: P `1010`(RB は優先入力にならない)、RB 解放: `1010`(WARN なし)、P 終了: S2 `1110` | 3.3 最終項 |
| test_press_and_release_within_pulse_is_not_ignored | P 中に RB を押して離す → P 終了 → RB 押下 → 解放 | P 終了: `1111`、RB 押下: `1110`、解放: P `1100` | 3.3、3.4 |
| test_release_priority_and_press_other_in_same_event | S2 で btn(LB,t), btn(RB,t+100)(LB 解放と RB 押下が1件) | P `1011`、P 終了後 RB 解放: S2 `1111` | 3.6 |
| test_warn_cleared_by_accepted_press_in_s2 | WARN をラッチした S2 で btn(LB) | `1111` → `1110` | 3.4 |

## 時刻のラップアラウンド

| テスト | 入力 | 期待値 | 仕様 |
|---|---|---|---|
| test_wraparound_toggle_and_manual | t0 = 0xFFFFFFFF − 999 で btn(BOTH), tick t0+1999 / +2000 / +2010 / +2011(0 をまたぐ), btn(NONE,t0+4000) | S0 → SEL_DELAY → SEL_DELAY → LOCK → S2 `1110` | 差分比較 |
| test_wraparound_pulse | t = 0xFFFFFFFF − 49 の S2 で RB パルス(t+10 開始)、tick t+210 / t+211 | P → S2 | 差分比較 |

## 乱択による安全不変条件(test_random_safety_invariants)

固定シード(20260926)の線形合同法で操作を生成する。

- 起動からやり直す区間を 600 回繰り返し、各区間で 500 回操作する(計 30 万回)
- 各区間の始めに、WARN の初期値(1/4 の確率で WARN ラッチ済み)と起動時の押下状態を乱択で決める
  - 起動直後に「S0、押されていたボタン = 無視中、WARN = 初期値」であることを確認する
- 操作: `tick` か、ランダムな押下状態での `btn`
- 時間間隔: 境界付近の値(0, 1, 5, 9〜12, 199〜202, 1999〜2001, 3999〜4001 ms など)か、0〜299 ms
- 時刻の起点: 0xFFFFFFFF − 5000 万。途中で 0 に戻る

毎回の呼び出しの後に、次を検査する。

| 不変条件 | 内容 |
|---|---|
| 押下状態の一致 | ロジックが持つ押下状態 = 入力、無視中のボタン ⊆ 押されているボタン |
| SEL=L ならドライブレンジ | SEL = L の間は D0:D1 = L:L |
| SEL 変化の前後 | SEL が変わった呼び出しでは、直前と直後の出力がともに D0:D1 = L:L で、L:L になってからの経過が T_SEL_DELAY を超えている |
| トグルの条件 | SEL_DELAY に入った呼び出しの直前の入力は両方押しで、両方押しになってから T_TOGGLE 以上経過しており、直前に無視中のボタンがない |
| パルスの開始 | パルスが始まるのは btn 呼び出しの中だけで、その呼び出しでパルスのボタンが押下 → 解放に変わっている |
| パルスの条件 | P の間は SEL = H かつ D0 ≠ D1。P が終わる(または次のパルスに替わる)ときは、開始から T_PULSE を超えている |
| WARN の解除 | WARN が消えるのは btn 呼び出しの中だけで、その呼び出しで新たに押されたボタンがある |
| WARN のラッチ | WARN が点くのは、呼び出しの前か後の状態が P のとき(P 中の変化、P の終了時、P 開始と同時の押下)だけ |
| 網羅の確認 | SEL の変化 100 回以上、パルス 100 回以上、LOCK → S2 10 回以上、WARN のラッチ・解除 各 10 回以上、起動時押下あり 100 区間以上、WARN 付き起動 50 区間以上 |

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
