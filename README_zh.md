# PixelTerm-C - 高性能终端图像查看器

*[English](README.md) | 中文*

🖼️ 基于Chafa库编写的C语言高性能终端图像浏览器。

## 概述

PixelTerm-C是原始PixelTerm应用的C语言实现，旨在提供显著更好的性能，同时保持所有相同的功能。通过直接利用Chafa库而不是使用子进程调用，我们消除了Python解释和外部进程创建的开销。

## 🌟 特性

- 🖼️ **多格式支持** - 支持JPG、PNG、GIF、BMP、WebP、TIFF等主流图像格式
- 📁 **智能浏览** - 自动检测目录中的图像文件，支持目录导航
- ⌨️ **键盘导航** - 使用方向键在图像间切换，支持各种终端环境
- 📏 **自适应显示** - 自动适应终端大小变化
- 🎨️ **极简界面** - 无冗余信息，专注于图像浏览体验
- ⚡️ **高性能** - 比Python版本快5-10倍，内存使用显著降低
- 🔄 **循环导航** - 在首尾图像间无缝浏览
- 📊 **详细信息** - 可切换的全面图像元数据显示
- 🎯 **蓝色文件名** - 彩色编码文件名显示，提高可见性
- 🏗️ **多架构支持** - 原生支持amd64和aarch64（ARM64）架构
- 📦 **预加载** - 可选的图像预加载功能，实现更快导航
- 📋 **智能帮助** - 未找到图像时自动显示版本和帮助信息

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

# 启动时显示图像信息
./pixelterm --info /path/to/images

# 禁用预加载
./pixelterm --no-preload /path/to/images

# 直接进入预览网格（打开含图片的目录默认如此）
./pixelterm /path/to/images   # 单图模式下按 Enter 或 p 返回网格
```

预览网格基础用法：
- 打开含图片的目录会默认进入预览网格；单图模式下按 `Enter` 或 `p` 进入网格。
- 使用方向键/PgUp/PgDn 移动，`+`/`-` 调节缩略图大小（至少 2 列），`Enter` 打开选中图片。

## 🎮 控制

| 按键 | 功能 |
|-----|------|
| ←/→ | 上一张/下一张图像 |
| a/d | 备用左右键 |
| ↑/↓ | 移动选中（预览/文件管理器） |
| PgUp/PgDn | 预览网格翻页 |
| p | 网格预览模式（方向键/PgUp/PgDn 移动，Enter 打开） |
| +/- | 放大/缩小预览缩略图 |
| TAB | 切换文件管理器（无图片时会直接退出） |
| i | 切换详细信息显示 |
| r | 删除当前图像 |
| q | 返回上个界面（图片浏览时即退出程序） |
| ESC | 任意模式直接退出程序 |
| Ctrl+C | 强制退出 |

文件管理器：
- ↑/↓导航，Enter/→ 打开，← 返回上级目录。
- 任意字母键（a–z/A–Z）按首字母跳转到下一条目。
- q 返回上一界面（图片浏览即退出）；TAB 切换文件管理器；ESC 直接退出程序。

## 更新日志（相对 v1.0.12）
- v1.0.13：预览网格体验升级（首次进入仅渲染一次、缩放更可控且至少2列、分页/翻页更顺畅），预加载暂停/禁用时真正停工并修复缓存占用问题，帮助/版本/参数错误提示更清晰。

## 📄 许可证

LGPL-3.0或更高版本 - 详见LICENSE文件

本项目采用与Chafa相同的许可证（LGPLv3+）。

---

**PixelTerm-C** - 让终端成为出色的图像查看器！🖼️
