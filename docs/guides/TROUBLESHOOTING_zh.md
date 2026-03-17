# 常见问题与排障

## 看不到图片或视频输出

- 先尝试显式指定协议：

```bash
pixelterm --protocol kitty /path/to/media
pixelterm --protocol iterm2 /path/to/media
pixelterm --protocol sixel /path/to/media
pixelterm --protocol text /path/to/media
```

- 当前 `auto` 模式的探测顺序是：`sixel` -> `iterm2` -> `kitty`。
- 如果视频已经打开，可以按 `p` 或 `P` 在这些视频输出模式之间循环切换：`text -> sixel -> iterm2 -> kitty -> text`。
- 远程 shell 或 SSH 会话下，协议探测结果可能和本地终端不同。
- 终端相关说明见 [TERMINAL_PROTOCOL_SUPPORT.md](TERMINAL_PROTOCOL_SUPPORT.md)。

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

## 配置文件看起来没有生效

- 默认配置路径：`$XDG_CONFIG_HOME/pixelterm/config.ini`
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
- 如果传入的是目录，程序会加载该目录，并以文件管理器模式启动。
- 如果传入的是普通文件，但它不是受支持的媒体文件，程序会退回到它的父目录，并在该目录打开文件管理器模式。
- 如果直接执行 `pixelterm` 不带路径，程序会在当前目录进入文件管理器模式。
