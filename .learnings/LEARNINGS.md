# Learnings

Corrections, insights, and knowledge gaps captured during development.

**Categories**: correction | insight | knowledge_gap | best_practice

---

## [LRN-20260930-001] correction

**Logged**: 2026-09-30T00:00:00+08:00
**Priority**: high
**Status**: resolved
**Area**: backend

### Summary
Chord emotion cannot be estimated reliably without tonal or modal context.

### Details
An enharmonically spelled Ab-major sonority (`C D# G#`) is intrinsically major, but in C Ionian it contains two out-of-mode pitch classes and functions as a dark borrowed sonority. A context-free interval model scores it too brightly.

### Suggested Action
Include mode brightness, out-of-mode ratio, and root stability in the emotion score while keeping intrinsic chord quality as a separate component.

### Metadata
- Source: user_feedback
- Related Files: chord_emotion.py, chord_progression.py
- Tags: music-theory, modal-context, valence

### Resolution
- **Resolved**: 2026-09-30T00:00:00+08:00
- **Notes**: Added mode brightness, root instability, and chromatic-ratio adjustments with regression coverage.

---
