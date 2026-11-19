# 视频播放性能设计与验证

## 目标与边界

在不破坏播放、暂停、seek、EOF、窗口缩放及协议回退的前提下，减少无效工作、内存流量和帧间抖动。平均 FPS 不是唯一指标；同时观察耗时分布、队列等待、迟到丢帧和帧提交间隔。

本文记录设计与实验协议；任务状态以 Beads epic `pixelterm-lzv` 为准。每个改动独立测试，未测出的收益不写成结论。不引入 AI 补帧、通用 GPU 框架或额外依赖。

## 当前链路与证据

`av_read_frame → avcodec_receive_frame → PTS/迟到判断（拒绝则跳过转换） → sws_scale(RGBA) → g_memdup2 → decode_queue → render workers（RGB direct／Chafa 回退） → frame_queue → GLib tick → stdout`

- `src/video_player.c`：解码工作线程在转换前丢弃迟到帧；队列使用单独复制的 RGBA；渲染工作线程持有各自 renderer。
- `src/kitty_graphics.c`：本地共享内存路径仍逐帧创建对象，并复制或缩放 RGBA，不是 GPU 零拷贝。
- `src/renderer.c`：`renderer_setup_canvas()` 每次创建 Chafa canvas。
- `src/video_player_playback.c`：已按播放时钟和队首 PTS 计算延迟，并有防饥饿丢帧策略。不能把现有实现描述为固定延迟播放。
- 线程所有权、锁顺序继续遵守 [线程契约](VIDEO_PLAYER_THREADING.md)。

## 方案取舍

采用“观测 → 局部 CPU 优化 → 调度 → 专用后端”的渐进方案。相比一次性重写 GPU 播放器，改动小、可回归、可定位收益来源。仅调低画质虽简单，但不能替代无损的冗余工作消除。平台专用优化必须保留现有软件路径。

## 实验顺序与验收

| 阶段 | 涉及文件／组件 | 设计与必须验证的边界 |
| --- | --- | --- |
| 基准观测 | `src/video_player.c`, `src/video_player_debug.c`, `scripts/summarize_video_log.py` | 区分解码、转色、渲染、输出；脚本提供样本数、均值、P50/P95/P99、事件计数。无数据输出 null，不能伪造 FPS。 |
| 提前丢帧 | `src/video_player.c`, `tests/test_video_player.c` | 解码和 PTS 推进不变；复用原丢帧策略，但在 `sws_scale` 前执行。测试迟到帧不转换、正常帧转换、无时钟与防饥饿路径不误丢。检查转换失败，不提交旧像素。 |
| 独占／引用计数缓冲 | `include/video_player.h`, `src/video_player_decode.c`, `src/video_player.c` | 先测复制成本，再替换 `g_memdup2`。生产者不能覆盖消费者持有的像素；旧 generation、队列清空、EOF、seek、stop 均释放所有权。保持原布局及字节上限。 |
| canvas／布局复用 | `src/renderer.c`, `include/renderer.h`, `tests/test_renderer.c` | 区分几何计算与 canvas 构建；仅在几何、像素模式、字符模式、色彩、dither 等配置一致时复用。覆盖图像、GIF、视频调用者；不要只比较宽高而漏掉配置。 |
| 绝对时间调度 | `src/video_player.c`, `src/video_player_playback.c` | 持久 GSource 根据单调时钟／PTS 设置截止时间；队列到帧可唤醒主上下文。取消、重排、pause、seek、EOF 不产生重复 source 或 use-after-free。不是实时调度保证。 |
| Kitty 动画／差分 | `src/kitty_graphics.c`, `src/video_player.c`, `src/gif_player.c` | 探测动画子能力，不从支持 Kitty 推导支持动画。使用有界预缓冲、显式图像 ID／旧帧回收；覆盖暂停、seek、resize、退出、配额与不支持时回退。差分对低变化素材和电影分别测量。 |
| VideoToolbox | `src/video_player_decode.c`, `src/video_player.c`, `Makefile` | macOS 条件编译，硬解后先缩放／转色再读回；按端到端耗时而不是纯解码 FPS 决定启用。覆盖不支持 codec／格式、初始化失败、色彩范围及软件回退。 |

