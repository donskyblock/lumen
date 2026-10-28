# Lumen

Native C++ / Qt Quick music player for Linux. Local files, lossless playback,
Material 3 look, no Electron, no QtWebEngine.

## Build (Arch)

    sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative ffmpeg sqlite zlib python
    cmake -B build -G Ninja && cmake --build build
    ./build/lumen ~/Music            # folder is remembered afterwards

Debian/Ubuntu: `qt6-base-dev qt6-declarative-dev qml6-module-qtquick{,-controls,-window,-layouts} libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libsqlite3-dev zlib1g-dev python3` (+ `qml6-module-qtquick-dialogs` for the Browse button).

## Build (Windows 10/11)

Install Visual Studio 2022 with the **Desktop development with C++** workload, CMake, and Git for Windows. From PowerShell in the repository:

    .\setup.ps1
    .\build.ps1
    .\run.ps1

`setup.ps1` bootstraps vcpkg under `.deps\vcpkg` and installs Qt 6, FFmpeg, and SQLite. To use an existing vcpkg checkout, set `$env:LUMEN_VCPKG_ROOT` first. The default build uses the x64 Visual Studio generator and writes to `build-windows\Release\lumen.exe`.

Install the built app for your Windows user with `./install-windows.ps1`. It copies the runtime files and official extensions under `%LOCALAPPDATA%\Programs\Lumen` and adds a Start Menu shortcut.

## Install on Linux

Build and install a user-local copy (`~/.local` by default), including an application launcher:

    ./install-linux.sh

Set `PREFIX=/usr/local` to install system-wide (run with appropriate privileges), or set `LUMEN_BUILD_DIR` to select a different build directory. On first launch, Lumen offers to copy all official extensions from the installed repository bundle into its user extensions folder; you can also skip this and do it later from Settings.

Spotify playlist imports are in **Settings → Your algorithm → Import Spotify playlists**. Import either Spotify's Account data ZIP/JSON export or a CSV downloaded from [TuneMyMusic](https://www.tunemymusic.com/transfer/spotify-to-file). TuneMyMusic's free export is limited to 500 tracks; larger libraries can use Spotify's account-data export. Lumen reads the file locally and searches its online catalog using title, artist, album, and duration to find a likely match. Matched tracks are stored as online catalog entries in the imported playlists; Liked/Favorite Songs exports merge into Lumen's **Liked Songs**. Unmatched tracks remain as unavailable metadata entries. Track metadata is sent to the catalog for matching. Lumen does not import or stream Spotify audio.

## What is in it

