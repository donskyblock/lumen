# Lumen

Native C++ / Qt Quick music player for Linux. Local files, lossless playback,
Material 3 look, no Electron, no QtWebEngine.

## Build (Arch)

    sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative ffmpeg sqlite python
    cmake -B build -G Ninja && cmake --build build
    ./build/lumen ~/Music            # folder is remembered afterwards

Debian/Ubuntu: `qt6-base-dev qt6-declarative-dev qml6-module-qtquick{,-controls,-window,-layouts} libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libsqlite3-dev python3` (+ `qml6-module-qtquick-dialogs` for the Browse button).

## What is in it

| Area | Notes |
|---|---|
| Audio | FFmpeg decode thread -> float32 ring buffer -> miniaudio (PulseAudio/PipeWire-pulse or ALSA, dlopen'ed). Device opens at the track's own sample rate; gain 1.0 = untouched samples. Gapless between tracks of the same rate/channels, sample-accurate seek. Streams from extensions (http/file URLs) too. |
| Library | Several folders, add/remove in Settings or by dropping a folder on the window. Everything is cached in SQLite (`~/.local/share/lumen/lumen.db`): start-up shows the cached library at once, rescans only read new/changed files and drop deleted ones. |
| Persistence | Library, folders, lyrics cache, extension on/off + order, volume/shuffle/repeat, window size, last page, and the play queue + position (comes back paused where you left off). |
| Search | One page for songs + albums in the library and every enabled `source` extension. Filter boxes on Albums/Songs stay too. |
| Lyrics | Synced (LRC) or plain, with a big follow-along view. Order: `.lrc` file next to the song, cache, then lyrics extensions in your priority order. Candidates are ranked by title (70%), artist (20%), length (10%) so a loose search can't pick the wrong song. |
| Extensions | Separate processes speaking JSON-RPC over stdio, started on demand, stopped after 60 s idle, memory-watched (20 MB cap for official). Capabilities: `lyrics`, `source`. See `docs/PLUGINS.md`. Official: `plugins/lrclib` (LRCLIB lyrics). Example source: `examples/plugins/local-demo`. |
| UI | Material 3 style panels, dynamic palette + soft cover backdrop, wavy seek bar, expressive play button, lyrics page, search page, settings page. Icons drawn from SVG path data in C++. |

Keyboard: Space = play/pause (outside text boxes), Tab/Enter on controls, Left/Right on seek and volume.

Data location: `~/.local/share/lumen/` (override with `LUMEN_DATA_DIR`). Community extensions go in its `plugins/` folder and start disabled.

## Layout

    src/AudioEngine.*    decode thread, ring buffer, device, gapless segments
    src/Player.*         queue, shuffle/repeat, session save/restore, remote playback
    src/Library.*        folders, incremental scanner, models, local search
    src/Db.*             SQLite (folders, track cache, lyrics cache, key/value)
    src/PluginManager.*  extension discovery, JSON-RPC processes, memory watch
    src/Lyrics.*         lookup, LRC parser, closest-match ranking
    src/Probe.* Covers.* Palette.* Icons.* WavyProgress.*
    qml/                 UI (Main.qml is the entry point)
    plugins/             official extensions (copied next to the binary at build time)
    examples/plugins/    example extensions
    docs/PLUGINS.md      extension protocol
    tests/               mock lyrics server for offline testing

## Not done yet

- Extension marketplace (index of installable extensions) and streaming sources such as YouTube Music / SoundCloud.
- Request headers from `source.resolve` (cookies, auth) are not passed to FFmpeg yet; remote tracks don't get gapless/queueing.
- MPRIS (media keys), queue editing, artist pages, bit-perfect exclusive ALSA mode.
- Embedded lyrics tags are not read yet (sidecar `.lrc` files are).

## Debug hooks

`LUMEN_SHOT=/tmp/x.png` with `LUMEN_PAGE` (0 albums, 1 songs, 2 search, 3 lyrics, 4 settings), `LUMEN_QUERY`, `LUMEN_PLAY=1`,
`LUMEN_SEEK=ms`, `LUMEN_REMOTE=pluginId:itemId`, `LUMEN_DELAY=ms` navigates, prints RSS/state, saves a screenshot and exits.
Run headless with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.
