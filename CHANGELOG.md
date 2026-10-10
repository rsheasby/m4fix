# Changelog

## 1.0.0

First release.

- `LeadtoolsHeap`: decoded images in cached heap memory instead of uncached memory-mapped sections.
- `SoundLocks`: sound engine mutexes replaced with critical sections.
- `TransitionDelayMs`: the fixed 550 ms freeze after each click is now configurable (default 100 ms).
- Windows installer (`m4fix-setup.exe`) with automatic game detection, silent mode and self-elevation.
- Mac installer (`Install m4fix.command` / `m4fix.sh`) for the Steam CrossOver build.
