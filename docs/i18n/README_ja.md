# PixelTerm-C

![Version](https://img.shields.io/badge/Version-v1.7.14-blue)
![License](https://img.shields.io/badge/License-LGPL--3.0-orange)

*[English](../../README.md) | [中文](README_zh.md) | 日本語*

PixelTerm-C は、画像・動画・電子書籍をターミナル内で閲覧するための C 製メディアブラウザです。
単体のファイルを開くことも、ディレクトリ内を移動しながらプレビューすることもでき、作業をターミナル内で完結できます。

## PixelTerm-C を使う理由

- 画像、アニメーション GIF、動画、PDF/EPUB/CBZ を 1 つのワークフローで扱えます。
- 単体表示、グリッド表示、電子書籍ビューア、ファイルマネージャをキーボードでもマウスでも操作できます。
- 描画方式、プリロード、ディザリング、ガンマ補正などを設定でき、端末ごとに調整できます。
- 起動や切り替えが軽く、続けて見比べたい場面でも扱いやすくなっています。

## スクリーンショット

PixelTerm-C の表示例です。

<img src="../../screenshots/2.png" alt="PixelTerm-C Screenshot">

実際の実行画面を使ったスクリーンショットです。

## インストール

### パッケージマネージャ

```bash
# Arch Linux
paru -S pixelterm-c
# または
yay -S pixelterm-c
```

### リリースバイナリ

```bash
# Linux AMD64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-linux
chmod +x pixelterm-amd64-linux && sudo mv pixelterm-amd64-linux /usr/local/bin/pixelterm

# Linux ARM64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-linux
chmod +x pixelterm-arm64-linux && sudo mv pixelterm-arm64-linux /usr/local/bin/pixelterm

# macOS AMD64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-macos
chmod +x pixelterm-amd64-macos && sudo mv pixelterm-amd64-macos /usr/local/bin/pixelterm

# macOS ARM64 (Apple Silicon)
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-macos
chmod +x pixelterm-arm64-macos && sudo mv pixelterm-arm64-macos /usr/local/bin/pixelterm
```

macOS でセキュリティ制限により起動できない場合は、必要に応じて次を実行してください。

```bash
xattr -dr com.apple.quarantine /usr/local/bin/pixelterm
```

## クイックスタート

```bash
# 画像を開く
pixelterm /path/to/image.jpg

# 動画を再生する（音声なし）
pixelterm /path/to/video.mp4

# 電子書籍を読む（PDF/EPUB/CBZ）
pixelterm /path/to/book.pdf

# ディレクトリをブラウズする
pixelterm /path/to/directory

# CLI ヘルプを表示する
pixelterm --help
```

CLI オプションや追加の例は [USAGE_ja.md](../guides/USAGE_ja.md) を参照してください。

## フォーマットと互換性

- 画像: JPG, PNG, GIF, BMP, WebP, TIFF など
- 動画: MP4, MKV, AVI, MOV, WebM, MPEG/MPG, M4V（映像のみ）
- 電子書籍: PDF, EPUB, CBZ（MuPDF 対応でビルドされた場合）
- 出力プロトコルは通常 `auto` で自動判定され、必要に応じて `--protocol` で `text`、`sixel`、`kitty`、`iterm2` を指定できます。

端末ごとの挙動やプロトコルについては [TERMINAL_PROTOCOL_SUPPORT_ja.md](../guides/TERMINAL_PROTOCOL_SUPPORT_ja.md) にまとめています。

## 設定

PixelTerm-C は `$XDG_CONFIG_HOME/pixelterm/config.ini` を読み込み、`XDG_CONFIG_HOME` が未設定または空の場合は `$HOME/.config/pixelterm/config.ini` にフォールバックします。`--config` を使うと別の設定ファイルも指定できます。まずは [`config.example.ini`](../../config.example.ini) をベースに、共通設定を `[default]` に置き、`TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` に対応するセクションで端末ごとの上書きを設定してください。コマンドライン引数は設定ファイルの読み込み後に解釈されるため、明示的に指定した CLI オプションが設定値を上書きします。

```bash
mkdir -p ~/.config/pixelterm
cp config.example.ini ~/.config/pixelterm/config.ini
```

## ドキュメント

- [README.md](../README.md)
- [USAGE_ja.md](../guides/USAGE_ja.md)
- [CONTROLS_ja.md](../guides/CONTROLS_ja.md)
- [TROUBLESHOOTING_ja.md](../guides/TROUBLESHOOTING_ja.md)
- [CHANGELOG.md](../../CHANGELOG.md)
- [TERMINAL_PROTOCOL_SUPPORT_ja.md](../guides/TERMINAL_PROTOCOL_SUPPORT_ja.md)

## ソースからビルド

### 依存関係

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config build-essential
# 電子書籍対応を有効にする場合
sudo apt-get install libmupdf-dev

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 ffmpeg pkgconf base-devel
# 電子書籍対応を有効にする場合
sudo pacman -S mupdf
```

macOS でソースからビルドする場合は、Homebrew で同等の依存関係を用意してください。

```bash
brew install chafa glib gdk-pixbuf ffmpeg pkg-config

# 電子書籍対応を有効にする場合
brew install mupdf
```

### ビルド手順

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make

# 生成物: bin/pixelterm
# システムへインストールする場合: sudo make install

# aarch64 向けクロスコンパイル
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
```

MuPDF が見つからない環境では、電子書籍機能なしでビルドされます。クロスコンパイルは実験的な扱いで、対象アーキテクチャ向けの依存ライブラリが別途必要です。

## ライセンス

LGPL-3.0 以降。詳細は [LICENSE](../../LICENSE) を参照してください。

このプロジェクトは [Chafa](https://github.com/hpjansson/chafa) と同じ LGPLv3+ で配布されています。
