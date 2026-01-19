# PixelTerm-C - 高性能终端图像查看器

![Version](https://img.shields.io/badge/Version-v1.5.10-blue)
![License](https://img.shields.io/badge/License-LGPL--3.0-orange)

*[English](README.md) | 中文*

🖼️ 基于[Chafa](https://github.com/hpjansson/chafa)库编写的C语言高性能终端图像浏览器。

## 概述

PixelTerm-C是原始PixelTerm应用的C语言实现，旨在提供显著更好的性能，同时保持所有相同的功能。通过直接利用Chafa库而不是使用子进程调用，我们消除了Python解释和外部进程创建的开销。

更新日志（英文）：见 `CHANGELOG.md`。

## 🌟 特性

- 🖼️ **多格式支持** - 支持JPG、PNG、GIF、BMP、WebP、TIFF等主流图像格式
- 🎬 **动画GIF支持** - 终端内播放动图，时序准确、渲染清晰
- 🎥 **视频播放** - 在终端内播放 MP4、MKV、AVI、MOV、WebM、MPEG/MPG、M4V 视频（仅视频，无音频）
- 🎨 **TrueColor渲染** - 全24位色彩支持，自动检测与优化
- 📁 **智能浏览** - 自动检测目录中的图像文件，支持目录导航
- ⌨️ **键盘导航** - 使用方向键在图像间切换，支持各种终端环境
- 📏 **自适应显示** - 自动适应终端大小变化
- 🎨️ **极简界面** - 无冗余信息，专注于图像浏览体验
- ⚡️ **高性能** - 比Python版本快5-10倍，内存使用显著降低
- 🔄 **循环导航** - 在首尾图像间无缝浏览
- 🏗️ **多架构支持** - 原生支持amd64和aarch64（ARM64）架构
- 🖱️ **鼠标支持** - 在所有模式下支持直观的鼠标导航、选择和滚动
- 📦 **预加载** - 图像预加载功能，实现更快导航（默认开启）。
- 🎨 **抖动** - 在色彩受限的终端中带来更好的视觉效果（默认关闭）。

## 性能对比

| 指标 | Python版本 | C版本 | 改进幅度 |
|--------|---------------|-----------|-------------|
| 启动时间 | ~1-2s | ~0.1-0.3s | 数倍提升 |
| 图像切换 | ~200-500ms | ~50-150ms | 2-5倍更快 |
| 内存使用 | ~50-100MB | ~15-35MB | 2-3倍减少 |
| CPU使用 | 高（Python + 子进程） | 中等（纯C） | 明显减少 |

## 📸 截图展示

以下是一张展示PixelTerm-C实际运行效果的截图：

<img src="screenshots/2.png" alt="PixelTerm-C 截图">

*图片由zimage生成，终端使用：Warp*

当前测试中，图片效果相对最好、色彩还原度最高的终端：
- [rio](https://github.com/raphamorim/rio)
- [ghostty](https://github.com/ghostty-org/ghostty)
- [warp](https://www.warp.dev/)
- [iterm2](https://github.com/gnachman/iTerm2)
- [contour](https://github.com/contour-terminal/contour)

## 🚀 快速开始

### 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config build-essential

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 ffmpeg pkgconf base-devel
```

### 快速安装

```bash
# 使用包管理器安装（推荐）
# Arch Linux: paru -S pixelterm-c 或yay -S pixelterm-c

# 或下载对应架构的二进制文件
# AMD64:
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-linux
chmod +x pixelterm-amd64-linux && sudo mv pixelterm-amd64-linux /usr/local/bin/pixelterm

# ARM64:
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-linux
chmod +x pixelterm-arm64-linux && sudo mv pixelterm-arm64-linux /usr/local/bin/pixelterm

# macOS AMD64:
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-amd64-macos
chmod +x pixelterm-amd64-macos && sudo mv pixelterm-amd64-macos /usr/local/bin/pixelterm

# macOS ARM64 (Apple Silicon):
wget https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/pixelterm-arm64-macos
chmod +x pixelterm-arm64-macos && sudo mv pixelterm-arm64-macos /usr/local/bin/pixelterm

# 注意：macOS用户如果因安全限制无法启动，请运行：
# xattr -dr com.apple.quarantine pixelterm-arm64-macos
```

### 源码构建

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make

# 可执行文件输出在 pixelterm
# （或使用：sudo make install 安装到系统）

# 交叉编译到aarch64
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
# 注意：交叉编译是实验性的，需要宿主系统正确安装对应的架构依赖库。
```

### 使用

```bash
# 查看单个图像（直接进入图像查看器）
pixelterm /path/to/image.jpg

# 播放视频（仅视频，无音频）
pixelterm /path/to/video.mp4

# 浏览目录（进入文件管理器模式）
pixelterm /path/to/directory

# 在当前目录运行（进入文件管理器模式）
pixelterm

# 显示版本
pixelterm --version

# 显示帮助
pixelterm --help

# 禁用预加载
pixelterm --no-preload /path/to/images

# 禁用备用屏幕缓冲区
pixelterm --no-alt-screen /path/to/images
# 说明：主要用于 Warp 终端，通常情况下不需要。

# 改善部分终端的界面显示（可能降低性能）
pixelterm --clear-workaround /path/to/images
# 说明：主要用于 Warp 终端，通常情况下不需要。

# 启用抖动
pixelterm -D /path/to/image.jpg
# 或
pixelterm --dither /path/to/image.jpg

# 调整渲染 work factor（1-9，越高越慢但质量更好）
pixelterm --work-factor 7 /path/to/image.jpg

# 强制输出协议（auto, text, sixel, kitty, iterm2）
pixelterm --protocol kitty /path/to/image.jpg

# 图像渲染伽马校正
# 说明：kitty 下默认 0.5，其他终端默认 1.0
pixelterm --gamma 0.8 /path/to/image.jpg

```

## 🎮 控制



### 全局控制

| 按键 | 功能 |
|-----|----------|
| ESC | 退出应用程序 |
| Ctrl+C | 强制退出 |

### 鼠标控制

鼠标交互显著增强了不同模式下的导航和选择体验。

| 操作 | 功能 | 适用模式 | 说明 |
|---------|----------|------------------|-------|
| 左键单击 | 切换到下一张图像 | 图像视图（单图模式） | 视频时为播放/暂停。 |
| 左键双击 | 切换到网格预览 | 图像视图（单图模式） | |
| 左键单击 | 选中图像 | 网格预览 | 选中光标下的图像。 |
| 左键双击 | 在图像视图中打开选中图像 | 网格预览 | 打开光标位置的图像。 |
| 左键单击 | 选中条目 | 文件管理器模式 | 选中光标下的文件或目录。 |
| 左键双击 | 打开选中条目（目录/文件） | 文件管理器模式 | 进入目录或打开图像。 |
| 鼠标滚轮上/下 | 上一张/下一张图像 | 图像视图（单图模式） | 流畅地浏览图像。 |
| 鼠标滚轮上/下 | 上/下翻页 | 网格预览 | 翻页浏览图像。 |
| 鼠标滚轮上/下 | 向上/向下导航条目 | 文件管理器模式 | 滚动文件和目录列表。 |

### 图像视图（单图模式）

这是查看单张图像时的默认模式。

| 按键 | 功能 |
|-----|----------|
| ←/↑ | 上一张图像 |
| →/↓ | 下一张图像 |
| h/k | Vim风格导航（上一张图像） |
| l/j | Vim风格导航（下一张图像） |
| 空格 | 视频播放/暂停（仅视频） |
| F | 显示/隐藏 FPS（仅视频） |
| + / - | 调整视频缩放（仅视频） |
| Enter | 切换进入网格预览模式 |
| TAB | 在图像视图 / 网格预览 / 文件管理器间循环切换 |
| i | 切换图像信息显示 |
| `~` / `` ` `` | 切换Zen模式（隐藏/显示所有文字信息） |
| r | 删除当前图像 |
| d/D | 切换抖动开/关 |

### 网格预览（缩略图模式）

此模式以网格形式显示多张图像缩略图。

| 按键 | 功能 |
|-----|----------|
| ←/→ | 移动选中项（左/右） |
| ↑/↓ | 移动选中项（上/下） |
| h/j/k/l | Vim风格导航（左/下/上/右） |
| PgUp/PgDn | 在网格中翻页 |
| Enter | 在图像视图中打开选中图像 |
| TAB | 在图像视图 / 网格预览 / 文件管理器间循环切换 |
| `~` / `` ` `` | 切换Zen模式（隐藏/显示所有文字信息） |
| r | 删除选中图像 |
| d/D | 切换抖动开/关 |
| +/= | 放大 |
| - | 缩小 |

### 文件管理器模式

此模式允许浏览目录和文件。请注意，此处不支持Vim风格导航（h/j/k/l），因为字母键保留用于快速跳转文件条目。

| 按键 | 功能 |
|-----|----------|
| ←/→ | 进入父目录 / 打开选中目录/文件 |
| ↑/↓ | 向上/向下导航条目 |
| Enter | 打开选中目录或文件 |
| TAB | 在图像视图 / 网格预览 / 文件管理器间循环切换 |
| Backspace | 显示/隐藏隐藏文件 |
| 任意字母键 (a-z/A-Z) | 跳转到以该字母开头的下一个条目 |

## 📄 许可证

LGPL-3.0或更高版本 - 详见LICENSE文件

本项目采用与[Chafa](https://github.com/hpjansson/chafa)相同的许可证（LGPLv3+）。

---

**PixelTerm-C** - 让终端成为出色的图像查看器！🖼️
