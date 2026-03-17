# よくある問題とトラブルシューティング

*[English](TROUBLESHOOTING.md) | [中文](TROUBLESHOOTING_zh.md) | 日本語*

## 画像や動画が表示されない

- まずは `--protocol` を明示して試してください。

```bash
pixelterm --protocol kitty /path/to/media
pixelterm --protocol iterm2 /path/to/media
pixelterm --protocol sixel /path/to/media
pixelterm --protocol text /path/to/media
```

- 現在の `auto` モードは `sixel` -> `iterm2` -> `kitty` の順でプロトコルを試します。
- 動画を開いている場合は、`p` または `P` で動画出力モードを `text -> sixel -> iterm2 -> kitty -> text` の順に切り替えられます。
- リモートシェルや SSH セッションでは、ローカル端末と挙動が変わることがあります。
- 端末ごとの補足は [TERMINAL_PROTOCOL_SUPPORT_ja.md](TERMINAL_PROTOCOL_SUPPORT_ja.md) を参照してください。

## Warp などの端末で表示が崩れる

- まず `--alt-screen false` を試してください。
- それでも不十分なら `--clear-workaround` を試してください。
- 特定の設定が安定する場合は、その値を `config.ini` の端末別セクションへ移すと扱いやすくなります。
- `config.example.ini` には `WezTerm` と `WarpTerminal` の設定例があります。

## ダウンロードした macOS バイナリが起動しない

macOS がダウンロード済みバイナリに quarantine 属性を付けることがあります。削除してから再度実行してください。

```bash
xattr -dr com.apple.quarantine /usr/local/bin/pixelterm
```

## 電子書籍が開けない

- PDF、EPUB、CBZ は MuPDF 対応でビルドされた場合にのみ利用できます。
- ソースからビルドする場合は、`make` を実行する前に MuPDF をインストールしてください。
- 現在のビルドに電子書籍対応がなくても、画像と動画は通常どおり使えます。

## 設定ファイルが無視されているように見える

- デフォルトの設定パス: `$XDG_CONFIG_HOME/pixelterm/config.ini`
- カスタム設定パス: `pixelterm --config /path/to/config.ini ...`
- デフォルトの設定ファイルが存在しない場合は無視されますが、`--config` で指定したファイルが存在しない場合はエラーになります。
- 設定の読み込み順は次のとおりです。
  - `[default]`
  - `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` の順で最初に一致した端末別グループ
- CLI 引数は設定ファイルの読み込み後に解釈されるため、明示した CLI オプションが設定値を上書きします。
- `--preload` や `--alt-screen` などの CLI ブール値には `true/false`、`yes/no`、`on/off`、`1/0` が使えます。

## 動画再生で音が出ない

現在の動画再生は映像のみで、音声出力はサポートしていません。

## パスの扱いが想定と違う

- パスが存在しない、またはアクセスできない場合、PixelTerm-C はエラーで終了します。
- ディレクトリを渡すと、そのディレクトリを読み込み、ファイルマネージャモードで起動します。
- 通常ファイルを渡しても、それが対応メディアでない場合は親ディレクトリへ戻り、その場所でファイルマネージャモードを開きます。
- `pixelterm` をパスなしで実行すると、カレントディレクトリを対象にファイルマネージャモードで起動します。