按显示像素尺寸提前缩放与缓冲优化一起评估，但不静默改变现有画质。字符模式需要足够采样分辨率，不能直接缩到字符宽高。Kitty `N=1` transient 提示作为协议阶段独立 A/B 实验；不把终端可忽略的提示当成性能保证。

## 可复现测量

固定同一提交、编译参数、素材 SHA-256、FFmpeg／Chafa／GLib 版本、机器、终端版本、协议、字符／像素尺寸、显示刷新率、画质参数和供电状态。每个场景预热一次，再独立运行至少三次，每次使用新日志，避免追加历史混淆。先比较同配置开日志结果，再关闭日志验证真实播放表现；现有日志逐行 flush，会扰动测量。

素材矩阵：720p30、1080p60、4K60；H.264／HEVC／AV1（以本机可用解码器为准）；固定与可变帧率；低变化录屏和快速运动。输出矩阵：文字、Sixel、Kitty direct、本地 Kitty shm；SSH 与 tmux 单独列组，不混入本地结果。

```sh
make -j4
log=$(mktemp "${TMPDIR:-/tmp}/pixelterm-video.XXXXXX")
PIXELTERM_DEBUG_VIDEO=1 PIXELTERM_DEBUG_VIDEO_LOG="$log" bin/pixelterm /absolute/path/to/video.mp4
python3 scripts/summarize_video_log.py "$log"
```

使用绝对路径启动后播放同一段、同样时长；基准运行不要 seek／暂停。交互正确性另测，不能把暂停时长算成卡顿。源时间戳间隔不等于实际提交间隔。

### 指标语义

- `worker-decode-time` 的 `b`：`c=1` 时为解复用和解码耗时（微秒）；旧日志 `c=0` 包含转色，脚本单列为 `legacy_decode_and_convert`，**不能直接与新口径比较**。
- `worker-convert-time` 的 `b`：`sws_scale` 耗时（微秒），只统计实际转换的帧。
- `worker-copy-time` 的 `b`／`c`：RGBA 缓冲复制耗时（微秒）／分配与复制的字节数；会写入一次新测量数据，既有日志没有该事件。
- `worker-render-time` 的 `b`：工作线程渲染／编码耗时（微秒）。
- `render-draw-time` 的 `b`：主线程准备输出与 stdout flush 的耗时（微秒），不是屏幕显示延迟。
- `render-submit` 的 `a`：PTS，`b`：输出结束的单调时间（微秒）。同一次无交互播放内相邻提交时间用于统计间隔和提交 FPS；一个区间样本不能计算稳定 FPS。
- 只有完整帧写入并成功 flush 才记录 `render-submit` 并更新呈现时钟／FPS。`render-output-error` 单独记录失败并终止当前输出循环，不把未送达的数据计入提交率。删除送达不确定时不重放删除，只重试 ST 帧界恢复。
- `render-wait` 的 `b`：下一队首帧的 PTS（毫秒），无队首时为 -1；它本身不是次数。摘要中的 `empty_queue_retries` 统计 `b=-1` 的事件数量，不是“卡顿事件数”或独立耗尽次数。其他等待可能只是下一帧尚未到 PTS。
- `worker-drop-late`／`worker-drop-stale`：各自事件计数，不等于总丢帧率；播放选帧也会淘汰帧，没有完整帧账本前不输出总丢帧率。

统计采用 nearest-rank 百分位：排序后取 `ceil(p*N)-1`；样本过少仍显示样本数，不能将其作为稳定尾延迟结论。脚本只做离线汇总，不改变运行时配置。

追加日志中的各播放段通过 `submission_runs` 分开报告；`play-start`、`tick-stop`、输出失败或 PTS 不再递增都会分段，已结束播放的数据不会丢失。多段日志的顶层提交 FPS 为 null、区间样本数为 0，不混合不同播放过程；耗时和事件计数仍是整份日志汇总。

## 正确性与性能门槛

每项先补失败回归测试，再做最小实现，运行针对性测试与 `make test`。内存或线程生命周期变化还需 `make debug-test`、`make ubsan-test`、`make tsan-test`；区分实际运行过的测试与只列出的命令。

