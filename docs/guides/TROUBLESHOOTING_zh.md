# 常见问题与排障

*[English](TROUBLESHOOTING.md) | 中文 | [日本語](TROUBLESHOOTING_ja.md)*

## 看不到图片或视频输出

- 先尝试显式指定协议：

```bash
pixelterm --protocol kitty /path/to/media
pixelterm --protocol iterm2 /path/to/media
pixelterm --protocol sixel /path/to/media
pixelterm --protocol text /path/to/media
```

- `auto` 会先检查匹配到的终端提示，只尝试该提示对应的可能协议，顺序是 `sixel` -> `iterm2` -> `kitty`。
- 在本地会话里，如果提示探测没有确认出协议，`auto` 会再按同样的 `sixel` -> `iterm2` -> `kitty` 顺序做一次通用探测；如果仍然没有确认结果，就会回落到文本输出。
- 在直连 SSH 会话中（设置了 `SSH_CONNECTION`、`SSH_CLIENT` 或 `SSH_TTY`，且没有 `TMUX` 或 `STY`），`auto` 会保持保守：不会继续做通用探测；如果没有针对提示终端家族的肯定信号，就会回落到文本输出。
- 如果你已经知道正确协议，或者远程/透传环境让 `auto` 一直停留在文本输出，就用显式 `--protocol` 值或终端专用 `config.ini` 覆盖。
- 如果视频已经打开，可以按 `p` 或 `P` 切换视频输出模式，顺序如下：`text -> sixel -> iterm2 -> kitty -> text`。
- 终端相关说明见 [TERMINAL_PROTOCOL_SUPPORT_zh.md](TERMINAL_PROTOCOL_SUPPORT_zh.md)。

## Warp 或其他终端里显示异常

- 先尝试 `--alt-screen false`。
- 如果还不够，再尝试 `--clear-workaround`。
- 如果某个设置在你的终端里明显更稳定，建议写进 `config.ini` 的终端专用分组。
- `config.example.ini` 里已经给出了 `WezTerm` 和 `WarpTerminal` 的示例。

## macOS 下载的二进制无法启动

macOS 可能会给下载的二进制附加 quarantine 元数据。移除后再试：

```bash
xattr -dr com.apple.quarantine /usr/local/bin/pixelterm
```

## 电子书打不开

- PDF、EPUB、CBZ 只在包含 MuPDF 支持的构建里可用。
- 如果你是从源码构建，请先安装 MuPDF 再执行 `make`。
- 当前构建如果没有电子书支持，图片和视频功能仍然可以正常使用。
- 目录路径不是有效的电子书文件。目录会以文件管理器模式打开，而把目录当成电子书打开会像缺失或无效的电子书路径一样被拒绝。

## 配置文件看起来没有生效

- 默认配置路径：`$XDG_CONFIG_HOME/pixelterm/config.ini`；如果 `XDG_CONFIG_HOME` 未设置或为空，则回退到 `$HOME/.config/pixelterm/config.ini`。
- 自定义配置路径：`pixelterm --config /path/to/config.ini ...`
- 默认配置文件不存在时会被忽略；但显式传入 `--config` 且文件不存在时，会直接报错。
- 配置加载顺序是：
  - `[default]`
  - 按 `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME`、`TERM` 顺序取第一个匹配分组
- CLI 参数会覆盖配置文件里的值，因为命令行参数是在配置加载之后解析的。
- 对于 `--preload`、`--alt-screen` 这类 CLI 布尔参数，可用值包括 `true/false`、`yes/no`、`on/off`、`1/0`。

## 视频播放没有声音

当前视频播放只支持视频画面，不支持音频输出。

## 路径行为和预期不一致

- 如果路径不存在或无法访问，PixelTerm-C 会直接报错退出。
- 如果路径本身以 `-` 开头，先用 `--` 停止选项解析，例如：`pixelterm -- --config=gallery.txt`
- 如果传入的是目录，程序会加载该目录，并以文件管理器模式启动。
- 如果启动路径既不是目录，也不是有效的电子书文件或有效的媒体文件，程序会退回到该路径的规范化父目录，并在该目录打开文件管理器模式。
- 如果直接执行 `pixelterm` 不带路径，程序会在当前目录进入文件管理器模式。

## 文件管理器或预览网格的选中状态看起来不对

- 如果在切换隐藏文件或调整预览网格缩放后选中了错误的项目，先退出并在同一目录重新打开 PixelTerm-C，重置当前视图。
- 如果重新打开后仍会复现，反馈时请记录目录路径、当时是否显示隐藏文件，以及最后按下的几个按键。
