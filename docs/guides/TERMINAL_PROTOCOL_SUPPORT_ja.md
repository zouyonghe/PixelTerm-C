# 端末とプロトコルのメモ

*[English](TERMINAL_PROTOCOL_SUPPORT.md) | [中文](TERMINAL_PROTOCOL_SUPPORT_zh.md) | 日本語*

このページでは、PixelTerm-C に現在文書化されている端末およびグラフィックスプロトコルの情報を整理しています。`auto` 検出や手動上書きを実際に使うときの参考用であり、端末の優劣を並べるものではありません。

## このページの見方

- `文書化済み` は、ここに明確な出発点があることを意味します。まず表のプロトコルや例から試してください。
- `一部文書化` は、PixelTerm-C に有力なプロトコルヒントはあるものの、`auto` が text に落ちる場合は `--protocol` を自分で試す必要があることを意味します。
- `認識済み` は、PixelTerm-C がその端末系統を識別できても、ローカルで手動上書きを確認していない限りグラフィックス出力を前提にしないほうがよい、という意味です。
- `auto` 以外の `--protocol` 値、または `config.ini` の非 `auto` `protocol = ...` を明示すると、`auto` を通さずそのプロトコルを直接使います。
- `auto` では、まず `TERM`、`TERM_PROGRAM`、端末固有の環境変数から一致した端末ヒントを見つけ、そのヒントで妥当なプロトコルだけを `sixel` -> `iterm2` -> `kitty` の優先順で試します。
- ヒント側のプローブで肯定応答が得られたら、そのプロトコルを使います。確定しなければ、下記の Warp フォールバックを除き、`sixel` -> `iterm2` -> `kitty` 順の汎用プローブへ進みます。直結 SSH で `TERM=xterm-256color` しかない場合も対象です。
- Sixel は能力 `4` を含む完全な主装置属性応答を必要とします。Kitty は画像を表示・保存しないインラインクエリの対応する `OK` 応答を確認し、完全な kitty/Ghostty/libghostty 識別応答も互換性のために利用します。iTerm2 は完全な iTerm2 識別応答を利用します。これは互換性の手掛かりであり、汎用の画像能力クエリではありません。
- 確定しなければ text にフォールバックします。Warp 専用 Kitty フォールバックは例外で、`TERM_PROGRAM=WarpTerminal` のヒント側プローブが不確定な場合、ローカルと直結 SSH の両方で汎用プローブより先に Kitty を選択します。この場合、Sixel と iTerm2 の汎用プローブは行いません。他の環境変数ヒントだけではグラフィックスを有効にしません。
- 正しいプロトコルが分かっている場合や、リモート/パススルー構成で `auto` が text のままになる場合は、`--protocol` または端末別 `config.ini` 上書きを使ってください。

## プロトコルに関するメモがある端末

| 端末 | プロトコルのメモ | 状態 | 補足 |
|------|------------------|------|------|
| WezTerm | kitty、sixel、必要に応じて iTerm2 上書き | 文書化済み | `config.example.ini` に `[WezTerm] protocol = iterm2` の例があります。 |
| kitty | kitty | 文書化済み | `--protocol kitty` で固定できます。 |
| iTerm2 | iTerm2、sixel | 文書化済み | `--protocol iterm2` で固定できます。 |
| Ghostty | kitty | 一部文書化 | `auto` が text のままなら `--protocol kitty` を試してください。 |
| Rio | sixel | 一部文書化 | `auto` が text のままなら `--protocol sixel` を試してください。 |
| Warp | kitty | 一部文書化 | `config.example.ini` に互換性設定付きの `[WarpTerminal]` 例があります。 |
| Contour | sixel | 一部文書化 | `auto` が text のままなら `--protocol sixel` を試してください。 |
| Konsole | kitty | 一部文書化 | `auto` が text のままなら `--protocol kitty` を試してください。 |
| EAT | sixel | 一部文書化 | `auto` が text のままなら `--protocol sixel` を試してください。 |
| foot | sixel | 一部文書化 | `auto` が text のままなら `--protocol sixel` を試してください。 |
| mintty | iTerm2、sixel | 一部文書化 | `auto` が text のままなら `--protocol iterm2` と `--protocol sixel` を試してください。 |
| mlterm | iTerm2、sixel | 一部文書化 | `auto` が text のままなら `--protocol iterm2` と `--protocol sixel` を試してください。 |
| yaft | sixel | 一部文書化 | `auto` が text のままなら `--protocol sixel` を試してください。 |

## グラフィックス出力の推奨がない認識済み端末

| 端末系統 | 状態 | 補足 |
|----------|------|------|
| Alacritty | 認識済み | ここではグラフィックス出力の推奨はありません。ローカルで上書きを確認していない限り、text を想定してください。 |
| Apple Terminal | 認識済み | ここではグラフィックス出力の推奨はありません。ローカルで上書きを確認していない限り、text を想定してください。 |
| ctx | 認識済み | ここではグラフィックス出力の推奨はありません。ローカルで上書きを確認していない限り、text を想定してください。 |
| fbterm | 認識済み | ここではグラフィックス出力の推奨はありません。ローカルで上書きを確認していない限り、text を想定してください。 |
| hurd / linux console / vt220 | 認識済み | 通常は text 専用として扱ってください。自分の環境で別の結果を確認できた場合だけ例外です。 |
| rxvt / st / xterm | 認識済み | ここではグラフィックス出力の推奨はありません。ローカルで上書きを確認していない限り、text を想定してください。 |
| VTE / Windows console | 認識済み | ここではグラフィックス出力の推奨はありません。ローカルで上書きを確認していない限り、text を想定してください。 |

## 手動で上書きする方法

```bash
# 現在の実行だけプロトコルを固定する
pixelterm --protocol kitty /path/to/image.jpg

# text プロトコルのまま quarter-block の細部を優先する
pixelterm --protocol text --text-symbols quarter /path/to/image.jpg

# 利用可能な値
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg
```

`config.ini` に `protocol = auto|text|sixel|kitty|iterm2` を書くこともできます。テキスト描画では、`text_symbols = auto|half|quarter` によって、端末安全な既定の記号集合を維持するか、より強い half-block / quarter-block の手動記号集合へ切り替えられます。端末別セクションは `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` の順で最初に一致したものが使われます。CLI と設定ファイルの現在の書式は [config.example.ini](../../config.example.ini) と [USAGE_ja.md](USAGE_ja.md) を参照してください。

## 範囲に関するメモ

- このページは実用ガイドであり、完全な認証マトリクスではありません。
- 実際の描画結果は、端末のバージョン、ローカル設定、リモートセッション構成、実行時のプロトコル検出結果によって変わる場合があります。
- プローブにはタイムアウトがあります。SSH の遅延や応答の遮断で text に戻る場合は、既知のプロトコルを明示してください。tmux/screen のパススルーラッピングには対応していません。
- `auto` より明示的なプロトコル指定のほうが安定する端末では、その端末向けにローカル設定で上書きしてください。