保留原图像／字幕区域、颜色、宽高比；验证 pause/resume、前后 seek、EOF 后重播、resize、协议切换、停止销毁及错误回退。逻辑／微基准能证明“少执行了转换或拷贝”，不能证明屏幕 FPS 提升。真实显示验收必须在目标终端完成。

性能报告给出三次原始结果和中位数，比较相关阶段耗时及 P95/P99，同时检查提交间隔、CPU、峰值 RSS、等待和丢弃计数。允许结论是“没有收益”或“需实机验证”，不为凑齐优化而默认启用更复杂路径。

## 首批验证记录

本批完成观测工具和提前丢帧，不包含后续缓冲、canvas、调度或专用后端实现。环境为 macOS arm64，构建使用本机 FFmpeg 9.0.2、Chafa 1.18.3、GLib 2.90.1；不据此推断其他平台性能。

使用 `tests/test_video_player.c` 内嵌的六帧视频，运行实际解码线程，保持深渲染队列和落后 PTS。修改前回归测试按预期失败：解码 6、转换 6、丢弃 6；移动转换位置后通过：解码 6、转换 0、丢弃 6。无播放时钟、超过静默期限、rewind 重同步三组测试均解码并转换 6 帧，不误丢。

首批历史验证记录（此时摘要脚本尚未加入 copy 和单区间测试，6 项；当前脚本为 10 项）：

| 命令 | 实际结果 |
| --- | --- |
| `python3 scripts/test_summarize_video_log.py` | 6 项通过 |
| `make -s -j4 test` | 首批完整套件通过，附带 Python／启动测试通过；最新计数见下文 |
| `make -s -j4 debug-test` | 首批 ASan 完整套件通过；最新计数见下文 |
| `make -s -j4 ubsan-test` | 首批 UBSan 完整套件通过；最新计数见下文 |
| `make -s -j4 tsan-test` | 针对性并发套件通过，21 项，包括 4 项实际解码线程测试 |

这里只证明被拒绝帧省去了转色，未测代表性素材的 CPU／RSS、真实终端显示 FPS 或 P95/P99 改善。计时日志可用于下一轮同配置对照；小型回归素材的微秒耗时不作为性能宣传。

### 4K direct Kitty 初始性能剖析（RGB24 优化前的 Chafa 基线）

对用户提供的 `VID_20220501_230552.mp4` 本地只读探测：H.264、3840×2160、263 帧、8.81 秒，平均帧率约 29.9。PTY 为 160×48、1280×768 像素，在引入 RGB24 优化及图像 ID 分配前运行 `--protocol kitty --kitty-transfer direct`，当时该命令走 Chafa inline，避免共享内存并排除真实 Warp 绘制差异。以下数值不是当前成功分配 ID 后 RGB24 路径的测量结果。

打开诊断日志运行约 3 秒时记录 43 次解码／转换／复制／渲染，42 次提交：转色均值 1.91ms（P50 1.76ms，P95 2.11ms），复制均值 1.07ms（P50 0.84ms，P95 1.29ms），每帧复制 33,177,600 字节；Chafa 渲染均值 15.15ms（P50 15.13ms），stdout 写入加 flush 均值 33.33ms（P50 33.08ms），提交间隔中位数 57.3ms（约 17.4 submit/s）。

同尺寸、关掉诊断日志再跑约 3 秒，PTY 接收 243,618,177 字节、观察到 56 个 Kitty 图像起始指令；真实 Kitty 终端并未绘制，不能称其为显示 FPS。诊断日志自身逐行 flush，首轮吞吐会受额外 I/O 扰动。两次 PTY 时长不同、非三轮样本，不作严格 before/after 对比；但阶段数据表明复制耗时远低于 Chafa 渲染与 stdout 输出，故**不立即引入缓冲池/所有权复杂度**。下一瓶颈更像 Kitty direct 帧载荷和终端 I/O，需评估图像分辨率/压缩质量取舍，再实现可控方案并在 Warp/Kitty 实机 A/B。Copy instrumentation 留作后续 profile 工具。

### Kitty direct RGB 对照

普通视频的 Kitty direct 路径对不透明帧使用 swscale 输出 RGB24，再按 Kitty 协议分块发送；同尺寸像素载荷比 RGBA32 少 25%。透明帧、非默认 gamma、颜色增强、tmux/screen 或转换失败时保留 Chafa 路径。共享内存、静态图片和 GIF 不受此改动影响。RGB 路径使用双线性缩放，与 Chafa 的缩放结果并不保证逐像素一致。

