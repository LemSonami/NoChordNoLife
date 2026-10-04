# Project workflow

## GUI build verification

- After launching `NoChordNoLife.exe` for a visual or MIDI test, close it and confirm that no `NoChordNoLife` process remains before relinking the same executable path.
- Link the Win32 GUI with `gdiplus`, `comdlg32`, and `winmm`; MIDI preview depends on the Windows multimedia library.

## 语言约定

- 对用户的说明，以及新增或修改的界面提示，使用中文。
- 资源路径、API 标识、文件格式标记和算法使用的调式名称保持原值，不为翻译而改动程序协议或资源引用。
