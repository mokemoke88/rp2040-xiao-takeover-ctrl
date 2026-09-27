# CLAUDE.md

README.md は公開向け(単体リポジトリの入口)。制約事項・ビルド方法は README.md と docs/ を参照。仕様は docs/spec.md が正。

## 管理元リポジトリでの位置づけ

このディレクトリは管理元リポジトリ(`claude_general`)の `workspaces/rp2040/xiao-takeover-ctrl` に submodule として置かれている。以下は管理元リポジトリでの情報で、公開向けの README には書かない。

- 参照元アイデア: なし(会話での相談から直接ワークスペース化)
- 開発規約: `rules/develop/c.md` + `rules/develop/c-pico-sdk.md`(後者が CMake + Pico SDK 向けの追加・上書き。Doxygen を .c の static にも付ける方針などはこちら)
- ツールチェーンの入手先とボード固有の注意点: knowledge の `pico-sdk-windows-toolchain-sources`、`xiao-rp2040-pico-sdk-notes`

## 書き分け

- README.md: 公開向けの入口(概要、動作の要点、ピン、ビルド、開発用コマンド、ライセンス)
- docs/spec.md: 仕様(正)
- docs/design-notes.md: 実装の判断、ファイル構成
- docs/verification.md: 実機で確認する項目と結果
- 公開される文書・コメントには、作業ログ調の言い回し(「ユーザー報告」など)や管理元リポジトリへの参照(`ideas/`、`rules/`、knowledge など)を書かない。実機の結果は「確認済み(日付)」と書く
