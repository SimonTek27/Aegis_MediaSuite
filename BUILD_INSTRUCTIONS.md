# Aegis MediaSuite — Build Instructions

## Prerequisites

```bash
sudo pacman -S qt6-base qt6-declarative mpv libcdio libcdio-paranoia libsndfile \
               libsamplerate taglib fftw gstreamer gst-plugins-base
```

## Build Steps

> **IMPORTANT**: Always start with a clean build directory to avoid stale moc-generated files.

```bash
cd /path/to/Aegis_MediaSuite
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

After a successful build the `build/` directory contains:

| Executable            | What it launches                     |
|-----------------------|--------------------------------------|
| `aegis`               | Main binary (all modes via `--mode`) |
| `aegis-launcher`      | Launcher / suite hub                 |
| `aegis-mediaplayer`   | Media Player                         |
| `aegis-audioeditor`   | Audio Editor                         |
| `aegis-videoeditor`   | Video Editor                         |
| `aegis-daw`           | DAW                                  |
| `aegis-disctools`     | Disc Tools (rip / burn / BD)         |
| `aegis-djmixer`       | DJ Mixer                             |
| `aegis-karaoke`       | Karaoke                              |
| `aegis-modtracker`    | Mod Tracker                          |
| `aegis-musicnotation` | Music Notation                       |
| `aegis-middleware`    | Middleware                           |
| `aegis-labelmaker`    | Label Maker                          |
| `aegis-converter`     | Converter                            |
| `aegis-capture`       | Capture / Screen Recorder            |
| `aegis-streaming`     | Streaming                            |

Each per-mode executable is a tiny self-contained binary that locates the
main `aegis` binary (sibling in the same directory, or via PATH) and
re-execs it with the correct `--mode=` flag, forwarding all arguments.

## Running from the build directory

```bash
cd build
./aegis-launcher          # open the Launcher
./aegis-mediaplayer       # open the Media Player
./aegis-daw               # open the DAW
# etc.
```

## Install (system-wide)

```bash
cd build
sudo cmake --install . --prefix /usr/local
```

All per-mode binaries are installed to `/usr/local/bin/`.

## Manual mode selection (any build)

You can always invoke any mode directly from the main binary:

```bash
./aegis --mode=launcher
./aegis --mode=mediaplayer
./aegis --mode=daw
# etc.
```

## Known Requirements
- C++20 compiler (GCC 11+ or Clang 12+)
- Qt 6.2+
- CMake 3.19+

## Clean Build
If you see errors about "redefinition" or stale moc files, always do a full clean:
```bash
rm -rf build && mkdir build && cd build && cmake .. && make
```
