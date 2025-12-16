# PixelTerm-C - 高性能终端图像查看器

*[English](README.md) | 中文*

🖼️ 基于Chafa库编写的C语言高性能终端图像浏览器。

## 概述

PixelTerm-C是原始PixelTerm应用的C语言实现，旨在提供显著更好的性能，同时保持所有相同的功能。通过直接利用Chafa库而不是使用子进程调用，我们消除了Python解释和外部进程创建的开销。

更新日志（英文）：见 `CHANGELOG.md`。

## 🌟 特性

- 🖼️ **多格式支持** - 支持JPG、PNG、GIF、BMP、WebP、TIFF等主流图像格式
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

## 🚀 快速开始

### 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config build-essential

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 pkgconf base-devel
```

### 快速安装

```bash
# 使用包管理器安装（推荐）
# Arch Linux: pacman -S pixelterm-c

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

# 交叉编译到aarch64
make CC=aarch64-linux-gnu-gcc ARCH=aarch64
# 注意：交叉编译是实验性的，需要宿主系统正确安装对应的架构依赖库。
```

### 使用

```bash
# 浏览目录中的图像（有图则直接进入预览网格）
./pixelterm /path/to/images

# 查看单个图像（直接进入查看模式）
./pixelterm /path/to/image.jpg

# 在当前目录运行
./pixelterm

# 显示版本
./pixelterm --version

# 显示帮助
./pixelterm --help

# 禁用预加载
./pixelterm --no-preload /path/to/images

# 启用抖动
./pixelterm -D /path/to/image.jpg
# 或
./pixelterm --dither /path/to/image.jpg

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
| 左键单击 | 切换到下一张图像 | 图像视图（单图模式） | |
| 左键双击 | 切换到网格预览 | 图像视图（单图模式） | |
| 左键单击 | 选中图像 | 网格预览 | 选中光标下的图像。 |
| 左键双击 | 在图像视图中打开选中图像 | 网格预览 | 打开光标位置的图像。 |
| 左键单击 | 选中条目 | 文件管理器模式 | 选中光标下的文件或目录。 |
| 左键双击 | 打开选中条目（目录/文件） | 文件管理器模式 | 进入目录或打开图像。 |
| 鼠标滚轮上/下 | 上一张/下一张图像 | 图像视图（单图模式） | 流畅地浏览图像。 |
| 鼠标滚轮上/下 | 上/下翻页 | 网格预览 | 翻页浏览图像。 |
| 鼠标滚轮上/下 | 向上/向下导航条目 | 文件管理器模式 | 滚动文件和目录列表。 |

*注意：鼠标滚轮事件设有100毫秒的延迟防抖动，以防止滚动过于灵敏。*

### 图像视图（单图模式）

这是查看单张图像时的默认模式。

| 按键 | 功能 |
|-----|----------|
| ←/→ | 上一张/下一张图像 |
| h/l | Vim风格导航（上一张/下一张图像） |
| Enter | 切换进入网格预览模式 |
| TAB | 切换进入文件管理器；再次按TAB返回此视图 |
| i | 切换图像信息显示 |
| r | 删除当前图像 |
| D | 切换抖动开/关 |

### 网格预览（缩略图模式）

此模式以网格形式显示多张图像缩略图。

| 按键 | 功能 |
|-----|----------|
| ←/→ | 移动选中项（左/右） |
| ↑/↓ | 移动选中项（上/下） |
| h/j/k/l | Vim风格导航（左/下/上/右） |
| PgUp/PgDn | 在网格中翻页 |
| Enter | 在图像视图中打开选中图像 |
| TAB | 切换进入文件管理器；再次按TAB返回此视图 |
| D | 切换抖动开/关 |

### 文件管理器模式

此模式允许浏览目录和文件。请注意，此处不支持Vim风格导航（h/j/k/l），因为字母键保留用于快速跳转文件条目。

| 按键 | 功能 |
|-----|----------|
| ←/→ | 进入父目录 / 打开选中目录/文件 |
| ↑/↓ | 向上/向下导航条目 |
| Enter | 打开选中目录或文件 |
| TAB | 返回之前的图像视图或网格预览；若文件管理器是直接进入则无效果 |
| 任意字母键 (a-z/A-Z) | 跳转到以该字母开头的下一个条目 |

## 📄 许可证

LGPL-3.0或更高版本 - 详见LICENSE文件

本项目采用与Chafa相同的许可证（LGPLv3+）。

---

**PixelTerm-C** - 让终端成为出色的图像查看器！🖼️
