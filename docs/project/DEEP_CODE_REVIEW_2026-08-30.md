# PixelTerm-C 深层代码审查报告

- 日期: 2026-08-30
- 范围: 视频解码/停止生命周期、FFmpeg 输入边界、跨线程状态访问
- 目标: 优先发现低可见度、高后果问题
- 方法: CodeGraph 源码追踪、两次独立攻击性复核、调用链核对
- 结论: 发现 2 条高严重度问题，1 条中严重度问题；修复已完成并通过测试

## 结论摘要

本次审查发现的问题集中在视频播放生命周期。两条高严重度问题都能从当前代码直接
构造触发路径：停止操作无法打断阻塞式 FFmpeg I/O；解码器对每帧参数变化没有防护。
两者分别造成确定性永久卡死和潜在库内越界读取/崩溃。

## Findings

### H-01: 停止操作无法打断阻塞式 FFmpeg I/O

- 严重度: **High**
- 位置: `src/video_player.c:1095`, `src/video_player.c:1227`, `src/video_player.c:1740-1746`
- 影响: 退出、切换媒体、暂停或 seek 永久卡死；进程无法响应 SIGINT 后完成退出

触发链：

1. `video_player_load()` 将路径交给 `avformat_open_input()` 和
   `avformat_find_stream_info()`，未设置 `AVIOInterruptCB`。
2. decode worker 在 `av_read_frame()` 中读取同一输入。
3. `video_player_stop()` 设置 `worker_stop` 后无条件 `g_thread_join()`。
4. 若输入是 FIFO、管道型文件、网络流或底层 I/O 长时间不返回，worker 永远停在
   FFmpeg 调用内部，无法重新检查 `worker_stop`。

当前代码还使用 `stat()`/`file_exists()`，没有限制 `video_player_load()` 只能接收普通
文件，因此 FIFO 是本地可构造的确定性复现输入，不依赖网络。

修复方向：

- 在 `AVFormatContext` 上设置 `interrupt_callback.callback` 和 `.opaque`。
- callback 只读取原子停止标志，并在 stop 时返回非零。
- 对 open/probe 阶段同样使用该 callback。
- 增加 FIFO/阻塞输入的子进程回归测试，断言 stop 在有限时间内完成。
- 可额外拒绝非 `S_ISREG` 输入，但不能替代 interrupt callback，因为普通文件也可能
  受到网络文件系统或设备 I/O 阻塞。

### H-02: 解码 worker 使用固定 SWS/buffer 处理可能变化的帧参数

- 严重度: **High**
- 位置: `src/video_player.c:1240-1248`, `src/video_player.c:1330-1351`,
  `src/video_player.c:1799-1821`, `src/video_player_decode.c:37-73`
- 影响: 恶意或异常媒体触发错误转换、错误帧、崩溃，潜在越界读取

`video_player_load()` 只根据初始 `codec_context->width`、`height` 和 `pix_fmt` 创建：

- `SwsContext`
- `rgba_frame` backing buffer
- `player->video_width` / `player->video_height`

之后 worker 对每个 `AVFrame` 直接调用：

```c
sws_scale(player->sws_context,
          ...,
          player->codec_context->height,
          ...,
          player->rgba_frame->linesize);
```

随后又使用 player 初始化时的固定尺寸验证和复制。代码没有比较或验证
`decode_frame->width`、`decode_frame->height`、`decode_frame->format`，也没有在参数变化
时重建 SWS context 和输出 buffer。

对发生 mid-stream resolution/pixel-format change 的输入，旧转换上下文可能按旧布局
读取新 frame。尺寸变小时存在从较小 source frame 读取超出实际行数据的风险；尺寸变大
时则可能产生截断或输出 buffer 不足。具体越界是否由当前 FFmpeg 版本内部拦截，不能作为
应用层安全保证。

修复方向：

- 在 `avcodec_receive_frame()` 成功后读取 frame 的实际 width/height/format。
- 要求参数与当前转换上下文一致；不一致时停止/丢弃该帧并安全重建 SWS + RGBA buffer。
- 重建前清理旧 context/buffer，并重新通过统一 pixel/byte limits。
- 以动态分辨率和动态 pixel format 样本建立回归测试；至少断言不会 crash、不会复制
  超出目标 buffer。

### M-01: seek 在停止 decode worker 前读取 FFmpeg context

- 严重度: **Medium**
- 位置: `src/video_player.c:1954-1986`
- 影响: FFmpeg context 并发访问；具体表现依赖 demuxer，可能出现 seek 时竞态、状态
  错乱或崩溃

`video_player_seek_relative_ms()` 在 `was_playing` 判断和 worker stop 之前读取：

- `player->format_context->duration`
- `player->format_context->streams[...]`
- 当前播放状态

decode worker 同时可能在 `av_read_frame()` 使用同一 `AVFormatContext`。当前读取 duration
通常不会写该字段，因此不能把它直接定性为必现 data race；但 FFmpeg demuxer/context
并发访问不应依赖这一实现细节。修复方向是先获得必要的受保护播放状态，再停止/join
worker，最后读取 duration/stream 并执行 seek；或用专用 seek mutex 串行化所有 FFmpeg
context 操作。

## 未确认项目

以下项目本次不作为 finding：

- timer callback UAF：当前所有 app 生命周期调用均位于主循环路径；没有证据证明 destroy
  会与同一 GLib context 的 callback 并发执行。
- symlink 删除越界：文件管理器确实跟随目录 symlink，但删除动作是用户主动操作，且
  目标文件本身通过 `lstat()` 拒绝 symlink；当前证据不足以证明违反产品安全边界。
- input CSI 32-byte buffer：循环保留 NUL 且限制 mouse 参数范围，未发现越界。
- `video_player_new` 部分初始化回滚：GLib 默认分配 API 不返回可继续执行的 OOM 半状态，
  原报告结论为误报。

## 修复状态

- H-01 已修复：`video_player_load()` 在 open/probe 前安装 `AVIOInterruptCB`，callback
  读取原子 `worker_stop`；stop 设置标志后可打断阻塞式 FFmpeg I/O。
- H-02 已修复：播放 worker 在每帧转换前校验实际 width/height/format；发现变化时记录
  事件、停止 worker、更新播放状态并广播队列条件变量，不再使用旧 SWS/buffer。
  first-frame 路径同样拒绝动态参数帧。
- M-01 已修复：`video_player_seek_relative_ms()` 先停止运行中的 worker，再访问
  `AVFormatContext` 的 duration/stream 字段；边界 no-op 会恢复原播放循环。

修复涉及：`src/video_player.c`、`src/video_player_decode.c`、`include/video_player.h`、
`include/video_player_decode_internal.h`、`tests/test_video_player.c`。

验证：`make -j8`、`make -j8 test`、`make -j8 debug-test` 均通过；新增 2 个 decoder
安全不变量测试。

## 建议修复优先级

1. H-01：安装 interrupt callback，先解决不可控永久卡死。
2. H-02：建立 per-frame layout invariant，拒绝或重建动态参数帧。
3. M-01：统一 FFmpeg context 的停止/seek 串行化规则。
4. 为三项修复补充失败前 RED 测试，再运行 ASan/UBSan/TSan 和完整测试。
