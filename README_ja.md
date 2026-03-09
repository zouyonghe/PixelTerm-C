# PixelTerm-C

![Version](https://img.shields.io/badge/Version-v1.7.5-blue)
![License](https://img.shields.io/badge/License-LGPL--3.0-orange)

*[English](README.md) | [中文](README_zh.md) | 日本語*

PixelTerm-C は、画像・動画・電子書籍をターミナル内で閲覧するための C 製ブラウザです。
ターミナルを離れずにローカルメディアをすばやく確認したいときのために、軽快な描画と扱いやすい操作系をまとめています。

リリースノートは [CHANGELOG.md](CHANGELOG.md) を参照してください。

## PixelTerm-C を使う理由

- 画像、アニメーション GIF、動画、PDF/EPUB/CBZ を 1 つのワークフローで扱えます。
- 単体表示、グリッド表示、電子書籍の閲覧、ファイルマネージャをキーボードとマウスで切り替えられます。
- 描画方式、プリロード、ディザリング、ガンマ補正などを設定でき、端末ごとに調整できます。
- 起動や切り替えが軽く、連続して見比べたい場面でも扱いやすい構成です。

## スクリーンショット

PixelTerm-C の表示例です。

<img src="screenshots/2.png" alt="PixelTerm-C Screenshot">

実際の PixelTerm-C セッションを使ったスクリーンショットです。

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
```

CLI オプションや追加の例は [USAGE.md](USAGE.md) を参照してください。現時点では詳細ドキュメントは英語です。

## フォーマットと互換性

- 画像: JPG, PNG, GIF, BMP, WebP, TIFF など
- 動画: MP4, MKV, AVI, MOV, WebM, MPEG/MPG, M4V（映像のみ）
- 電子書籍: PDF, EPUB, CBZ（MuPDF 対応でビルドされた場合）
- 出力プロトコルは通常 `auto` で自動判定され、必要に応じて `--protocol` で `text`、`sixel`、`kitty`、`iterm2` を指定できます。

端末ごとの挙動やプロトコルのメモは [docs/TERMINAL_PROTOCOL_SUPPORT.md](docs/TERMINAL_PROTOCOL_SUPPORT.md) にまとめています。

## 設定

PixelTerm-C は `$XDG_CONFIG_HOME/pixelterm/config.ini` が存在すれば自動で読み込みます。`--config` を使うと別の設定ファイルも指定できます。共通設定は `[default]` に書き、`TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` に対応するセクションで端末ごとの上書きができます。

```bash
mkdir -p ~/.config/pixelterm
cp config.example.ini ~/.config/pixelterm/config.ini
```

## ドキュメント

- [USAGE.md](USAGE.md): CLI オプションと実行例
- [CONTROLS.md](CONTROLS.md): キーボードとマウス操作
- [docs/TERMINAL_PROTOCOL_SUPPORT.md](docs/TERMINAL_PROTOCOL_SUPPORT.md): 端末ごとのプロトコルメモ
- [CHANGELOG.md](CHANGELOG.md): リリース履歴

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

LGPL-3.0 以降。詳細は [LICENSE](LICENSE) を参照してください。

このプロジェクトは [Chafa](https://github.com/hpjansson/chafa) と同じ LGPLv3+ で配布されています。
