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

## 🚀 快速开始

### 安装依赖

```bash
# 1. 安装系统依赖（必需）
# Arch Linux
sudo pacman -S base-devel chafa glib2 gdk-pixbuf2 pkgconf

# Ubuntu/Debian  
sudo apt-get update
sudo apt-get install build-essential libchafa-dev libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config

# Fedora/CentOS/RHEL
sudo dnf install gcc gcc-c++ make chafa-devel glib2-devel gdk-pixbuf2-devel pkgconfig

# 2. 克隆并构建
git clone https://github.com/zouyonghe/PixelTerm-C.git
cd PixelTerm-C
make
```

**依赖说明**：
- **系统库**：必须安装chafa、glib2和gdk-pixbuf2开发文件
- **构建工具**：GCC或Clang编译器配合make工具
- **包管理器**：pkg-config用于正确的库检测

### 基本用法

```bash
# 浏览当前目录图像
./pixelterm-c

# 浏览指定目录图像
./pixelterm-c /path/to/images

# 查看单个图像
./pixelterm-c image.jpg
```

### 安装

```bash
sudo make install
```

## 🎮 控制

| 按键 | 功能 |
|-----|------|
| ←/→ | 上一张/下一张图像 |
| a/d | 备用左右键（兼容模式） |
| i   | 切换详细信息显示 |
| r   | 删除当前图像 |
| q   | 退出程序 |
| ESC | 退出程序 |
| Ctrl+C | 强制退出 |

## 📁 目录导航

- **自动扫描** - 启动时自动扫描当前目录所有图像文件
- **智能排序** - 按文件名排序便于浏览
- **循环导航** - 到达最后一张后自动回到第一张
- **文件支持** - 支持直接指定图像文件或目录

## ⚙️ 高级功能

### 显示协议支持
- **自动检测** - 自动选择最优显示协议：
  - iTerm2/iTerm3（最高分辨率）
  - Kitty（高分辨率）
  - Sixels（中等分辨率）
  - Symbols（通用兼容性）

### 终端适配
- **实时响应** - 终端窗口大小变化时自动重绘
- **尺寸优化** - 智能计算最优显示尺寸
- **光标管理** - 浏览时隐藏光标，退出时恢复

### 性能优化
- **内存缓存** - 预加载图像列表避免重复扫描
- **流处理** - 高效的按键序列处理
- **快速响应** - 优化的输入处理逻辑

## 🔧 技术实现

### 核心架构
```
PixelTerm-C/
├── 🖼️ 图像显示 (renderer.c)
├── 📁 文件浏览器 (browser.c)
├── 🎮️ 用户界面 (input.c)
├── ⚙️ 配置管理 (app.c)
├── 🔄 预加载系统 (preloader.c)
├── 🔧 通用工具 (common.c)
└── 🚀 主程序 (main.c)
```

### 关键技术
- **直接Chafa集成** - 直接使用Chafa库消除子进程开销
- **多线程预加载** - 后台图像准备实现流畅导航
- **智能缓存** - LRU缓存自动清理
- **线程安全** - 共享资源的互斥保护
- **终端状态管理** - 正确的光标和属性重置

## 📊 性能对比

| 指标 | Python版本 | C版本 | 改进幅度 |
|--------|---------------|-----------|-------------|
| 启动时间 | ~1-2s | ~0.2s | 5-10倍更快 |
| 图像切换 | ~200-500ms | ~50-100ms | 3-5倍更快 |
| 内存使用 | ~50-100MB | ~20-30MB | 2-3倍减少 |
| CPU使用 | 高（Python + 子进程） | 中等（纯C） | 2-4倍减少 |

## 📦 项目信息

- **编程语言**: C11
- **核心依赖**: Chafa, GLib 2.0, GDK-Pixbuf 2.0
- **代码规模**: 7个源文件，1000+行代码
- **许可证**: MIT许可证
- **仓库**: https://github.com/zouyonghe/PixelTerm-C

## 🎯 设计理念

- **性能优先** - 专注于核心功能，消除冗余信息
- **用户友好** - 直观操作，无学习曲线
- **高性能** - 快速响应和流畅体验
- **强兼容性** - 支持各种终端环境

## 🔗 相关项目

### Python版本
- **[PixelTerm (Python)](https://github.com/zouyonghe/PixelTerm)** - 原始Python实现，功能丰富
- **性能对比**：C版本提供5-10倍更好性能，内存使用显著降低

## 🤝 贡献

1. Fork仓库
2. 创建功能分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m '添加神奇功能'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 开启Pull Request

### 开发指南

- 遵循C11标准
- 使用一致的代码风格
- 添加适当的错误处理
- 包含内存管理
- 为新功能编写测试

## 📄 许可证

MIT许可证 - 详见LICENSE文件

## 🚀 路线图

- [ ] 高级图像滤镜和调整
- [ ] 插件系统支持自定义扩展
- [ ] 网络图像浏览支持
- [ ] 配置文件支持
- [ ] 批处理功能
- [ ] GUI模式集成

---

**PixelTerm-C** - 让终端成为出色的图像查看器！🖼️