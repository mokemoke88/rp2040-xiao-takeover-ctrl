# xiao-takeover-ctrl

XIAO RP2040 用のファームウェア。2つのタクトスイッチ(LB / RB)の操作で、制御の乗っ取り(SEL)とレンジ指令(D0:D1)を出力する。

## 参照元アイデア

- なし(会話での相談から直接ワークスペース化)

## ドキュメント

- [docs/spec.md](docs/spec.md) — 仕様(ピン割り当て、状態遷移、タイミング、ウォッチドッグ)
- [tests/CASES.md](tests/CASES.md) — 状態遷移ロジックの単体テストの入力と期待値、仕様に明記がなく実装で決めた解釈

## 対象と開発環境

- ボード: Seeed Studio XIAO RP2040
- SDK: Pico SDK 2.3.1(C/C++)
- CMake では `-DPICO_BOARD=seeed_xiao_rp2040` を必ず指定する(`CMakeLists.txt` は未指定時にこれを既定にしている)
- ツールチェーンの入手先とボード固有の注意点は、knowledge の次の記事を参照:
  - `pico-sdk-windows-toolchain-sources`
  - `xiao-rp2040-pico-sdk-notes`
- 開発規約: `rules/develop/c.md` + `rules/develop/c-pico-sdk.md`(後者が CMake + Pico SDK 向けの追加・上書き。Doxygen を .c の static にも付ける方針などはこちら)

## ビルド・書き込み

### 必要なもの

- ファームウェア: `PICO_SDK_PATH`(Pico SDK 2.3.1)、Arm GNU Toolchain(`arm-none-eabi-gcc`)、CMake、Ninja。picotool / pioasm はビルド済みのものを `picotool_DIR` / `pioasm_DIR` で指定しておく
- ホスト側のテスト・整形: `gcc`、`make`(Windows では w64devkit で確認)、`clang-format`
- PIO の検算スクリプト: Python 3

### コマンド

すべてワークスペース直下で実行する。

| コマンド | 内容 |
|---|---|
| `make format` | clang-format で整形 |
| `make lint` | 整形済みか検査(差分があれば失敗) |
| `make test` | 状態遷移ロジック(`src/takeover_logic.c`)をホストの gcc でビルドし、Unity のテストを実行。同着時 LB 優先(既定)と RB 優先(`-DTAKEOVER_TIE_PREFER_LB=0`)の2通りでビルドして両方実行する。成果物は `build-host/` |
| `make firmware` | `cmake -S . -B build -G Ninja -DPICO_BOARD=seeed_xiao_rp2040` + `cmake --build build`。`build/xiao_takeover_ctrl.uf2` ができる |
| `make firmware BUILD_DIR=build-nowdt CMAKE_ARGS=-DTAKEOVER_ENABLE_WATCHDOG=OFF` | ウォッチドッグ無効版を別ディレクトリにビルド |
| `python tools/verification/verify_debounce_pio.py build/debounce.pio.h` | `make firmware` で生成された PIO の命令語を模擬実行し、チャタリング除去の時間、ノイズ除去、起動時の最初の push を検算。期待値は生成ヘッダの `debounce_CLOCK_HZ` / `debounce_REREAD_DELAY` から計算する |

コードを変更したら `make format` → `make lint` → `make test` を1つずつ実行し、警告なし・全 PASS を確認する。ファームウェア側を変えた場合は `make firmware` も警告なしで通ること。

### ビルドオプション

| CMake オプション | 既定 | 内容 |
|---|---|---|
| `TAKEOVER_ENABLE_WATCHDOG` | ON | ウォッチドッグ(タイムアウト 100 ms、デバッガ停止中は一時停止)を有効にする。無効にすると panic 後は停止したままになる |

ピン番号・タイミング定数・同着時の優先は `include/takeover_config.h`、チャタリング除去の時間は `src/debounce.pio` の冒頭コメントと定数で決まる。

### 書き込み

1. XIAO の B(BOOT)ボタンを押しながら USB で PC に接続する(または接続中に B を押したまま R を押して離す)
2. `RPI-RP2` ドライブが現れるので、`build/xiao_takeover_ctrl.uf2` をコピーする
3. コピーが終わると自動で再起動し、ファームウェアが動き始める

picotool を使う場合は、BOOTSEL 状態で `picotool load -x build/xiao_takeover_ctrl.uf2` でもよい。

### 実機で確認すること(未実施)

- 仕様の各操作で SEL / D0 / D1 が期待どおりか(テスター / ロジックアナライザ)
- パルス幅が 200 ms を超えること(〜約 201 ms)、D0:D1 → SEL の遅延が 10 ms を超えること(`T_SEL_DELAY` は暫定値)
- チャタリング除去の効き(除去時間は計算上 約 21〜22 ms)
- 起動時に押していたボタンが、離すまで無視されること
- WARN(赤 LED)の点灯と解除、青 LED が消灯していること
- 緑 LED: 起動後に点灯すること。ウォッチドッグによる再起動後は赤が点いた状態で始まり、緑も点くこと(再起動の起こし方は要検討。例: デバッガでコアを止める場合は `pause_on_debug` のためウォッチドッグも止まる)

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
```

### エディタ(clangd)

ワークスペース直下の `.clangd` が `build/compile_commands.json`(`make firmware` で生成)を参照する。arm-none-eabi-gcc の標準ヘッダを clangd に見つけさせるには、エディタ側で clangd の起動引数に `--query-driver=<arm-none-eabi-gcc のパス>*` を追加する(例: VSCode の `clangd.arguments`)。`tests/` はコンパイル DB に含まれないため、`.clangd` で Unity のインクルードパスを足している。

## 設計メモ

- `takeover_logic` は時刻(ミリ秒の `uint32_t`)を引数で受け取る純粋な状態機械で、Pico SDK を include しない。経過時間は差分で判定するので、約 49.7 日ごとの時刻の折り返しでも動く
- 「2 秒に達したら」「4 秒以上」は、どちらも「経過 ≥ 定数」で判定する
- トグル時の `T_SEL_DELAY` 待ちは非ブロッキング(`SEL_DELAY` 状態として `takeover_tick()` で処理)。待ちの間も入力はロックされる
- チャタリング除去の設計(レジスタの使い方、除去時間の計算、2ピン同時監視の性質)は `src/debounce.pio` の冒頭コメントを参照
- 起動後の PIO の最初の push は必ず起動時の状態になる(確定値 X の初期値を 4 にしている)。ロジックは初期化後の最初の入力を起動時の状態として扱い、押されていたボタンは離されるまで無視する
- `T_PULSE` / `T_SEL_DELAY` は「経過 > 定数」で判定する。メインループ各周の先頭で読んだ時刻(サンプリング時刻)の差が定数を超えることを保証する。ピンが変わるのは各周の出力処理の時点なので、実際の間隔はループ処理時間の差(µs 程度)だけずれうる
- SEL を反転した時刻と同じ時刻には T(LOCK)を抜けない。SEL の変化と D0:D1 = H:H が1回の出力で同時に出ないようにするため
- panic は `PICO_PANIC_FUNCTION` で `src/panic_hook.c` の `takeover_panic()` に置き換えている。外部出力を安全側に倒し(D0:D1 = L:L → `T_SEL_DELAY` 以上待つ → SEL = L)、緑 LED を消して停止する(仕組みは `include/panic_hook.h`、流れは spec 6.2)
- SEL の反転と D0:D1 の変化は別の出力書き込みになるようにしているが、SEL 反転後に L:L を保つ時間の下限は設けていない(SEL の先は半導体スイッチ。spec 3.1)
