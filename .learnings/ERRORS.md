# Errors

Command failures and integration errors.

---

## [ERR-20260930-006] legacy-windows-sdk-dpi

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: config

### Summary
The legacy MinGW Windows headers do not declare `SetProcessDPIAware`.

### Error
```
SetProcessDPIAware was not declared in this scope
```

### Context
- DPI awareness is optional and does not affect either algorithm.

### Suggested Fix
Resolve the API dynamically from user32 so the executable remains compatible with old SDK headers and operating systems.

### Metadata
- Reproducible: yes
- Related Files: chord_gui.cpp

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Added optional runtime API resolution.

---

## [ERR-20260930-005] legacy-mingw-cpp17

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: config

### Summary
The installed MinGW accepts the C++17 flag but lacks several C++17 language/library facilities.

### Error
```
structured bindings rejected; std::clamp missing; string::data() returned const pointers
```

### Context
- The failures were compiler compatibility issues, not algorithm errors.
- Win32 headers also required an explicit Windows target version for DPI declarations.

### Suggested Fix
Use C++11-compatible pair access, min/max clamping, mutable buffer access, and an explicit `_WIN32_WINNT` value.

### Metadata
- Reproducible: yes
- Related Files: chord_progression.cpp, chord_emotion.cpp, chord_gui.cpp

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Rewrote only compatibility syntax; formulas and weights were unchanged.

---

## [ERR-20260930-004] mingw-gui-build

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: config

### Summary
The installed MinGW version does not recognize the `-municode` driver option.

### Error
```
g++.exe: error: unrecognized command line option '-municode'
```

### Context
- The GUI already uses Unicode Win32 controls and wide-character APIs.
- Only the executable entry-point convention was affected.

### Suggested Fix
Use the broadly supported `WinMain` entry point with `-mwindows`; continue using wide-character APIs internally.

### Metadata
- Reproducible: yes
- Related Files: chord_gui.cpp

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Switched the entry-point signature to WinMain and removed `-municode` from the build command.

---

## [ERR-20260930-003] compiler-discovery

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: config

### Summary
The combined compiler-discovery command returned a non-zero status because optional tools were absent.

### Error
```
where.exe could not find cl, clang++, or cmake
```

### Context
- MinGW `g++.exe` was found successfully and is sufficient for the requested Win32 build.
- No source or project file was affected.

### Suggested Fix
Probe optional executables independently or tolerate missing optional tools after a usable compiler is found.

### Metadata
- Reproducible: yes
- Related Files: none

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Selected the available MinGW g++ toolchain.

---

## [ERR-20260930-002] apply_patch

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: config

### Summary
A multi-file status-update patch contained an invalid hunk boundary.

### Error
```
apply_patch verification failed: invalid hunk
```

### Context
- The attempted patch only targeted learning-record metadata.
- Source code and tests were unaffected.

### Suggested Fix
Keep each update hunk structurally complete before starting the next file section.

### Metadata
- Reproducible: yes
- Related Files: .learnings/LEARNINGS.md, .learnings/FEATURE_REQUESTS.md

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Reapplied the updates with complete file-scoped hunks.

---

## [ERR-20260930-001] learning-file inspection

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: config

### Summary
The expected `.learnings` files were absent when the skill attempted to inspect them.

### Error
```
Get-Content: Cannot find path .learnings/LEARNINGS.md
```

### Context
- The editor context listed learning files, but they were not present in the workspace.

### Suggested Fix
Initialize the skill files before appending records.

### Metadata
- Reproducible: no
- Related Files: .learnings/LEARNINGS.md

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Reinitialized the three learning files without touching source code.

---
