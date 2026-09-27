# xiao-takeover-ctrl

Seeed Studio XIAO RP2040 用のファームウェア。2 つのタクトスイッチ(LB / RB)の操作で、制御の乗っ取り(SEL)とレンジ指令(D0:D1)を出力する。

## 動作の要点

詳細(状態遷移図、タイミング、WARN、ウォッチドッグ)は [docs/spec.md](docs/spec.md) が正。

- **両押しを 2 秒**続けると、D0:D1 を L:L(ドライブレンジ)にしてから SEL を反転する(乗っ取り ⇔ 解除)。乗っ取り側へのトグルで両押しを **4 秒**まで続けると、D0:D1 = H:H(マニュアル待機)になる
- マニュアル待機中は、**先に押したボタンを離した時点**で 200 ms のパルスを出す(LB: D0:D1 = L:H、RB: H:L)
- パルス出力中の操作など想定外の入力は、オンボード LED の赤(WARN)をラッチして知らせる
- 入力のチャタリングは PIO で除去する(約 21〜22 ms)。ウォッチドッグ(100 ms)と、panic 時に出力を安全側に倒す処理を持つ

| D0:D1 | 意味 |
|---|---|
| L:L | マニュアル以外 = ドライブレンジ |
| H:H | マニュアルモード待機 |
| L:H | LB パルス(200 ms) |
| H:L | RB パルス(200 ms) |

## ハードウェアとピン

| 信号 | 向き | XIAO ピン | GPIO |
|---|---|---|---|
| D0 | 出力 | D0 | 26 |
| D1 | 出力 | D1 | 27 |
| SEL | 出力(H = 乗っ取り中) | D2 | 28 |
| WARN | オンボード RGB LED の赤 | — | 17 |
| LB | 入力(外部プルアップ 10 kΩ、押下で L) | D9 | 4 |
| RB | 入力(同上) | D10 | 3 |

プルアップは 3V3 に接続する(RP2040 の I/O は 5V 耐性なし)。出力は 3.3V ロジックで、相手側と GND を共通にする。詳細は [docs/spec.md](docs/spec.md) の「1. 信号とピン割り当て」。

## 必要なもの

- ファームウェア: Pico SDK 2.3.1(`PICO_SDK_PATH`)、Arm GNU Toolchain(`arm-none-eabi-gcc`)、CMake、Ninja。picotool / pioasm はビルド済みのものを `picotool_DIR` / `pioasm_DIR` で指定しておく
- ホスト側のテスト・整形: `gcc`、`make`(Windows では w64devkit で確認)、`clang-format`
- PIO の検算スクリプト: Python 3(標準ライブラリのみ)

## ビルドと書き込み

コマンドはすべてリポジトリ直下で実行する。

```
make firmware
```

`cmake -S . -B build -G Ninja -DPICO_BOARD=seeed_xiao_rp2040` と `cmake --build build` を実行し、`build/xiao_takeover_ctrl.uf2` ができる(`CMakeLists.txt` は `PICO_BOARD` 未指定時に `seeed_xiao_rp2040` を既定にしている)。

1. XIAO の B(BOOT)ボタンを押しながら USB で PC に接続する(または接続中に B を押したまま R を押して離す)
2. `RPI-RP2` ドライブが現れるので、`build/xiao_takeover_ctrl.uf2` をコピーする
3. コピーが終わると自動で再起動し、ファームウェアが動き始める

picotool を使う場合は、BOOTSEL 状態で `picotool load -x build/xiao_takeover_ctrl.uf2` でもよい。

### ビルドオプション

| CMake オプション | 既定 | 内容 |
|---|---|---|
| `TAKEOVER_ENABLE_WATCHDOG` | ON | ウォッチドッグ(タイムアウト 100 ms、デバッガ停止中は一時停止)を有効にする。無効にすると panic 後は停止したままになる |

ウォッチドッグ無効版を別ディレクトリにビルドする例: `make firmware BUILD_DIR=build-nowdt CMAKE_ARGS=-DTAKEOVER_ENABLE_WATCHDOG=OFF`

ピン番号・タイミング定数・同着時の優先は `include/takeover_config.h`、チャタリング除去の時間は `src/debounce.pio` の冒頭コメントと定数で決まる。タイミング定数の間の前提(例: `T_MANUAL > T_TOGGLE + T_SEL_DELAY`)は `takeover_config.h` でコンパイル時に検査しているので、前提に反する値にするとビルドが失敗する。

## 開発

| コマンド | 内容 |
|---|---|
| `make format` | clang-format で整形 |
| `make lint` | 整形済みか検査(差分があれば失敗) |
| `make test` | 状態遷移ロジック(`src/takeover_logic.c`)をホストの gcc でビルドし、Unity のテストを実行。次の 3 構成でビルドしてすべて実行する: 仕様どおり(同着時 LB 優先)、RB 優先(`-DTAKEOVER_TIE_PREFER_LB=0`)、タイミング定数を仕様値と違う値にした構成(`ALT_TIMING`)。テストの時刻は `takeover_config.h` の定数から計算しているので、定数を調整してもテストを書き直す必要はない。成果物は `build-host/` |
| `make firmware` | ファームウェアのビルド(上記) |
| `python tools/verification/verify_debounce_pio.py build/debounce.pio.h` | `make firmware` で生成された PIO の命令語を模擬実行し、チャタリング除去の時間、ノイズ除去、起動時の最初の push を検算。期待値は生成ヘッダの `debounce_CLOCK_HZ` / `debounce_REREAD_DELAY` から計算する |

コードを変更したら `make format` → `make lint` → `make test` を 1 つずつ実行し、警告なし・全 PASS を確認する。ファームウェア側を変えた場合は `make firmware` も警告なしで通ること。

## ドキュメント

- [docs/spec.md](docs/spec.md) — 仕様(ピン割り当て、状態遷移、タイミング、ウォッチドッグ)
- [docs/design-notes.md](docs/design-notes.md) — 設計メモ(実装の判断、ファイル構成、エディタの設定)
- [docs/verification.md](docs/verification.md) — 実機での確認項目と結果
- [tests/CASES.md](tests/CASES.md) — 状態遷移ロジックの単体テストの入力と期待値、仕様に明記がなく実装で決めた解釈

## ライセンス

[MIT](LICENSE)。`third_party/unity/`(Unity)は MIT([LICENSE.txt](third_party/unity/LICENSE.txt))。
