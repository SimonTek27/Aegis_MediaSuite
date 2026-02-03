# Audio Architecture

This document describes the audio layer that underpins the media player,
editor, DJ mixer and karaoke features.

The design is built around three pillars:

1. **AudioEngine** – real‑time routing, mixing and I/O
2. **EffectChain / DSP** – modular processing graphs
3. **MpvBackend** – decoding/encoding and A/V sync

---

## High‑Level Layout

```text
Application Layer (Player / DJ / Karaoke / Editor)
              │
              ▼
        AudioEngine (audio.h)
              │
      ┌───────┼─────────────────────────────┐
      ▼       ▼                             ▼
 Deck/Bus EffectChains (Deck A, Master, Karaoke, …)
              │
              ▼
        Individual Effects (EQ, Filters, Comp, Limiter,
        KaraokeProcessor, etc.)
              │
              ▼
          MpvBackend + Audio I/O
```

Each application feature (deck, master bus, karaoke bus, editor output) owns
its own `EffectChain`, but all chains are driven by the shared `AudioEngine`
for consistent latency and synchronization.

---

## AudioEngine

Responsibilities:

- Real‑time processing and mixing of all active streams
- Scheduling of effect chains and buffer hand‑off
- Integration with the mpv backend for decoded audio
- Providing metering data (FFT, loudness) to the UI

The engine is designed for a dedicated audio thread with real‑time priority
(SCHED_FIFO on supported systems).

---

## Effect Chains and DSP

`EffectChain` objects represent ordered lists of DSP units.
Typical chains:

- **Deck chains** (e.g. Deck A/Deck B in the DJ mixer)
  - Input gain
  - EQ/filter
  - Optional dynamics (compressor/limiter)
- **Master chain**
  - Master EQ
  - Loudness normalization / limiting
- **Karaoke chain**
  - `KaraokeProcessor` for vocal reduction or enhancement

Effects are designed to be:

- Reusable across application modes
- Configurable from QML
- Safe for real‑time processing (no allocations in the hot path)

---

## MpvBackend and Sources

The `MpvBackend` is the bridge between decoded audio and the audio engine.
It can pull audio from:

- Local files
- Network streams
- Optical discs

Multiple logical contexts (file, stream, disc) can be mapped into the same
engine so that the player, editor preview and DJ decks share infrastructure.

---

## Relationship to Features

- **Media Player** – uses the engine for playback, FFT metering and
  loudness normalization.
- **Editor** – monitors the edited buffer through an engine‑backed bus
  and effect chain.
- **DJ Mixer** – has per‑deck chains plus a master chain; the crossfader
  and EQ operate on these busses.
- **Karaoke** – uses the dedicated `KaraokeProcessor` path described above.

For a higher‑level product view, see `readme.md`. For current feature
completion, see `projectstatus.md`.
