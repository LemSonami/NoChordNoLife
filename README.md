# NoChordNoLife
[术力口音乐创作革新·NCNL] NoChordNoLife/弦歌成囚

以下为临时README内容

## MIDI 节奏与分块

- 将 `.mid` / `.midi` 拖到钢琴卷帘。支持标准 Type 0/1、PPQN 计时；跨轨道的重叠音符只保留各时间段最低音，保留休止、力度和时值。播放速度由代码中的 `BPM`（默认 120）决定，不采用文件的变速信息。
- 导入后所有节奏音符默认属于同一个和弦分块。在相邻节奏音符之间按鼠标中键切分，再次按中键合并；按住中键移动可连续添加分块，一次拖动中同一间隙只处理一次，快速跨过多个间隙也不会漏掉。箭头表示分块边界，合并采用左侧和弦设置。
- 按住右键经过箭头可擦除该分块边界，箭头以碎光粒子消散，将左右块合并；节奏保留，采用左块的内音与情感设置。
- 各分块共用一套和弦内音和情感预设，分块数量不再固定为四。生成会按原有节奏重复各和弦声部，卷帘、播放和导出使用同一份音符数据。
- 双击卷帘上方编辑内音；拖动音符调整音高；按住右键经过音符仅删除该次音符，经过标题清空整个分块，经过下方色彩框清空预设。
- 点击左侧琴键试听对应音高（按住可持续试听、滑动可换音）。左键点击卷帘空白音高行添加音符：已有节奏位置沿用该次时长，休止位置新增十六分音符并裁到邻近音符之前；上方内音自动同步。各色彩块在对应分块的左右边界之间居中，宽度随分块时长调整。
- `Enter` 生成，`Space` 循环播放/停止（钢琴音色、播放线及粒子效果）。只有被清空的分块重新生成；全部分块都有内音时，重新生成全部分块。
- 主界面获得焦点时，`Ctrl+Z` 撤销音符增删、音高拖动、和弦内音编辑、情感预设及分块修改，也支持撤销生成和 MIDI 导入。保留最近 20 步，一次音符拖动只算一步；正在键入内音时使用文本框自身的撤销。
- 每次从满足用户限制且进行质量严格大于 65、小于 90 的采样结果中随机选取，包含尾到头的循环评分；本次采样找不到合格结果时提示调整条件，不返回越界结果。
- 生成优先采用调式内、至少三个音的和弦：候选每含一个离调音，相对抽样权重乘以 0.15；二音结构再乘以 0.18。合格结果也按这个偏好随机选择，不追求最高质量分；情感限制使理想候选不足时仍可选其他结构。用户指定内音不受此偏好影响。两个基础评分算法保持不变。
- 左键长按卷帘上方任意和弦内音框约 350 毫秒，然后拖到桌面或文件夹并松开，即可导出整个钢琴卷帘的 MIDI（全部分块）；保留所有当前声部、原始时间位置、节奏、休止、力度和完整循环长度，不裁切为单个和弦。即使该内音框为空，只要卷帘其他位置有音符，也能拖出整段 MIDI。双击仍编辑内音，`Esc` 取消拖出。主界面不再显示导入/导出按钮或底部操作说明。

文件拖出采用 [Windows OLE 文件拖放](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-dodragdrop)，只允许复制，不允许移动；取消不会修改卷帘或留下临时文件。成功拖出后源文件保留在系统临时目录，以兼容延迟读取文件的目标应用；目标位置的 MIDI 是独立副本。

下方情感标签依次为「I 黯然」「II 伤心」「III 暧昧」「IV 希望」「V 光明」，对应原有五个色彩区间。标签使用更大的字号，较密集的分块仍自动缩小以避免重叠。

播放中的音符带有柔光高亮、向四周扩散的光粒和星芒；删除音符或清空分块时，音符以约 0.76 秒的径向碎光消散动画淡出。音符数据即时删除，动画不会延迟 MIDI 更新。动画层复用缓存缓冲，并限制同时绘制的粒子数量。

动画时钟目标为 120 FPS，采用高精度定时、待处理帧合并和局部缓冲拷贝。静止、最小化及拖拽缩放期间暂停高频刷新，避免持续占用资源。实际呈现帧率取决于设备、窗口大小和粒子负载，不保证始终 120 FPS。

