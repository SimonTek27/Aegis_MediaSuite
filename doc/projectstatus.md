# Project Status

This document summarizes the current status of Aegis MediaSuite based on the
internal roadmap.

---

## Phase Overview

- **Phase 1 – Core (Complete)**
  - mpv integration
  - Library database schema (SQLite)
  - Audio engine with FFT / EBU R128 loudness

- **Phase 2 – Editing (Complete)**
  - Waveform display
  - Destructive audio editing
  - Effects chains

- **Phase 3 – DJ & Burning (Complete)**
  - Dual‑deck DJ mixer and time‑stretching
  - CD/DVD/Blu‑ray burning
  - Secure CD audio ripping

- **Phase 4 – Polish (In progress)**
  - LV2 plugin support in the editor
  - MIDI controller mapping for DJ mode
  - DVB EPG integration for streaming
  - DLNA server / network features

- **Phase 5 – Advanced (Planned)**
  - Timecode vinyl control
  - Multi‑track recording
  - Video editing timeline
  - AI‑assisted audio restoration tools

---

## Maturity by Area

- **Playback (audio/video)** – feature‑complete for core use; ongoing
  polish and performance work.
- **Audio Editing** – core feature set implemented; future work around
  plugins, workflow polish and documentation.
- **DJ** – main functionality in place; controller mapping and advanced
  features are the next focus.
- **Disc Tools** – ripping and burning implemented; UI refinement and
  additional drive testing desirable.
- **Streaming / Capture** – integrations exist (yt‑dlp, radio, IPTV,
  capture), with DVB‑specific work still underway.
- **Documentation** – under active development (this directory).

For technical architecture, see `audio.md` and `video.md`. For a product‑
level overview and build instructions, see `readme.md`.
