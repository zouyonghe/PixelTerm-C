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

## 性能对比

| 指标 | Python版本 | C版本 | 改进幅度 |
|--------|---------------|-----------|-------------|
| 启动时间 | ~1-2s | ~0.2s | 5-10倍更快 |
| 图像切换 | ~200-500ms | ~50-100ms | 3-5倍更快 |
| 内存使用 | ~50-100MB | ~20-30MB | 2-3倍减少 |
| CPU使用 | 高（Python + 子进程） | 中等（纯C） | 2-4倍减少 |

## 🚀 快速开始

### 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config build-essential

# Arch Linux
sudo pacman -S chafa glib2 gdk-pixbuf2 pkgconf base-devel
```

### 快速安装 (Linux amd64)

```bash
# 下载并安装最新二进制文件
LATEST_VERSION=$(curl -s https://api.github.com/repos/zouyonghe/PixelTerm-C/releases/latest | grep '"tag_name"' | cut -d'"' -f4)
wget https://github.com/zouyonghe/PixelTerm-C/releases/download/${LATEST_VERSION}/pixelterm-c-${LATEST_VERSION} -O pixelterm-c
chmod +x pixelterm-c
sudo mv pixelterm-c /usr/local/bin/

# 或者仅下载到当前目录
LATEST_VERSION=$(curl -s https://api.github.com/repos/zouyonghe/PixelTerm-C/releases/latest | grep '"tag_name"' | cut -d'"' -f4)
wget https://github.com/zouyonghe/PixelTerm-C/releases/download/${LATEST_VERSION}/pixelterm-c-${LATEST_VERSION} -O pixelterm-c
chmod +x pixelterm-c
./pixelterm-c /path/to/images
```

### 源码构建

```bash
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make
```

### 使用

```bash
# 浏览图像
./pixelterm-c /path/to/images
```

## 🎮 控制

| 按键 | 功能 |
|-----|------|
| ←/→ | 上一张/下一张图像 |
| a/d | 备用左右键 |
| i | 切换详细信息显示 |
| r | 删除当前图像 |
| q 或 ESC | 退出程序 |
| Ctrl+C | 强制退出 |

## 🔗 相关项目

### Python版本
- **[PixelTerm (Python)](https://github.com/zouyonghe/PixelTerm)** - 原始Python实现，功能丰富
- **性能对比**：C版本提供5-10倍更好性能，内存使用显著降低

## 📄 许可证

MIT许可证

---

**PixelTerm-C** - 让终端成为出色的图像查看器！🖼️