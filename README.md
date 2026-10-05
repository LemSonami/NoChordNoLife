# NoChordNoLife

**弦歌成囚 · NCNL**，一个根据调式、情感色彩和节奏生成和弦进行的 Windows 应用。

可以导入 MIDI 节奏、编辑音符、试听和弦，并将结果拖出为 MIDI 文件。当前是独立应用，尚不是 VST 插件。

## 开始使用

运行 `NoChordNoLife.exe` 即可，无需单独安装字体或复制材质。

应用会在 EXE 所在目录自动创建：

- `config.json`：和弦行进评价参数。
- `presents/`：节奏型预设。
- `plugins/`：可选插件。

请将应用放在可写入的文件夹中。已有配置和文件不会被重置。

## 常用操作

1. 在「配置 → 调式选择」中选主音和调式，点击「决定了，就是你！」或按 `Enter` 应用。
2. 将 `.mid` 或 `.midi` 文件拖到钢琴卷帘，导入节奏。重叠音符只保留最低音。
3. 在音符之间按中键分块，让不同段落使用不同和弦。再次按中键可合并；按住中键移动可连续分块。
4. 将顶部情感按钮拖到对应分块，或双击卷帘上方的内音框输入和弦，例如 `C E G`。
5. 按 `Enter` 生成，按空格循环播放或停止。默认速度为 120 BPM。
6. 左键长按任意内音框，再拖到桌面或文件夹，导出**整段 MIDI**。

情感预设从暗到亮依次为：I 黯然、II 伤心、III 暧昧、IV 希望、V 光明。

生成优先选择调式内、至少三个音的和弦，并考虑最后一个和弦回到第一个和弦的衔接。只重新生成已清空的分块；如果所有分块都有内音，则全部重新生成。指定的内音不必符合情感预设。找不到符合条件的进行时，会提示调整条件。

### 编辑与快捷键

- 点击左侧琴键：试听。
- 左键点击卷帘空白处：添加音符；上下拖动音符：修改音高。
- 按住右键经过音符、内音框、情感块或箭头：清除对应内容。清除箭头会合并左右分块，并保留左侧设置。
- `Ctrl+Z`：撤销，最多保留 20 步。

## 节奏型预设

点击「预设」打开管理窗口：

- 输入名称，点击「保存当前节奏」保存为 MIDI。
- 双击预览区域加载；双击名称或选中后按 `F2` 重命名。
- 拖动条目调整顺序；按住右键经过条目删除。
- 在预设窗口按 `Ctrl+Z` 可撤销删除，退出应用后也可从 Windows 回收站恢复。

预设只保存 MIDI，不保存情感限制或分块设置。也可以把已有 MIDI 放入 `presents/`，再刷新列表。

## 配置与插件

配置包含三个页面：

- **调式选择**：旋转圆盘选择主音，再选择七种调式之一。
- **和弦行进评价雷达图**：拖动各维度的点调整权重，总和始终为 1。
- **插件管理**：刷新、启用或禁用插件。

安装插件：将插件文件夹放入 `plugins/`，在插件管理中刷新并启用，再点击主界面底部按钮切换。卸载前先禁用插件。

内置的 NCNL 不能禁用，同时最多启用四个模块（含 NCNL）。这套机制只加载遵循 NCNL 接口的扩展，不加载任意 VST。插件 DLL 可以执行代码，请只启用可信插件。

## 开发者指南

### 编译应用

需要 MinGW 的 `g++` 和 `windres`。先关闭应用，再在项目根目录运行：

```powershell
./src/tools/build.ps1
```

如果编译器没有加入 PATH，可指定路径：

```powershell
./src/tools/build.ps1 -Compiler "你的路径/g++.exe" -ResourceCompiler "你的路径/windres.exe"
```

生成的 `NoChordNoLife.exe` 已包含图片、GIF、动画光标和字体，发布时无需附带 `assets/`。修改材质后需要重新编译。

应用图标源图为 `assets/icon.png`，构建时自动生成多尺寸的 `assets/icon.ico`，用于程序文件、窗口和任务栏。

源码位于 `src/`：`algorithms` 是评分算法，`generation` 是生成逻辑，`midi` 处理 MIDI，`ui` 是界面，`config` 是参数配置，`plugins` 是插件系统，`resources` 与 `tools` 负责资源和构建。测试源码用于开发验证，不参与正常应用构建。

### 开发插件

最简单的方式是复制 `src/plugins/examples/rhythm_inspector/` 示例并修改。一个插件由一个文件夹、一份 UTF-8 清单和一个 DLL 组成：

```text
plugins/你的插件/
├── plugin.ini
├── plugin.dll
└── button.png       可选的按钮图片
```

清单示例：

```ini
[plugin]
abi=1
id=rhythm_inspector
name=节奏观察
version=1.0.0
description=查看当前调式、速度与音符信息
entry=plugin.dll
button=button.png
```

`id` 必须唯一，最多 48 字符，只能使用小写英文字母、数字、下划线和连字符；`ncnl` 保留给本体。`entry` 和 `button` 只能填写文件名。按钮图片保持等比例显示，建议实际可见区域比例接近 216:46。

接口定义在 `src/plugins/plugin_api.h`，当前版本为 **NCNL Plugin ABI 1**：

1. 定义 `NCNL_BUILD_PLUGIN` 并包含接口头文件。
2. 导出 `ncnl_get_plugin`，检查 ABI，填写 `NcnlPluginV1`。
3. 实现页面创建、销毁和缩放；页面激活回调可选。
4. 编译 DLL，连同清单放入插件目录。

页面必须是宿主提供的父窗口的直接 `WS_CHILD` 子窗口。销毁时清理窗口、定时器和资源；切换页面会销毁插件页面，需要保留的设置由插件自行保存。

宿主提供 `get_context` 读取调式、BPM 和音符数量，提供 `read_midi` 读取完整 MIDI。调用前设置上下文结构的 `size`；读取 MIDI 时先用空缓冲查询长度，再分配空间读取。接口只在界面线程使用，不跨 DLL 传递 STL 对象，也不要向宿主抛出异常。

插件与应用的 32/64 位必须一致，当前构建使用 32 位 MinGW。示例 DLL 的编译命令：

```powershell
g++ -std=c++11 -O2 -shared src/plugins/examples/rhythm_inspector/plugin.cpp -o plugin.dll -static-libgcc -static-libstdc++ -lgdi32
```

当前插件接口只提供 Windows 页面扩展和只读数据，不提供音频处理或修改卷帘功能。后续移植到 VST 时，还需要适配 DAW、音频接口和状态保存。
