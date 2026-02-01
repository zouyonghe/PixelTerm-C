# PixelTerm-C Refactor Plan

> 目标：先文档化重构方向，随后按阶段逐步实现，保持行为不变、可回退、每步可测试。

## 背景与现状
- 主要复杂度集中在 `src/app.c`（约 6k+ 行），承担 UI 渲染、文件管理、书籍预览、文本工具、预加载调度等多职责。
- `PixelTermApp` 结构体过大，单体状态难以维护与复用。
- 存在重复逻辑：
  - 预览网格与书籍预览渲染骨架高度相似。
  - UTF-8 文本/路径裁剪与 terminal 安全输出工具函数分散。
  - 媒体类型判断在 `app.c` 与 `main.c` 多处重复。
  - 预加载器生命周期管理逻辑分散且重复。

## 总体目标
1. 降低 `app.c` 体积与职责，拆分模块并明确边界。
2. 收敛重复逻辑（文本工具、媒体类型判断、预加载器控制、网格渲染框架）。
3. 可测试、可回滚：每一步的改动都保持行为不变并能独立验证。

## 非目标
- 不改变任何用户可见行为/快捷键。
- 不做 UI 视觉/布局重设计。
- 不引入新的第三方依赖。

## 重点问题（代码味道）
- `src/app.c` 过大且包含大量 static helper 与 UI 逻辑。
- `PixelTermApp` 结构体职责混杂（渲染/目录/预览/书籍/输入/异步）。
- 逻辑重复：
  - 预览网格 vs 书籍预览。
  - UTF-8 处理与安全打印。
  - 媒体类型判断。
  - 预加载器生命周期与队列管理。

## 拟定模块拆分
> 仅拆分逻辑，不改变外部行为。

1. 文本/路径工具
   - 新文件：`src/text_utils.c`, `include/text_utils.h`
   - 迁移：
     - `sanitize_for_terminal`
     - `utf8_display_width`
     - `truncate_utf8_for_display`
     - `truncate_utf8_middle_keep_suffix`
     - `utf8_prefix_by_width` / `utf8_suffix_by_width`
   - 目标：集中处理 UTF-8 与终端安全输出。

2. 媒体类型判断统一
   - 新文件：`src/media_utils.c`, `include/media_utils.h`
   - 新接口：
     - `MediaKind media_classify(const char *path)`
     - `bool media_is_image(MediaKind)` / `media_is_video(MediaKind)` / `media_is_animated_image(MediaKind)`
   - 替换：
     - `app_current_is_video` 与 `app_render_current_image` 中的判定逻辑

3. 预加载器生命周期封装
   - 新文件：`src/preload_control.c`, `include/preload_control.h`
   - 目标函数（示例）：
     - `app_preloader_start(PixelTermApp*)`
     - `app_preloader_stop(PixelTermApp*)`
     - `app_preloader_reset(PixelTermApp*)`
     - `app_preloader_queue_directory(PixelTermApp*)`
   - 收敛：`app_load_directory`、`app_toggle_preload` 等分散逻辑。

4. 预览网格渲染骨架抽取
   - 新文件：`src/grid_render.c`, `include/grid_render.h`
   - 抽象：
     - 通用布局与绘制流程（header / page indicator / cell loop / selection border）
     - 通过回调注入单元渲染（图片、视频、书页）
   - 调用端：
     - `app_render_preview_grid`
     - `app_render_book_preview`

5. 输入分发拆分（后置）
   - `src/input_handlers/*.c` 或按模式拆分
   - 目标：减少 `main.c` 巨型分发逻辑，建立 `mode -> handler` 映射表

6. `PixelTermApp` 状态拆分（后置，风险较高）
   - 结构体拆成子结构：
     - `PreviewState`, `FileManagerState`, `BookState`, `InputState`, `AsyncState`
   - 保留原字段名兼容阶段（或通过宏映射过渡）。

## 分阶段实施计划

### Phase 1: 文本与媒体工具收敛（低风险）
- 新增 `text_utils` 模块并迁移 UTF-8 / sanitize 函数。
- 新增 `media_utils`，统一媒体类型判断。
- 替换调用点，确保行为一致。

### Phase 2: 预加载器生命周期统一（中风险）
- 新增 `preload_control`，集中创建/启停/队列逻辑。
- 替换 `app_load_directory`、`app_toggle_preload`、刷新/切换时的重复逻辑。

### Phase 3: 预览/书籍渲染骨架统一（中风险）
- 新增 `grid_render` 框架。
- 抽取共用流程，保留回调实现细节。
- 让 `app_render_preview_grid` / `app_render_book_preview` 变薄。

### Phase 4: 输入分发拆分（中风险）
- 将 `main.c` 中按模式的 `handle_key_press_*` / `handle_mouse_*` 迁移到独立模块。
- 引入 `InputHandler` 表驱动分发。

### Phase 5: App 状态拆分（高风险）
- 渐进式拆分 `PixelTermApp` 字段到子结构。
- 只要不影响 ABI，优先通过内部字段访问器过渡。

## 影响范围预估
- `src/app.c`：核心拆分目标，预计减少 30%+ 行数。
- `src/main.c`：输入分发整理后体积明显下降。
- 新增文件：`text_utils.c/h`, `media_utils.c/h`, `preload_control.c/h`, `grid_render.c/h`。

## 风险与对策
- 行为回归风险：每阶段只做结构调整，不改逻辑。
- 模块依赖失控：新增模块只依赖 `common.h`/`app.h` 等基础头文件。
- 重构打断节奏：每阶段独立提交，保留可回退点。

## 验证策略
- Phase 1/2：编译 + 启动基本流程（单图/目录/预览/书籍）。
- Phase 3：对比渲染输出（预览/书籍）边界行为。
- Phase 4：回归所有输入路径（键盘/鼠标/滚轮/双击）。
- Phase 5：回归全功能 + 压测（快速切换/预加载）。

## 进入实施的前置条件
- 确认接受此重构顺序与模块拆分命名。
- 确认是否需要保持 C API/ABI 稳定（如作为库使用）。
