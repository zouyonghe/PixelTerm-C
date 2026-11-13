# 终端与协议说明

*[English](TERMINAL_PROTOCOL_SUPPORT.md) | 中文 | [日本語](TERMINAL_PROTOCOL_SUPPORT_ja.md)*

本页汇总了 PixelTerm-C 当前已有文档记录的终端与图形协议说明。它主要面向实际使用，帮助你理解 `auto` 探测和手动覆盖的行为，不是终端优劣排名。

## 如何阅读本页

- `已文档化` 表示这里已经给出了明确的起点；优先先试表里列出的协议或示例。
- `部分文档化` 表示 PixelTerm-C 有较可能的协议提示，但如果 `auto` 回落到文本输出，你可能仍需自己测试 `--protocol`。
- `已识别` 表示 PixelTerm-C 能识别这类终端，但除非你已经在本地验证过手动覆盖，否则不要默认它能输出图形。
- 任何显式的非 `auto` `--protocol` 值，或 `config.ini` 里的非 `auto` `protocol = ...`，都会跳过 `auto`，直接使用指定协议。
- 在 `auto` 模式下，PixelTerm-C 会先根据 `TERM`、`TERM_PROGRAM` 或终端专用环境变量匹配终端提示，只尝试该提示对应的可能协议，优先顺序是 `sixel` -> `iterm2` -> `kitty`。
- 如果提示内的协议探测到了肯定信号，`auto` 就会选用它；否则，除下述 Warp 回退外，按 `sixel` -> `iterm2` -> `kitty` 顺序做通用探测。直连 SSH 也会探测，即使远端只有 `TERM=xterm-256color`。
- Sixel 要求完整的主设备属性响应包含能力 `4`。Kitty 发送不显示、不存储图片的内联图形查询，并接受匹配的 `OK` 响应；同时保留完整的 kitty/Ghostty/libghostty 身份响应作为兼容依据。iTerm2 使用完整的 iTerm2 身份响应，而非普通状态响应；它是兼容信号，不是通用图片能力查询。
- 探测无结论时回落到文本。Warp 专用 Kitty 回退是例外：当 `TERM_PROGRAM=WarpTerminal` 且提示内探测无结论时，本地和直连 SSH 都会在通用探测前选择 Kitty，此时不再做 Sixel 和 iTerm2 通用探测。其他终端提示本身不会启用图形。
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

# 保持 text 协议，但优先保留 quarter-block 细节
pixelterm --protocol text --text-symbols quarter /path/to/image.jpg

# 可用值
pixelterm --protocol auto|text|sixel|kitty|iterm2 /path/to/image.jpg
```

你也可以在 `config.ini` 里设置 `protocol = auto|text|sixel|kitty|iterm2`。对于文本渲染，`text_symbols = auto|half|quarter` 可控制 PixelTerm-C 保持终端安全默认符号集，或切换到更激进的 half-block / quarter-block 手动符号集。终端专用分组会按照 `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` 的顺序，取第一个匹配值。当前 CLI 和配置语法见 [config.example.ini](../../config.example.ini) 和 [USAGE_zh.md](USAGE_zh.md)。

## 适用范围说明

- 把本页当作实际使用指南，而不是完整的认证矩阵。
- 实际渲染效果仍可能受到终端版本、本地设置、远程会话环境以及运行时协议探测结果的影响。
- 在直连 Kitty 中，符合限制的 GIF 和动画 WebP 会使用 Kitty 原生动画帧；其他情况继续使用客户端逐帧回退路径。
- 探测有超时上限，SSH 延迟较高或响应被屏蔽时仍可能回落到文本；对已知可用的协议使用显式覆盖。不包含 tmux/screen 透传封装。
- 回复与按键的区分也有时间边界：Escape 最多等待 250 ms 来识别后续引导字节；半截回复状态在空闲 250 ms 或累计 2 秒后到期。终端回复和 Alt 按键共用字节编码，无法完美区分任意长延迟；到期后优先恢复键盘输入。较慢链路可显式指定协议以跳过探测。
- 每次查询前都会建立有界且不清空输入的边界：已排队的回复，以及查询前已开始的半截回复，不用于确认当前查询；普通按键保留。DA/XTVERSION 没有请求 ID，因此无法完美区分全部字节都在新查询之后才到达的旧回复。
- 如果某个终端在显式指定协议时比 `auto` 更稳定，优先为该终端使用本地配置覆盖。
