# 终端与协议说明

*[English](TERMINAL_PROTOCOL_SUPPORT.md) | 中文 | [日本語](TERMINAL_PROTOCOL_SUPPORT_ja.md)*

本页汇总了 PixelTerm-C 当前已有文档记录的终端与图形协议说明。它主要面向实际使用，帮助你理解 `auto` 探测和手动覆盖的行为，不是终端优劣排名。

## 如何阅读本页

- `已文档化` 表示仓库里已经有直接面向用户的说明或示例。
- `部分文档化` 表示项目里已经有协议提示或终端相关说明，但实际表现仍可能取决于你的本地环境。
- `已识别` 表示项目知道这类终端，但本页没有给出更强的协议保证。
- `auto` 模式下，PixelTerm-C 会按 `sixel`、`iterm2`、`kitty` 的顺序探测协议。如果这和你的环境不匹配，可以使用 `--protocol`，或者在 `config.ini` 里加终端专用覆盖。

## 已有协议说明的终端

| 终端 | 协议说明 | 状态 | 备注 |
|------|----------|------|------|
| WezTerm | kitty、sixel、可选的 iTerm2 覆盖 | 已文档化 | `config.example.ini` 里有 `[WezTerm] protocol = iterm2` 示例。 |
| kitty | kitty | 已文档化 | 可以通过 `--protocol kitty` 强制指定。 |
| iTerm2 | iTerm2、sixel | 已文档化 | 可以通过 `--protocol iterm2` 强制指定。 |
| Ghostty | kitty | 部分文档化 | PixelTerm-C 对 Ghostty 环境带有 kitty 协议提示。 |
| Rio | sixel | 部分文档化 | 当前文档把它作为 sixel 探测场景处理。 |
| Warp | kitty | 部分文档化 | `config.example.ini` 里有包含兼容性设置的 `[WarpTerminal]` 示例。 |
| Contour | sixel | 部分文档化 | 当前文档把它作为 sixel 探测场景处理。 |
| Konsole | kitty | 部分文档化 | 代码里还包含了 Konsole 的渲染兼容处理。 |
| EAT | sixel | 部分文档化 | 当前文档把它作为 sixel 探测场景处理。 |
| foot | sixel | 部分文档化 | 当前文档把它作为 sixel 探测场景处理。 |
| mintty | iTerm2、sixel | 部分文档化 | 当前文档主要提供协议提示，没有更广泛的保证。 |
| mlterm | iTerm2、sixel | 部分文档化 | 当前文档主要提供协议提示，没有更广泛的保证。 |
| yaft | sixel | 部分文档化 | 当前文档把它作为 sixel 探测场景处理。 |

## 已识别但暂无更强协议说明的终端

| 终端家族 | 状态 | 备注 |
|----------|------|------|
| Alacritty | 已识别 | 项目能识别终端名，但本页没有给出更强的 kitty、iTerm2 或 Sixel 预期。 |
| Apple Terminal | 已识别 | 本页没有给出更强的协议说明。 |
| ctx | 已识别 | 本页没有给出更强的协议说明。 |
| fbterm | 已识别 | 本页没有给出更强的协议说明。 |
| hurd / linux console / vt220 | 已识别 | 项目能识别这些环境，但本页没有把它们作为支持图形协议的终端来说明。 |
| rxvt / st / xterm | 已识别 | 本页没有给出更强的协议说明。 |
| VTE / Windows console | 已识别 | 本页没有给出更强的协议说明。 |

## 手动覆盖方式

```bash
# 为当前运行显式指定协议
pixelterm --protocol kitty /path/to/image.jpg

# 可用值
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg
```

你也可以在 `config.ini` 里设置 `protocol = auto|text|sixel|kitty|iterm2`。终端专用分组会按照 `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` 的顺序，取第一个匹配值。当前 CLI 和配置语法见 [config.example.ini](../../config.example.ini) 和 [USAGE_zh.md](USAGE_zh.md)。

## 适用范围说明

- 本页只反映项目当前已经写进文档的支持说明，不是完整的认证矩阵。
- 实际渲染效果仍可能受到终端版本、本地设置、远程会话环境以及运行时协议探测结果的影响。
- 如果某个终端在显式指定协议时比 `auto` 更稳定，优先使用本地配置覆盖，并在更新文档时记录实际结果。
