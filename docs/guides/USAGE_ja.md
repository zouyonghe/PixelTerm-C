# 使い方

*[English](USAGE.md) | [中文](USAGE_zh.md) | 日本語*

```bash
# 単体の画像を開く（直接画像ビューアを開く）
pixelterm /path/to/image.jpg

# 動画を再生する（映像のみ、音声なし）
pixelterm /path/to/video.mp4

# 電子書籍を読む（PDF/EPUB/CBZ）
pixelterm /path/to/book.pdf

# ディレクトリをブラウズする（ファイルマネージャモードで開始）
pixelterm /path/to/directory

# カレントディレクトリで実行する（ファイルマネージャモードで開始）
pixelterm

# バージョンを表示する
pixelterm --version

# ヘルプを表示する
pixelterm --help

# プリロードを切り替える
pixelterm --preload false /path/to/images

# 代替スクリーンバッファを切り替える
pixelterm --alt-screen false /path/to/images
# 注: 主に Warp 向けで、通常は不要です。

# 一部の端末で UI 表示を安定させる（パフォーマンス低下の可能性あり）
pixelterm --clear-workaround /path/to/images
# 注: 主に Warp 向けで、通常は不要です。

# ディザリングを有効にする
pixelterm -D /path/to/image.jpg
# または
pixelterm --dither /path/to/image.jpg

# 描画 work factor を調整する（1-9。高いほど低速だが高品質）
pixelterm --work-factor 7 /path/to/image.jpg

# 出力プロトコルを固定する（auto, text, sixel, kitty, iterm2）
pixelterm --protocol kitty /path/to/image.jpg

# 画像描画のガンマ補正
# 注: デフォルトは 1.0
pixelterm --gamma 0.8 /path/to/image.jpg

# 設定ファイルを読み込む（デフォルト: $XDG_CONFIG_HOME/pixelterm/config.ini）
pixelterm --config ~/.config/pixelterm/config.ini /path/to/image.jpg

# 設定ファイル形式: 共通設定は [default] に置き、TERM_PROGRAM /
# LC_TERMINAL / TERMINAL_NAME / TERM に対応する端末別セクションで上書きします。
# 詳細は config.example.ini を参照してください
```

## 補足

- パスを付けずに `pixelterm` を実行すると、カレントディレクトリを対象にファイルマネージャモードで起動します。
- 設定ファイルは先に読み込まれ、その後に CLI 引数が解釈されるため、明示的な CLI オプションが設定値を上書きします。
- `--preload` と `--alt-screen` では、`true/false`、`yes/no`、`on/off`、`1/0` が使えます。
- デフォルトの設定ファイルが存在しない場合は無視されますが、`--config` で指定したファイルが存在しない場合はエラーになります。
- 設定グループは、`[default]` の後に `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` の順で最初に一致した端末別グループが適用されます。
- 描画結果が崩れる場合は `--protocol` を明示するか、[TROUBLESHOOTING_ja.md](TROUBLESHOOTING_ja.md) を参照してください。
