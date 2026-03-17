# 端末とプロトコルのメモ

*[English](TERMINAL_PROTOCOL_SUPPORT.md) | [中文](TERMINAL_PROTOCOL_SUPPORT_zh.md) | 日本語*

このページでは、PixelTerm-C に現在文書化されている端末およびグラフィックスプロトコルの情報を整理しています。`auto` 検出や手動上書きを実際に使うときの参考用であり、端末の優劣を並べるものではありません。

## このページの見方

- `文書化済み` は、ユーザー向けの説明や例がリポジトリ内に用意されていることを意味します。
- `一部文書化` は、プロトコルヒントや端末固有のメモはあるものの、実際の挙動がローカル環境に左右される可能性があることを意味します。
- `認識済み` は、プロジェクトがその端末系統を把握しているものの、このページでより強いプロトコル保証を示していないことを意味します。
- `auto` モードでは、PixelTerm-C は `sixel`、`iterm2`、`kitty` の順にプロトコルを試します。環境に合わない場合は、`--protocol` や端末別の `config.ini` 上書きを使ってください。

## プロトコルに関するメモがある端末

| 端末 | プロトコルのメモ | 状態 | 補足 |
|------|------------------|------|------|
| WezTerm | kitty、sixel、必要に応じて iTerm2 上書き | 文書化済み | `config.example.ini` に `[WezTerm] protocol = iterm2` の例があります。 |
| kitty | kitty | 文書化済み | `--protocol kitty` で固定できます。 |
| iTerm2 | iTerm2、sixel | 文書化済み | `--protocol iterm2` で固定できます。 |
| Ghostty | kitty | 一部文書化 | Ghostty 環境向けの kitty プロトコルヒントがあります。 |
| Rio | sixel | 一部文書化 | 現在の文書では sixel 検出を前提に扱っています。 |
| Warp | kitty | 一部文書化 | `config.example.ini` に互換性設定付きの `[WarpTerminal]` 例があります。 |
| Contour | sixel | 一部文書化 | 現在の文書では sixel 検出を前提に扱っています。 |
| Konsole | kitty | 一部文書化 | コードベースには Konsole 向けの描画調整も含まれています。 |
| EAT | sixel | 一部文書化 | 現在の文書では sixel 検出を前提に扱っています。 |
| foot | sixel | 一部文書化 | 現在の文書では sixel 検出を前提に扱っています。 |
| mintty | iTerm2、sixel | 一部文書化 | 広い保証というより、現状はプロトコルヒント中心です。 |
| mlterm | iTerm2、sixel | 一部文書化 | 広い保証というより、現状はプロトコルヒント中心です。 |
| yaft | sixel | 一部文書化 | 現在の文書では sixel 検出を前提に扱っています。 |

## より強いプロトコル前提が書かれていない認識済み端末

| 端末系統 | 状態 | 補足 |
|----------|------|------|
| Alacritty | 認識済み | 端末名は認識されますが、このページでは kitty、iTerm2、Sixel いずれの強い前提も示していません。 |
| Apple Terminal | 認識済み | このページではより強いプロトコルメモはありません。 |
| ctx | 認識済み | このページではより強いプロトコルメモはありません。 |
| fbterm | 認識済み | このページではより強いプロトコルメモはありません。 |
| hurd / linux console / vt220 | 認識済み | プロジェクトは把握していますが、グラフィックス対応端末としては記述していません。 |
| rxvt / st / xterm | 認識済み | このページではより強いプロトコルメモはありません。 |
| VTE / Windows console | 認識済み | このページではより強いプロトコルメモはありません。 |

## 手動で上書きする方法

```bash
# 現在の実行だけプロトコルを固定する
pixelterm --protocol kitty /path/to/image.jpg

# 利用可能な値
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg
```

`config.ini` に `protocol = auto|text|sixel|kitty|iterm2` を書くこともできます。端末別セクションは `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` の順で最初に一致したものが使われます。CLI と設定ファイルの現在の書式は [config.example.ini](../../config.example.ini) と [USAGE_ja.md](USAGE_ja.md) を参照してください。

## 範囲に関するメモ

- このページは、現時点でプロジェクトが文書化しているサポート情報だけをまとめたもので、完全な認証マトリクスではありません。
- 実際の描画結果は、端末のバージョン、ローカル設定、リモートセッション構成、実行時のプロトコル検出結果によって変わる場合があります。
- `auto` より明示的なプロトコル指定のほうが安定する端末では、ローカル設定で上書きし、その結果を文書更新時に記録してください。