黑键音名以白字显示在黑键上，白键音名位置不变。琴键试听时高亮，松开或失去焦点后恢复。试听与进行播放共用一个 MIDI 设备句柄、使用独立通道，避免 Windows MIDI 设备被重复打开而报“已占用”。

界面字体从 `assets/res/font.ttf` 私有加载，不需要安装到系统。左侧键盘先绘制连续白键，再将黑键叠在两颗白键之间；点击判定与显示形状一致。卷帘不绘制横纵网格细线，仅保留音高行底色及播放指示线。

顶部情感按钮使用渐变背景和圆角图标。启动时从 `assets/chord_emotion` 加载 `1.gif` 至 `5.gif`，同名 GIF 优先于 PNG；缺少或无法读取 GIF 时回退到对应 PNG。图标以中心正方形裁切，保持 1:1，不拉伸；顶部与下方一样铺满整个色彩块，不再留内缩边距。GIF 按文件帧时长循环播放，顶部按钮和已应用的下方色彩块同步动画；最小化及拖拽缩放期间暂停刷新，恢复后继续。GIF 帧在启动时预解码、预裁切成 160×160 的透明缓存；换帧只更新独立情感图层，不重建钢琴卷帘或背景缓存。按钮采用离屏绘制，避免换帧闪烁。帧时长的读取参考 [GDI+ 属性说明](https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-constant-property-item-descriptions#propertytagframedelay)。

## 配置与调式圆盘

配置窗口与预设窗口一样采用开关淡入淡出，切换选项卡时内容渐显；转盘松开后平滑吸附。过渡结束后停止刷新，缩放过程中使用缓冲画面，松开后重新绘制。

配置动画共用高精度、合并待处理帧的 120 FPS 目标时钟。静态页面与音名字形缓存复用，窗口透明度变化不重复重绘静态页面，圆盘拖动仅在帧刷新时绘制最新位置。点击七种调式立即更新高亮与音阶，不触发淡入淡出；窗口开关、选项卡切换和圆盘吸附动画保留。实际屏幕帧率仍取决于设备和窗口尺寸。

「调式选择」中的圆盘按五度圈排列十二音，并占据界面上半部。点击音区、拖动圆盘或使用滚轮，将主音旋转到顶部；下方选择 Ionian、Dorian、Phrygian、Lydian、Mixolydian、Aeolian 或 Locrian，圆盘高亮对应七个音（黑色音名），中心仅显示如 `C Ionian` 的当前调式。

配置只保留「调式选择」和「和弦行进评价雷达图」两页，调式选择排在前面，默认打开该页；不再显示外观设置，背景统一从 `assets/skins` 读取。

点击「决定了，就是你！」或按 `Enter` 后关闭配置窗口，更新主界面的调式（如 `D Dorian`），后续生成使用新调式，已有和弦的情感及进行质量重新评估。已有 MIDI 音符不会自动移调或清除；直接关闭窗口则取消未确认的调式预览。配置与预设窗口默认在主应用窗口中心打开；必要时限制位置，避免超出当前显示器工作区。

## 节奏型预设

点击圆角长方形「预设」按钮打开独立的预设管理界面。输入名称后点击「保存当前节奏」，将卷帘中当前音符和节奏保存为 `presents/名称.mid`；请先生成、导入或添加音符。保存同名文件不会覆盖已有预设。

双击条目的图标或空白区域加载；双击名称或选中后按 `F2` 就地重命名，`Enter` 确认、`Esc` 取消。下方名称输入框仅用于保存新预设。加载使用 MIDI 节奏导入规则，仅保留重叠音符中的最低音。预设是标准 MIDI，不保存情感限制或分块设置。可直接将已有 `.mid` / `.midi` 放入 `presents`，再点击刷新。

预设窗口开关采用淡入淡出，列表变化轻微滑入；过渡完成后停止动画刷新。副标题为「不放过一丝灵感，不留下一丝遗憾。」；空列表提示居中。预设计数与底部两行说明已移除，错误反馈仅在预览区域显示。

拖动列表项可上下排序（竖直调整光标），松开后保存顺序到 `presents/order.txt`。按住右键经过列表项删除预设（不可用光标），删除使用 Windows 回收站；同一行停留不会连续误删补上来的项目。名称输入使用文本光标。管理窗口支持等比例缩放，缩放结束后调整 UI。

「预设名称」标签及名称输入文字使用更大的字号，输入文字按实际字体高度垂直居中。名称输入和就地重命名支持 `Ctrl+A` 全选。预设窗口获得焦点时，`Ctrl+Z` 恢复最近删除的节奏型 MIDI 及原列表位置，保留最近 20 次删除；本次运行中关闭并重新打开预设窗口后仍可撤销。同名文件已存在时提示冲突，不覆盖文件，也不丢弃撤销记录。输入框中 `Ctrl+Z` 优先撤销文本编辑，没有文本撤销记录时才恢复删除的预设。退出整个应用后删除撤销记录释放，仍可从 Windows 回收站恢复文件。

## 像素风背景皮肤

背景采用内置 imagegen 生成的原创温馨像素风，参考田园经营与方块沙盒游戏的氛围，没有使用现成游戏贴图。生成提示词完整记录在 `assets/skins/prompts.json`。

| 界面 | 背景文件 | 主题 |
| --- | --- | --- |
| 主界面 | `assets/skins/main.png` | 草地晨光 |
| 调式选择 | `assets/skins/mode.png` | 林间池塘 |
| 和弦行进评价雷达图 | `assets/skins/radar.png` | 森林空地 |
| 节奏型预设 | `assets/skins/preset.png` | 星夜田野 |

旧示例 `bg.png` 已删除，不再作为备用，也不会由程序重新迁移或生成。背景文件名不再带 `_pixel` 后缀。

主界面保持 40% 背景不透明度；配置页采用 65%，预设页采用 70%，配合深色半透明区域保证文字可读。图片居中裁切、保持比例，使用最近邻采样保留像素边缘；解码后释放文件句柄，窗口大小和透明度对应的图层缓存复用。图片缺失时安全回退到渐变底色，不影响 MIDI 功能。

雷达图仍使用 `assets/res/penta_dim.png` 作为图表底图，绘制在森林窗口背景上；原有已校准顶点、鼠标拖动区域、权重映射及计算逻辑均保持不变。窗口缩放仍在松开后更新，播放、GIF 和淡入淡出继续复用既有动画缓存。

文字按钮采用 `assets/skins/buttons` 中的原创像素风 PNG，文字已烘焙在图片里，不再叠绘一层系统字体。包含主界面的 `generate.png`、`settings.png`、`presets.png`，配置页的 `mode_tab.png`、`radar_tab.png`、`confirm.png` 和 `mode_*.png` 七种调式，以及预设页的 `refresh.png`、`save.png`。圆角外部为真正透明区域；解码后释放源文件，按显示尺寸缓存缩放贴图，按下和选中反馈无需重新解码。缺图时回退到原有文字按钮。情感按钮继续使用原有动态 GIF（优先于 PNG）。完整生成提示词同样记录在 `assets/skins/prompts.json`。

全部 15 张正式 PNG 按 alpha≠0 的可见像素紧边界验证，透明画布留白不计入比例，与原按钮比例精确一致（误差 0）。经用户授权，用独立 C++ 工具校正背景与圆角边框；文字和叶片/花瓣装饰保持原始比例，不进行横纵独立拉伸，原始生成素材保存在 `assets/skins/buttons/source`。例如「生成」的可见区域为 1800×780，对应原按钮的 120×52。按钮布局尺寸保持不变：主界面生成/配置为 120×52、预设为 140×60；配置页调式为 162×40、底部选项卡为 360×55、确认为 340×40；预设页刷新为 120×42、保存为 276×50。渲染仍保留等比缩放保护，未来替换比例错误的素材也不会压扁文字。`prompts.json` 记录原始输入、校正方式与最终实测比例。

## 鼠标光标与特效

应用光标仅从 `assets/cursor` 加载：默认 `Link.ani`，竖直缩放/音高拖动 `Vertical Resize.ani`，按住右键 `Unavailable.ani`，文本输入区域 `Text Select.ani`。这些文件已从「Miku 完整版」复制，运行不再依赖原文件夹；不修改系统全局光标。缺少素材时回退到系统光标。

每次左键点击产生扩散光粒；按住左/中/右键移动分别产生浅绿/淡灰/浅红拖尾与沿轨迹散开的粒子。主界面和配置界面均支持，覆盖原生按钮、编辑框。特效使用独立、[鼠标穿透的透明图层](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)，复用小范围像素缓冲，不触发整张钢琴卷帘重绘。拖尾和粒子有数量与生命周期上限，空闲后停止动画时钟；最小化、窗口缩放或应用失去焦点时清理。

## Windows 构建

C++ 源码统一放在 `src`，按功能划分：

```text
src/
├── algorithms/   和弦情感与进行评分算法
├── generation/   和弦进行生成
├── config/       进行权重配置
├── midi/         MIDI 节奏及卷帘音符数据辅助
├── ui/           Win32 界面、鼠标特效及预设管理
├── tools/        离线按钮素材比例校正工具（不参与运行时计算）
└── tests/        C++ 回归测试源码
```

在项目根目录运行构建命令；可执行文件仍放在根目录，`assets`、`presents` 和 `config.json` 的运行时位置不变。

```powershell
$sources = @(
    "src/algorithms/chord_progression.cpp", "src/algorithms/chord_emotion.cpp",
    "src/generation/chord_generator.cpp", "src/config/progression_config.cpp",
    "src/midi/midi_rhythm.cpp", "src/ui/mouse_feedback.cpp",
    "src/ui/preset_library.cpp", "src/ui/midi_file_drag.cpp", "src/ui/chord_gui.cpp"
)
g++ -std=c++11 -O2 -mwindows @sources -o NoChordNoLife.exe -static-libgcc -static-libstdc++ -lgdiplus -lcomdlg32 -lwinmm -lshell32 -lole32 -luuid
```

测试源码位于 `src/tests`，测试生成物仍可输出到根目录下的 `tests`。例如：

```powershell
g++ -std=c++11 -O2 src/tests/preset_library_test.cpp src/midi/midi_rhythm.cpp src/ui/mouse_feedback.cpp -o tests/preset_library_test.exe -static-libgcc -static-libstdc++ -lgdiplus -lshell32 -lgdi32 -lwinmm
./tests/preset_library_test.exe

# 以下测试已包含界面实现，不要重复编译 src/ui/chord_gui.cpp。
$coreSources = $sources | Where-Object { $_ -ne "src/ui/chord_gui.cpp" }
g++ -std=c++11 -O2 src/tests/undo_buttons_test.cpp @coreSources -o tests/undo_buttons_test.exe -static-libgcc -static-libstdc++ -lgdiplus -lcomdlg32 -lwinmm -lshell32 -lgdi32 -lole32 -luuid
./tests/undo_buttons_test.exe

# 文件拖出协议及标题框交互测试；只复制到测试专属文件夹，不改动桌面文件。
g++ -std=c++11 -O2 src/tests/midi_file_drag_test.cpp @coreSources -o tests/midi_file_drag_test.exe -static-libgcc -static-libstdc++ -lgdiplus -lcomdlg32 -lwinmm -lshell32 -lgdi32 -lole32 -luuid
./tests/midi_file_drag_test.exe

# 像素皮肤加载、文件句柄释放、裁切、不透明度、缓存及界面预览。
g++ -std=c++11 -O2 src/tests/pixel_skin_test.cpp @coreSources -o tests/pixel_skin_test.exe -static-libgcc -static-libstdc++ -lgdiplus -lcomdlg32 -lwinmm -lshell32 -lgdi32 -lole32 -luuid
./tests/pixel_skin_test.exe

# 带文字按钮的透明边角、完整显示、文件句柄释放、按下/选中与缩放缓存。
g++ -std=c++11 -O2 src/tests/button_artwork_test.cpp -o tests/button_artwork_test.exe -static-libgcc -static-libstdc++ -lgdiplus -lgdi32
./tests/button_artwork_test.exe

# 严格验证正式 PNG 的非透明区域比例，不使用容差或 alpha 阈值。
./tests/button_artwork_test.exe --verify-aspect

# 从保留的原始素材复现校正；输出必须使用独立目录。
g++ -std=c++11 -O2 src/tools/button_asset_fit.cpp -o tests/button_asset_fit.exe -static-libgcc -static-libstdc++ -lgdiplus -lgdi32
New-Item -ItemType Directory -Force tests/button_asset_fit/output | Out-Null
./tests/button_asset_fit.exe assets/skins/buttons/source tests/button_asset_fit/output
./tests/button_artwork_test.exe --verify-aspect tests/button_asset_fit/output
```

重新构建前关闭正在运行的应用。两个评分算法保持不变，节奏和动态分块在独立模块与前端中实现
