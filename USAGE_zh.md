# 使用

```bash
# 查看单个图像（直接进入图像查看器）
pixelterm /path/to/image.jpg

# 播放视频（仅视频，无音频）
pixelterm /path/to/video.mp4

# 阅读电子书（PDF/EPUB/CBZ）
pixelterm /path/to/book.pdf

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

# 控制备用屏幕缓冲区
pixelterm --alt-screen false /path/to/images
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
# 说明：默认 1.0
pixelterm --gamma 0.8 /path/to/image.jpg

# 加载配置文件（默认：$XDG_CONFIG_HOME/pixelterm/config.ini）
pixelterm --config ~/.config/pixelterm/config.ini /path/to/image.jpg

# 配置文件格式：用 [default] 作为基础配置，按终端名新增分组
# （匹配 TERM_PROGRAM/LC_TERMINAL/TERMINAL_NAME/TERM）覆盖。
# 见 config.example.ini
```
