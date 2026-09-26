# xiao-takeover-ctrl

XIAO RP2040 用のファームウェア。2つのタクトスイッチ(LB / RB)の操作で、制御の乗っ取り(SEL)とレンジ指令(D0:D1)を出力する。

## 参照元アイデア

- なし(会話での相談から直接ワークスペース化)

## ドキュメント

- [docs/spec.md](docs/spec.md) — 仕様(ピン割り当て、状態遷移、タイミング、ウォッチドッグ)

## 対象と開発環境

- ボード: Seeed Studio XIAO RP2040
- SDK: Pico SDK 2.3.1(C/C++)
- CMake では `-DPICO_BOARD=seeed_xiao_rp2040` を必ず指定する
- ツールチェーンの入手先とボード固有の注意点は、knowledge の次の記事を参照:
  - `pico-sdk-windows-toolchain-sources`
  - `xiao-rp2040-pico-sdk-notes`

## ビルド・書き込み

未整備(実装着手時に記載する)。
