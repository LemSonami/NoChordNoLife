# Feature Requests

Capabilities requested by the user.

---

## [FEAT-20260930-002] cpp-win32-port

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: high
**Status**: resolved
**Area**: backend

### Requested Capability
Port both Python music algorithms to C++ without changing their formulas, and provide a native GUI that displays the 0-100 result in a dialog.

### User Context
The C++ port will be a stepping stone toward a future VST plugin for DAWs such as FL Studio.

### Complexity Estimate
complex

### Suggested Implementation
Keep algorithm code independent of Win32, expose it through a shared header, and place all native window code in a third translation unit.

### Metadata
- Frequency: first_time
- Related Features: mode-aware-chord-emotion

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Added two C++ algorithm ports, a shared API header, a Win32 GUI, a compiled executable, and Python/C++ parity verification.

---

## [FEAT-20260930-001] mode-aware-chord-emotion

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: high
**Status**: resolved
**Area**: backend

### Requested Capability
Accept a mode string plus a chord string and let the selected mode affect the 0-100 chord-emotion score.

### User Context
Context-free scoring makes a chromatic borrowed chord such as `C D# G#` sound artificially bright in C Ionian.

### Complexity Estimate
medium

### Suggested Implementation
Reuse `parse_mode` and `tonal_stability`, calculate modal brightness and chromatic distance, and expose the new two-string interface through both the API and command-line entry point.

### Metadata
- Frequency: first_time
- Related Features: chord_emotion_score

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: The public API and CLI now accept mode and chord strings, and analysis exposes modal diagnostics.

---
