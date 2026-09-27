# 設計メモ

実装の判断と、仕様([spec.md](spec.md))に明記していない実装上の約束。

## 設計の判断

- `takeover_logic` は時刻(ミリ秒の `uint32_t`)を引数で受け取る純粋な状態機械で、Pico SDK を include しない。経過時間は差分で判定するので、約 49.7 日ごとの時刻の折り返しでも動く
- 「2 秒に達したら」「4 秒に達したら」は、どちらも「経過 ≥ 定数」で判定する
- 乗っ取り側へのトグルで同時押しを途切れずに 4 秒続けると、T(LOCK)のまま解放を待たずに D0:D1 = H:H にする(両解放で S2)。SEL を反転した時刻と同じ時刻には H:H にしない(spec 3.1)
- トグル時の `T_SEL_DELAY` 待ちは非ブロッキング(`SEL_DELAY` 状態として `takeover_tick()` で処理)。待ちの間も入力はロックされる
- チャタリング除去の設計(レジスタの使い方、除去時間の計算、2ピン同時監視の性質)は `src/debounce.pio` の冒頭コメントを参照
- 起動後の PIO の最初の push は必ず起動時の状態になる(確定値 X の初期値を 4 にしている)。ロジックは初期化後の最初の入力を起動時の状態として扱い、押されていたボタンは離されるまで無視する
- `T_PULSE` / `T_SEL_DELAY` は「経過 > 定数」で判定する。メインループ各周の先頭で読んだ時刻(サンプリング時刻)の差が定数を超えることを保証する。ピンが変わるのは各周の出力処理の時点なので、実際の間隔はループ処理時間の差(µs 程度)だけずれうる
- SEL を反転した時刻と同じ時刻には、T(LOCK)中に D0:D1 を H:H にすることも、T を抜けることもしない。SEL の変化と D0:D1 = H:H が1回の出力で同時に出ないようにするため(`after_sel_flip()`)
- panic は `PICO_PANIC_FUNCTION` で `src/panic_hook.c` の `takeover_panic()` に置き換えている。外部出力を安全側に倒し(D0:D1 = L:L → `T_SEL_DELAY` 以上待つ → SEL = L)、緑 LED を消して停止する(仕組みは `include/panic_hook.h`、流れは spec 6.2)
- SEL の反転と D0:D1 の変化は別の出力書き込みになるようにしているが、SEL 反転後に L:L を保つ時間の下限は設けていない(SEL の先は半導体スイッチ。spec 3.1)

## ファイル構成

```
CMakeLists.txt            ファームウェアのビルド定義(PICO_BOARD 既定 seeed_xiao_rp2040、TAKEOVER_ENABLE_WATCHDOG)
pico_sdk_import.cmake     Pico SDK 同梱のものをコピー($PICO_SDK_PATH/external/)
Makefile                  make test / format / lint(ホスト)、make firmware(CMake 呼び出し)
.clangd                   clangd がファームウェアのコンパイル DB(build/)を使う設定
LICENSE                   MIT
include/
  takeover_config.h       ピン番号、タイミング定数、同着時の優先、ウォッチドッグ設定
  takeover_logic.h        状態遷移ロジックの API(SDK 非依存)
  button_input.h          PIO によるチャタリング除去の初期化と読み出し
  outputs.h               SEL / D0 / D1 / LED の出力
  panic_hook.h            panic の置き換え(出力を安全側に倒し、緑 LED を消して停止)
src/
  main.c                  初期化とメインループ(FIFO 読み出し → 時間処理 → 出力 → ウォッチドッグ更新)
  takeover_logic.c        状態遷移ロジック(stdint.h / stdbool.h だけに依存。ホストでテストする)
  button_input.c          PIO0 の SM を1つ設定し、RX FIFO をポーリング
  debounce.pio            安定待ち方式のチャタリング除去(29 命令)
  outputs.c               gpio_put_masked による同時出力、緑 LED、panic 時の安全側出力
  panic_hook.c            takeover_panic()
tests/
  test_takeover_logic.c   Unity による単体テスト
  CASES.md                テストの入力と期待値の対応表
tools/verification/
  verify_debounce_pio.py  PIO プログラムの模擬実行による検算
third_party/unity/        Unity(MIT)
docs/
  spec.md                 仕様
  design-notes.md         この文書
  verification.md         実機での確認項目と結果
```

## エディタ(clangd)

リポジトリ直下の `.clangd` が `build/compile_commands.json`(`make firmware` で生成)を参照する。arm-none-eabi-gcc の標準ヘッダを clangd に見つけさせるには、エディタ側で clangd の起動引数に `--query-driver=<arm-none-eabi-gcc のパス>*` を追加する(例: VSCode の `clangd.arguments`)。`tests/` はコンパイル DB に含まれないため、`.clangd` で Unity のインクルードパスを足している。
