# 终端与协议说明

*[English](TERMINAL_PROTOCOL_SUPPORT.md) | 中文 | [日本語](TERMINAL_PROTOCOL_SUPPORT_ja.md)*

本页汇总了 PixelTerm-C 当前已有文档记录的终端与图形协议说明。它主要面向实际使用，帮助你理解 `auto` 探测和手动覆盖的行为，不是终端优劣排名。

## 如何阅读本页

- `已文档化` 表示这里已经给出了明确的起点；优先先试表里列出的协议或示例。
- `部分文档化` 表示 PixelTerm-C 有较可能的协议提示，但如果 `auto` 回落到文本输出，你可能仍需自己测试 `--protocol`。
- `已识别` 表示 PixelTerm-C 能识别这类终端，但除非你已经在本地验证过手动覆盖，否则不要默认它能输出图形。
- 任何显式的非 `auto` `--protocol` 值，或 `config.ini` 里的非 `auto` `protocol = ...`，都会跳过 `auto`，直接使用指定协议。
- 在 `auto` 模式下，PixelTerm-C 会先根据 `TERM`、`TERM_PROGRAM` 或终端专用环境变量匹配终端提示，只尝试该提示对应的可能协议，优先顺序是 `sixel` -> `iterm2` -> `kitty`。
- 如果提示内的某个协议探测到了肯定信号，`auto` 就会选用它。若不是直连 SSH，程序才会继续按相同的 `sixel` -> `iterm2` -> `kitty` 顺序做一次通用探测。
- 如果本地探测仍然没有确认出图形协议，`auto` 会回落到文本输出。
- 在直连 SSH 会话中（设置了 `SSH_CONNECTION`、`SSH_CLIENT` 或 `SSH_TTY`，且没有 `TMUX` 或 `STY`），`auto` 不会继续做通用探测；如果没有针对提示终端家族的肯定信号，就会回落到文本输出。
- 如果你已经知道正确协议，或者远程/透传环境让 `auto` 一直停留在文本输出，就用 `--protocol` 或终端专用的 `config.ini` 覆盖。

## 已有协议说明的终端

| 终端 | 协议说明 | 状态 | 备注 |
|------|----------|------|------|
| WezTerm | kitty、sixel、可选的 iTerm2 覆盖 | 已文档化 | `config.example.ini` 里有 `[WezTerm] protocol = iterm2` 示例。 |
| kitty | kitty | 已文档化 | 可以通过 `--protocol kitty` 强制指定。 |
| iTerm2 | iTerm2、sixel | 已文档化 | 可以通过 `--protocol iterm2` 强制指定。 |
| Ghostty | kitty | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol kitty`。 |
| Rio | sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol sixel`。 |
| Warp | kitty | 部分文档化 | `config.example.ini` 里有包含兼容性设置的 `[WarpTerminal]` 示例。 |
| Contour | sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol sixel`。 |
| Konsole | kitty | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol kitty`。 |
| EAT | sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol sixel`。 |
| foot | sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol sixel`。 |
| mintty | iTerm2、sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，可以测试 `--protocol iterm2` 和 `--protocol sixel`。 |
| mlterm | iTerm2、sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，可以测试 `--protocol iterm2` 和 `--protocol sixel`。 |
| yaft | sixel | 部分文档化 | 如果 `auto` 一直停留在文本输出，试试 `--protocol sixel`。 |

## 已识别但没有图形输出建议的终端

| 终端家族 | 状态 | 备注 |
|----------|------|------|
| Alacritty | 已识别 | 这里没有给出图形输出建议；除非你已经在本地验证过覆盖，否则按文本输出预期处理。 |
| Apple Terminal | 已识别 | 这里没有给出图形输出建议；除非你已经在本地验证过覆盖，否则按文本输出预期处理。 |
| ctx | 已识别 | 这里没有给出图形输出建议；除非你已经在本地验证过覆盖，否则按文本输出预期处理。 |
| fbterm | 已识别 | 这里没有给出图形输出建议；除非你已经在本地验证过覆盖，否则按文本输出预期处理。 |
| hurd / linux console / vt220 | 已识别 | 一般按纯文本终端处理，除非你已经在自己的环境里确认过不同结果。 |
| rxvt / st / xterm | 已识别 | 这里没有给出图形输出建议；除非你已经在本地验证过覆盖，否则按文本输出预期处理。 |
| VTE / Windows console | 已识别 | 这里没有给出图形输出建议；除非你已经在本地验证过覆盖，否则按文本输出预期处理。 |

## 手动覆盖方式

```bash
# 为当前运行显式指定协议
pixelterm --protocol kitty /path/to/image.jpg

# 可用值
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg
```

你也可以在 `config.ini` 里设置 `protocol = auto|text|sixel|kitty|iterm2`。其中任何非 `auto` 的值都会像 `--protocol` 一样跳过自动解析。终端专用分组会按照 `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` 的顺序，取第一个匹配值。当前 CLI 和配置语法见 [config.example.ini](../../config.example.ini) 和 [USAGE_zh.md](USAGE_zh.md)。

## 适用范围说明

- 把本页当作实际使用指南，而不是完整的认证矩阵。
- 实际渲染效果仍可能受到终端版本、本地设置、远程会话环境以及运行时协议探测结果的影响。
- 直连 SSH 的回落策略是刻意保守的。如果远程、tmux 或 screen 透传环境隐藏了你预期的肯定信号，优先使用显式覆盖。
- 如果某个终端在显式指定协议时比 `auto` 更稳定，优先为该终端使用本地配置覆盖。
