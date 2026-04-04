# PixelTerm-C

![Version](https://img.shields.io/badge/Version-v1.7.20-blue)
![License](https://img.shields.io/badge/License-LGPL--3.0-orange)

*[English](../../README.md) | 中文 | [日本語](README_ja.md)*

PixelTerm-C 是一个在终端里浏览图片、视频和电子书的本地媒体工具。你既可以直接打开单个文件，也可以在目录里快速切换和预览内容，全程不用离开终端。

## 为什么使用 PixelTerm-C

- 同一套流程里就能看图片、动图、视频和电子书。
- 支持单图查看、网格预览、电子书阅读和文件管理。
- 键盘和鼠标都能用，适合在终端里连续浏览、切换内容。
- 预加载、抖动、伽马校正和终端相关配置都可以按需调整。
- 提供 Linux 和 macOS 的预编译二进制，也可以自己从源码构建。

## 截图

下面是 PixelTerm-C 的实际界面：

<img src="../../screenshots/2.png" alt="PixelTerm-C 截图">

截图截自真实运行会话。

## 安装

macOS 和 Linux 推荐直接用一条命令安装：

```bash
curl -fsSL https://raw.githubusercontent.com/zouyonghe/PixelTerm-C/main/scripts/install.sh | bash
```

安装脚本会自动识别 `macOS/Linux + amd64/arm64`，拉取最新 GitHub Release，对应安装到 `/usr/local/bin/pixelterm`。只有在目标目录不可写时才会调用 `sudo`。

如果你不想安装到 `/usr/local/bin`，也可以改成自定义目录：

```bash
curl -fsSL https://raw.githubusercontent.com/zouyonghe/PixelTerm-C/main/scripts/install.sh | bash -s -- --bin-dir "$HOME/.local/bin"
```

如果你更偏向包管理器或手动安装，也可以继续用下面这些方式：

```bash
# Arch Linux (AUR)
paru -S pixelterm-c
# 或
yay -S pixelterm-c

# Linux amd64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-linux
chmod +x pixelterm-amd64-linux && sudo mv pixelterm-amd64-linux /usr/local/bin/pixelterm

# Linux arm64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-linux
chmod +x pixelterm-arm64-linux && sudo mv pixelterm-arm64-linux /usr/local/bin/pixelterm

# macOS amd64
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-macos
chmod +x pixelterm-amd64-macos && sudo mv pixelterm-amd64-macos /usr/local/bin/pixelterm

# macOS arm64 (Apple Silicon)
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-macos
chmod +x pixelterm-arm64-macos && sudo mv pixelterm-arm64-macos /usr/local/bin/pixelterm
```

如果 macOS 阻止运行已安装的二进制，可以先移除 quarantine 属性：

```bash
xattr -dr com.apple.quarantine /usr/local/bin/pixelterm
```

## 快速开始

```bash
# 打开单张图片
pixelterm /path/to/image.jpg

# 播放视频（仅视频，无音频）
pixelterm /path/to/video.mp4

# 阅读电子书
pixelterm /path/to/book.pdf

# 浏览目录
pixelterm /path/to/directory

# 查看命令行帮助
pixelterm --help
```

更多命令示例和参数说明见 [USAGE_zh.md](../guides/USAGE_zh.md)。

## 格式与兼容性

- 图片：JPG、PNG、GIF、BMP、WebP、TIFF 等常见格式。
- 视频：MP4、MKV、AVI、MOV、WebM、MPEG/MPG、M4V（仅视频，无音频）。
- 电子书：PDF、EPUB、CBZ；需在构建时启用 MuPDF 支持。
- 程序会自动选择合适的输出方式，必要时也可以用 `--protocol` 手动指定。
- 终端与协议说明见 [TERMINAL_PROTOCOL_SUPPORT_zh.md](../guides/TERMINAL_PROTOCOL_SUPPORT_zh.md)。

## 配置

PixelTerm-C 会读取 `$XDG_CONFIG_HOME/pixelterm/config.ini`；如果 `XDG_CONFIG_HOME` 未设置或为空，则回退到 `$HOME/.config/pixelterm/config.ini`。也可以通过 `--config` 指定其他配置文件。建议先从 [`config.example.ini`](../../config.example.ini) 开始：把通用设置放在 `[default]`，再按终端环境添加覆盖分组，匹配 `TERM_PROGRAM`、`LC_TERMINAL`、`TERMINAL_NAME` 或 `TERM`。命令行参数会在读取配置文件之后解析，因此显式传入的 CLI 参数会覆盖配置值。

快速初始化：

```bash
mkdir -p ~/.config/pixelterm
cp config.example.ini ~/.config/pixelterm/config.ini
```

## 文档

- [README.md](../README.md)
- [USAGE_zh.md](../guides/USAGE_zh.md)
- [CONTROLS_zh.md](../guides/CONTROLS_zh.md)
- [TROUBLESHOOTING_zh.md](../guides/TROUBLESHOOTING_zh.md)
- [CHANGELOG.md](../../CHANGELOG.md)
- [TERMINAL_PROTOCOL_SUPPORT_zh.md](../guides/TERMINAL_PROTOCOL_SUPPORT_zh.md)

## 源码构建

先安装依赖：

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config build-essential
# 可选：电子书支持
sudo apt-get install libmupdf-dev

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 ffmpeg pkgconf base-devel
# 可选：电子书支持
sudo pacman -S mupdf

# macOS (Homebrew)
brew install chafa glib gdk-pixbuf ffmpeg pkg-config
# 可选：电子书支持
brew install mupdf
```

然后执行构建：

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make

# 二进制输出
bin/pixelterm

# 可选：安装到系统路径
sudo make install

# 交叉编译到 aarch64
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
```

如果系统里装了 MuPDF，构建时会自动启用电子书支持。交叉编译仍属于实验性流程，需要宿主机提供对应架构的依赖库。

## 许可证

LGPL-3.0-or-later，详见 [`LICENSE`](../../LICENSE)。

项目使用与 [Chafa](https://github.com/hpjansson/chafa) 相同的许可证体系（LGPLv3+）。
