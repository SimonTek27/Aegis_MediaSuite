# Aegis MediaSuite – Overview

Aegis MediaSuite is a unified media workstation that combines **player**,
**editor**, **DJ mixer**, **streaming client**, and **CD/DVD/Blu‑ray burner**
into a single Qt6/QML application. It is designed to replace the traditional
"VLC + Audacity + Mixxx + K3b" toolchain with one coherent workflow.

Core ideas:

- One engine for playback, editing, DJ, and burning
- Professional audio pipeline with FFT and EBU R128 loudness metering
- mpv‑based video playback with hardware acceleration
- SQLite‑backed media library with tagging and smart playlists

---

## Main Components

| **App** | **Description** |
|:--------|:----------------|
| **Launcher** | Main menu where to start your desired project and browse recent files |
| **Media Player** | mpv‑powered audio/video playback, MPRIS2 integration, loudness‑normalized output and spectrum analysis. |
| **Audio Editor** | destructive editor with waveform + spectral views, effects chains and batch processing. |
| **Video Editor** | Edit your videos with an app inspired by Kdenlive |
| **Disc Tools** | secure CD ripping, CD‑Text, and audio/data/bd burning via libburn/libcdio |
| **DJ Mixer** | dual‑deck mixer with planned timecode support, BPM sync, 3‑band EQ, crossfader curves and mix recording |
| **Karaoke** | Karaoke app |
| **Modtracker** | Compose music using tracker |
| **DAW** | DAW app |
| **Middleware** | Middleware app for developers |
| **Converter** | A/V converter |
| **Library** | SQLite database, TagLib‑based metadata, smart playlists and folder watching |
| **Streaming** | integration with yt‑dlp, internet radio, IPTV/M3U and capture devices via **Media Player** |
| **Label Maker** | Print labels |

See:
- `audio.md` for details of the audio engine and DSP graph
- `video.md` for video pipeline and UI modes
- `projectstatus.md` for current maturity and roadmap snapshot

---

## Building (summary)

Aegis uses CMake and depends primarily on Qt6, mpv, libsndfile, libsamplerate,
FFTW, libebur128, TagLib, libcdio/libburn/libisofs plus standard build tools.

Typical build:

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

Refer to the top‑level `doc/README.MD` or project README for distro‑specific
package lists.
