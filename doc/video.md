# Video Pipeline and Interface Modes

This document describes how Aegis handles video playback and how it maps
onto the various UI modes.

---

## Backend

Aegis uses **mpv** (via libmpv) as the core video backend.

Key characteristics:

- Hardware‑accelerated decoding where available (VA‑API/VDPAU/DXVA, etc.)
- Support for a wide range of container and codec formats
- Shared timing and synchronization with the audio engine
- Exposed statistics and events for the QML UI (position, state, errors)

The mpv context is isolated in its own thread so that intensive video
rendering does not block the Qt/QML UI or the real‑time audio thread.

---

## Integration with the UI

The Qt6/QML frontend embeds the mpv render context inside the Player mode.
The same backend is reused by other modes when they need video preview.

Common features:

- Play / pause / seek controls
- Track and subtitle selection
- On‑screen display for basic metadata
- Spectrum overlay synchronized with the audio FFT engine

---

## Interface Modes

Although Aegis is a single application, it exposes several focused
"modes" optimized for different tasks.

### Player Mode

- Full‑screen or windowed video playback
- Audio spectrum overlay
- Basic playlist and library browser

### Editor Mode

- Primarily audio‑centric, but can show video preview when editing the
  audio track of a media file
- Waveform and spectral views drive the editing actions, with the mpv
  preview kept in sync.

### DJ Mode

- Focused on dual‑deck audio mixing; video is typically not displayed.
- In the future, can be extended with video‑aware decks or visualizers
  using the same backend.

### Burner / Disc Tools

- Uses mpv for preview of DVD‑Video or other disc‑based content
- Otherwise focused on compilation and burning workflows.

---

## Relation to Other Docs

- `readme.md` – project overview and main components
- `audio.md` – details of the audio/DSP side, including FFT and loudness
- `projectstatus.md` – status of video‑related features versus roadmap