为了不丢失透明度，每个被消费的帧先做一次 O(width×height) alpha 扫描，再缩放／转色。该扫描计入 `worker-render-time`，也是 RGB 渲染耗时上升的组成部分；不是零成本，也没有单独测出它占总耗时的比例。当前每个 decoded frame 只由一个 worker 消费，简单缓存扫描结果不会减少扫描次数；基于源格式的透明度元数据优化留待独立测量。

图像 ID 由终端分配：主上下文在启动 workers 前，经现有 `terminal_probe_query_response_tracked()` 发送 1×1 透明占位图像，使用 `I=` 请求新 ID，校验终端返回的 `i=`、请求编号及 `OK` 后才启用 RGB 优化。非回复按键由现有 pending-input 队列回放；主上下文暂停读取期间没有其他输入线程，workers 不读取 stdin。请求期限本地 120ms、SSH 500ms；超时、错误或无效回复会回退 Chafa，不猜测图像 ID。`I=` 的随机数只用于关联回复，不是终端资源 ID。选用共享内存时跳过 RGB24 ID 分配，单帧共享内存失败回退 Chafa。

终端恢复失败时先立即重试两次；即使重试成功，该次探测仍返回失败，完整有效回复只用于清理已确认 ID。仍失败则保留 termios，后续探测和输入初始化先尝试恢复；主循环检查恢复仍失败时有意停止会话、进入清理并报告错误，而非继续操作不可靠的输入。探测函数不直接打印到 TUI。外层输入恢复无论成功或失败都以用户的原始设置替换探测 raw 快照，失败时保留原始设置供退出重试，且不把 raw_mode_enabled 标成已关闭。永久设备故障仍无法保证恢复。

已知 ID 的删除只发送一次。写入或 flush 失败时，终端可能已执行删除并复用 ID，因此先丢弃 stdout 中尚未发出的缓冲（Linux 使用 __fpurge、macOS 使用 fpurge），再仅排队恢复 ST 帧界，不重放破坏性删除。仅替换命令字符串或 clearerr 不会清掉 stdio 中的旧字节。排队删除遇到不确定送达时同样处理。停止仍 join workers 并报告输出错误。该安全取舍可能留下图像直到终端回收／重置，但避免误删其他客户端复用的 ID。

首次播放、恢复及播放中的 seek 共用准备／启动／首次输出／调度入口，明确区分正常 EOF／暂时无帧与真正的输出错误；失败不继续调度，并返回 `ERROR_OUTPUT_IO`。暂停 seek 同样传递输出错误并保留目标位置。单图视频分支传递该错误，不将失败标成 redraw 完成；底层统一报告输出故障，因此计时器、键盘、缩放、resize、协议切换等没有错误返回通道的入口也不会静默失败。每次帧输出在任何光标／文本输出前发送 ST；失败后另将 ST 放入主上下文恢复队列，避免半条 APC 吞掉后续 UI 输出。后台清理失败后至少间隔 100ms 再试；显式分配／退出仍立即重试。

暂停保留已分配的 ID，恢复复用；播放中回退 Chafa 时用 `d=i` 隐藏该 placement 但保留 ID 预留，避免 worker 队列仍引用已释放资源。停止、销毁、离开 Kitty 或替换为非 Kitty renderer 时，在 workers 停止后用 `d=I` 释放；删除送达不确定时不重放删除。每次删除先发送 ST 结束可能残留的 APC，再清除历史 stdio 错误标志并检查完整写入／flush。分配请求如已发送、但回复超时／格式无效／终端状态恢复失败，仅排队发送 ST 恢复协议帧界，不再按共享的 `I=` 编号发送 `d=N`：该编号不跨客户端唯一，重试期间其他应用可能创建相同编号的新图像。未知 ID 的不可见 1×1 占位图可能留到终端配额回收或重置；这是避免误删其他应用图像的安全取舍。默认 `q=0` 允许成功回复；`q=1`／`q=2` 分别抑制成功／所有回复，分配阶段不能使用它们。