| Area | Notes |
|---|---|
| Audio | FFmpeg decode thread -> float32 ring buffer -> miniaudio (PulseAudio/PipeWire-pulse or ALSA, dlopen'ed). Device opens at the track's own sample rate; gain 1.0 = untouched samples. Gapless between tracks of the same rate/channels, sample-accurate seek. Streams from extensions (http/file URLs) too. |
| Online catalog | Search any artist and get a full page with no setup: popular songs, albums, singles, related artists, charts (metadata from Deezer's public API, cached in SQLite, paced under its quota). Songs play at once through a resolver: your own file of the same song first, then any "source" extension you have on (full length), then the catalog's 30-second preview. "Add to library" imports an artist, album or song (all songs, deduplicated) so it appears in Songs/Albums/Artists, search, playlists and history. |
| Metadata | `src/Metadata.*`: looks up the real title/artist/album/year/genre/ISRC/cover for songs that come from uploads (YouTube, SoundCloud). Providers, all keyless: Deezer, Apple's iTunes search and MusicBrainz, each paced on its own and cached on disk. Order and addresses: `LUMEN_META_PROVIDERS`, `LUMEN_ITUNES_URL`, `LUMEN_MUSICBRAINZ_URL`, `LUMEN_CONTACT`. |
| Algorithm | Learns from plays (fading with a half-life), likes, skips and saved artists. Powers "Play my radio", per-artist radio and Home "Discover". Seven knobs in Settings -> Your algorithm. Export writes knobs + history + likes + playlists + saved artists to one JSON file; import on another device rebuilds them (saved artists are re-imported from the catalog). |
| Autoplay | When the queue is nearly out (the loop button in the player bar), Lumen pools similar songs from your library, the online catalog and every enabled extension that answers `source.recommend`, ranks them with your algorithm and queues them. See docs/PLUGINS.md. |
| Library | Several folders (Settings, or drop a folder on the window). Cached in SQLite (`~/.local/share/lumen/lumen.db`): start-up shows the cached library at once, rescans only read new/changed files and drop deleted ones. Genre, year and "date added" are tracked. |
| Home | Spotify-style: quick picks, "Made for you" mixes (Daily Mix per top artist, Discovery, Forgotten Favorites, Lossless Selects; genre mixes before there's history), Jump back in, Recently added, your playlists, On repeat, artists. Sections can be hidden per profile. |
| Artists | Artist pages with popular songs, discography, "appears on" and "Fans also like" (by shared genre and by what you play together). Compilations credit each track's artist. |
| Playlists | Create/rename/delete, add from any song/album/artist/mix menu, reorder, "Liked songs" heart, save any mix as a playlist, play next / add to queue. |
| Search | One search box in the top bar, always visible (`/` or Ctrl+K). Accent-insensitive, word-start matching, typo tolerant when nothing matches, top result, and filters for songs/albums/artists/playlists/online plus "lossless only". Songs/Albums/Artists pages have sort + lossless / Hi-Res / format filters. |
| Profiles | Named looks: dark / black / light, accent from cover art or a custom hue, corner style, density, card size, text size, cover glow, animations, wavy seek bar, home sections. Switch from the avatar in the top bar. Presets: Default, Midnight, Paper, Neon, Cozy, Focus. |
| Persistence | Library, folders, play history, playlists, profiles, lyrics cache, extension on/off + order, volume/shuffle/repeat, window size, last page, and the play queue + position (comes back paused where you left off). |
| Audio | FFmpeg decode thread -> float32 ring buffer -> miniaudio (PulseAudio/PipeWire-pulse or ALSA, dlopen'ed). Device opens at the track's own sample rate; gain 1.0 = untouched samples. Gapless between same-format tracks, sample-accurate seek. |
| Lyrics | Synced (LRC) or plain; `.lrc` next to the song, then cache, then lyrics extensions in your order, ranked by title/artist/length. |
| Marketplace | Settings -> Get more extensions reads a JSON index from any address (GitHub raw, file://, a path). Downloads must match the SHA-256 in the index, are unpacked without links, and start disabled. Build packages with `tools/package_extensions.py`. |
| Song index | `src/SongIndex.*` + `Db::idx*`: a compact local memory of every song seen (catalog, extensions, searches, plays) and how songs/artists relate. Autoplay draws on it ("played after", related artists, artists you listen to / searched for). A passive indexer reads top songs and related artists for those artists, one every ~40 s (max 300/day); switch it off in Settings -> Algorithm. About 24 MB per 100,000 songs, capped. |
| Discord status | Official extension `plugins/discord-rpc` (off until you switch it on in Settings -> Extensions): "Listening to <artist>", song, album art, progress bar and a "Listen for free on Lumen" button, through the EquiPresence Discord plugin. Cleared on pause, stop, quit. |
| Extensions | Separate processes speaking JSON-RPC over stdio, started on demand, stopped after 60 s idle, memory-watched (20 MB cap for official). Capabilities: `lyrics`, `source`, `recommend`, `presence`. See `docs/PLUGINS.md`. Official: `plugins/lrclib` (LRCLIB lyrics), `plugins/discord-rpc` (Discord status). Community-style: `extensions/ytdlp` (YouTube + SoundCloud through your own yt-dlp, not bundled; install it from the store). Example: `examples/plugins/local-demo`. |
| UI | Material 3 style panels, dynamic palette + soft cover backdrop, wavy seek bar, expressive play button, lyrics page, search page, settings page. Icons drawn from SVG path data in C++. |

Keyboard: Space play/pause (outside text boxes), `/` or Ctrl+K search, Esc clears search, Alt+Left/Right back/forward, Ctrl+Left/Right previous/next, Tab/Enter on controls, Left/Right on seek and volume. Right-click songs, albums, artists, playlists and mixes for more actions.

Data location: `~/.local/share/lumen/` (override with `LUMEN_DATA_DIR`). Community extensions go in its `plugins/` folder and start disabled.

## Layout

    src/AudioEngine.*    decode thread, ring buffer, device, gapless segments
    src/Player.*         queue, shuffle/repeat, session save/restore, remote playback
    src/Library.* Discovery.cpp  folders, incremental scanner, models, search ranking, artist pages, mixes
    src/Playlists.* Profiles.* PlayStats.h  playlists, look profiles, listening history
    src/Catalog.*        online catalog client (search, artist/album pages, import, request pacing + cache)
    src/Resolver.*       finds something to play for a catalog song (local file / extension / preview)
    src/SongIndex.*      song index + passive indexer (see above)
    src/Presence.*       pushes now-playing to `presence` extensions (debounced, clears on pause/quit)
    src/Metadata.*       keyless song metadata lookup (Deezer, iTunes, MusicBrainz), cached
    src/Taste.*          the algorithm: profile, radio, discover, export/import
    src/Db.*             SQLite (folders, track cache, lyrics cache, catalog cache, key/value)
    src/PluginManager.*  extension discovery, JSON-RPC processes, memory watch
    src/Lyrics.*         lookup, LRC parser, closest-match ranking
    src/Probe.* Covers.* Palette.* Icons.* WavyProgress.*
    qml/                 UI (Main.qml is the entry point)
    plugins/             official extensions (copied next to the binary at build time)
    examples/plugins/    example extensions
    docs/PLUGINS.md      extension protocol
    tests/               mock Deezer API (mock_deezer.py), mock lyrics server, header-checking stream server, yt-dlp stand-in
    extensions/ytdlp/    yt-dlp source extension (package it, then install via the store)
    tools/               package_extensions.py

## Not done yet

- A hosted index repository (the default index address is a placeholder), signing beyond SHA-256, and extension updates on a schedule.
- Remote tracks don't get queueing or gapless playback, and the yt-dlp extension is only tested against a stand-in, not the live sites.
- MPRIS (media keys), queue editing, artist pages, bit-perfect exclusive ALSA mode.
- Embedded lyrics tags are not read yet (sidecar `.lrc` files are).

## Testing the catalog offline

    python3 tests/mock_deezer.py 8765 &
    LUMEN_DEEZER_URL=http://127.0.0.1:8765 LUMEN_DATA_DIR=/tmp/ld ./build/lumen

Hooks (with `LUMEN_SHOT`): `LUMEN_PLAY_ARTIST=<id>`, `LUMEN_IMPORT_ARTIST=<id>`, `LUMEN_RADIO=taste|artist:<id>`, `LUMEN_META_LOOKUP=title|artist|ms`, `LUMEN_PLAY_ONLINE=<track id>`, `LUMEN_PLAY_FIXED=id,id`, `LUMEN_INDEX_DEMO=id,id,id`, `LUMEN_PLAY_EXT=plugin:item|title|artist`,
`LUMEN_EXPORT=<file>` (`LUMEN_EXPORT_AT=ms`), `LUMEN_TASTE_IMPORT=<file>`; routes `oartist` / `oalbum` take a catalog id.

## Debug hooks

`LUMEN_SHOT=/tmp/x.png` with `LUMEN_ROUTE` (home, artists, albums, songs, lyrics, settings, artist, album, playlist, mix) + `LUMEN_ROUTE_ARG`, `LUMEN_QUERY`, `LUMEN_PROFILE=<preset>`, `LUMEN_PLAY=1`,
`LUMEN_SEEK=ms`, `LUMEN_REMOTE=pluginId:itemId`, `LUMEN_DELAY=ms` navigates, prints RSS/state, saves a screenshot and exits.
Run headless with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.
