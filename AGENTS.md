# Project workflow

## GUI build verification

- After launching `NoChordNoLife.exe` for a visual or MIDI test, close it and confirm that no `NoChordNoLife` process remains before relinking the same executable path.
- Link the Win32 GUI with `gdiplus`, `comdlg32`, and `winmm`; MIDI preview depends on the Windows multimedia library.