`play-start` 在启动 workers 和首次渲染之前记录；输出失败会终止播放并 join workers、清空队列，不再仅删除 tick 定时器。失败后显式恢复播放会创建新 workers；重播入口在重置时钟、代数和分配 ID 之前先 join 旧线程，包括动态布局异常留下的线程句柄。相关测试不再在恢复前手动 `clearerr(stdout)`，并检查捕获的针对性删除命令，而不仅检查布尔标志。

同一 4K H.264 素材，同分辨率下交替运行 RGBA 基线和 RGB 版本，各三轮，每轮采样 3 秒，诊断日志开启。每种窗口内两版使用相同的载荷像素尺寸：

| PTY 尺寸 | 载荷尺寸 | 提交帧/秒（RGBA → RGB） | 输出耗时 P50 | 提交间隔 P95 |
| --- | --- | --- | --- | --- |
| 80×24 | 512×288 | 31.19 → 30.84 | 6.13 → 4.61 ms | 42.52 → 42.17 ms |
| 160×48 | 1200×672 | 19.40 → 27.01 | 31.96 → 22.63 ms | 72.51 → 55.51 ms |

表中为各轮指标的中位数。小窗口（80×24）未见吞吐收益，提交率从 31.19 略降至 30.84，仅输出 P50 明显下降。大窗口的渲染耗时 P50 从 15.33 增至 19.09 ms，小窗口从 5.99 增至 11.30 ms；收益来自减少终端写入，而非加快渲染。伪终端只接收字节，不负责解码或绘制。提交率超过素材标称帧率不代表画面显示得更快；真实 Warp/Kitty 的显示和画质仍待实机验证。

Review 后补充：direct RGB 按统一比例缩放两个轴，像素负载始终不超过 8 MiB，即使没有终端像素尺寸也适用；constructor 再次检查上限。紧密排列的 RGB 缓冲直接 base64 编码，只有含行填充时才打包。每个播放器复用已验证分配的 Kitty 图像 ID，`a=T` 替换同 ID 的图像数据并重新显示；未提交帧不触发删除，终端资源生命周期见上文。显式 SHM 模式失败仍沿用旧 Chafa 回退，不进入 RGB 优化。仅一个提交区间时保留区间统计，但 FPS 输出 null；两个以上区间才计算提交率。

上表是这些 review 修正前的实验记录：统一比例缩放会改变部分尺寸的舍入结果，不能把原数字当作新版本的复测结论。`tsan-suite` 当前有 21 个路径。早先只统计行首 `ok`，会漏掉带终端控制输出前缀的成功记录；现在用 TAP 计划和全部 `ok N /path` 记录交叉验证。

独立 FFmpeg 微基准中，前 60 帧从单线程的 1.54 秒降到 4 线程的 0.51 秒（各三轮中位数），两组 `framemd5` 一致，但播放器直传对照没有稳定提升，未保留线程改动。512×288 RGBA 帧使用 zlib level 1 可将像素载荷压到约 38%，但编码约 4.77 ms/帧，且未验证 Warp 压缩支持，因此未启用。

最新 review 修正验证：正常、ASan、UBSan 套件各有 479 条 C TAP 成功记录（计划合计亦为 479），Python 摘要测试 10 项通过；TSan 针对性套件 21 项通过。覆盖 RGB 分块、行填充、超限拒绝、等比缩放、未知边界内存上限、图像生命周期，以及实际渲染工作线程的 direct/auto/显式 SHM 回退；只读输出 FD 的失败注入、无手工清除错误的恢复重试、暂停时非 Kitty renderer 替换、协议切换代数失效、实际 worker 重启、分配回复／异常回复／按键回放和实际解码—呈现—暂停—恢复—停止链路。真实终端分配与视觉／显示 FPS 仍需实机验证。

## 上游依据

- [GLib timeout 限制](https://docs.gtk.org/glib/func.timeout_add.html)、[绝对 ready time](https://docs.gtk.org/glib/method.Source.set_ready_time.html)
- [Kitty 图形协议：共享内存、动画、局部帧与 usage hints](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
- [Apple VideoToolbox](https://developer.apple.com/documentation/videotoolbox)
- [FFmpeg 发布记录](https://ffmpeg.org/index.html)

文档由 AI 辅助整理，源码事实与性能推断分开。依赖版本和终端子能力以实际运行环境为准。
