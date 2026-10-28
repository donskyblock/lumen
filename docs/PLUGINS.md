# Lumen extensions

An extension is a folder with a `plugin.json` and anything that can be run as
a command. Lumen starts it on demand, talks to it over **stdin/stdout** with
one JSON object per line, and stops it after 60 s of inactivity.

Put community extensions in the plugins folder (Settings -> Extensions ->
Open folder). Official extensions ship next to the app. Community extensions
start **disabled** and run with your user's permissions: use them at your own
risk, and read what you install.

## plugin.json

```json
{
  "id": "lrclib",
  "name": "LRCLIB",
  "version": "0.1.0",
  "description": "Shown in Settings",
  "exec": ["python3", "-S", "main.py"],
  "capabilities": ["lyrics"],
  "memoryLimitMB": 20,
  "setup": "Shown if the extension needs something installed"
}
```

`exec` runs with the extension folder as working directory. Official
extensions must stay under **20 MB** of RAM (PSS). Lumen checks every 3 s and
stops an extension that goes over its limit (community default: 64 MB).

## Protocol

Request:  `{"id": 7, "method": "lyrics.search", "params": {...}}`
Response: `{"id": 7, "result": ...}` or `{"id": 7, "error": {"message": "why"}}`

Print nothing else on stdout. stderr is logged.

### `lyrics` capability

`lyrics.search` params: `title`, `artist`, `album`, `durationMs`.
Result: array of candidates

```json
{"title": "", "artist": "", "album": "", "durationMs": 0, "synced": "LRC text or ''", "plain": "text or ''"}
```

Return a handful of the closest hits. Lumen ranks them itself (title 70 %,
artist 20 %, duration 10 %, small bonus for synced) and falls through to the
next provider if nothing is close enough. Lyrics are cached locally; a
`.lrc` file next to the song always wins.

### `source` capability

`source.search` params: `query`, `limit` -> `[{"id", "title", "artist", "album", "durationMs", "cover"}]`
(`cover` is an http(s) URL, optional)

`source.resolve` params: `id` -> `{"url", "headers", "format", "lossless", "sampleRate", "bitDepth", "bitrateKbps"}`
`url` is anything FFmpeg can open (http, https, file). Only `url` is required.
`headers` (optional, an object such as `{"User-Agent": "..."}`) is sent with every request for that stream.

### `recommend` capability (autoplay)

Declare `"recommend"` next to `"source"` and answer `source.recommend` (the bundled yt-dlp extension does, from v0.2.0; Lumen also asks plain `source` extensions and ignores an error reply). When Autoplay is on and the
queue is about to run out, Lumen asks every enabled extension that offers it, then pools the answers
with your library and the online catalog, ranks everything with your algorithm and queues the best.

`source.recommend` params: `title`, `artist`, `album` (the song that just played), `limit`
-> `[{"id", "title", "artist", "album", "durationMs", "cover"}]`, best first.
The `id` is later passed to `source.resolve` when the song is about to play, so an extension that
recommends must also resolve (`source` capability).

### Extension options (settings)

An extension can declare settings in `plugin.json`; Lumen shows them under its card in Settings -> Extensions,
stores the values, and sends the non-empty ones with every call as `params["_options"]` (an object of key -> string).
Changing one stops the extension process so the next call starts clean.

```json
"options": [
  {"key": "cookiesBrowser", "label": "Sign in using your browser", "type": "choice",
   "choices": [{"value": "", "label": "Not signed in"}, {"value": "firefox", "label": "Firefox"}],
   "hint": "Shown in small print under the control."},
  {"key": "cookiesFile", "label": "or a cookies.txt file", "type": "text"}
]
```

`type` is `choice` (a row of chips) or `text` (a one-line field, saved when you leave it).
The bundled yt-dlp extension uses these for optional YouTube sign-in: `cookiesBrowser` -> `--cookies-from-browser`,
`cookiesFile` -> `--cookies`. Lumen never reads those cookies; yt-dlp does, locally.

### `presence` capability (Discord Rich Presence and similar)

Lumen pushes what is playing to every enabled `presence` extension; the extension decides what to do with it.
Calls are debounced (about 0.8 s), so seeking or skipping quickly produces one update.

`presence.update` params: `title`, `artist`, `album`, `cover` (https URL, or `""` for local files),
`durationMs`, `positionMs`, `playing`. Every call carries the full state, so the extension can be stateless.
Lumen sends `presence.update` only while a song is playing; when playback pauses, stops, or the extension is
switched off it sends `presence.clear` instead (also when Lumen quits). Both return anything on success or
`{"error": {"message": ...}}`; a failure is logged once and never interrupts playback.

Extensions that share something about you should set `"defaultEnabled": false` in `plugin.json`: official
extensions are on by default unless they say so.

The bundled `plugins/discord-rpc` publishes a Spotify-style activity through the EquiPresence plugin's local
API (`POST http://127.0.0.1:3210/api/v1/activity`, token in `~/.equipresence/api-token`).

## Sharing extensions (marketplace)

The store in Settings reads a JSON index from any address you give it
(https, file://, or a local path). There is no central server: host the index
and packages anywhere, for example a GitHub repository.

```json
{
  "version": 1,
  "extensions": [
    {
      "id": "ytdlp", "name": "...", "version": "0.1.0", "description": "...",
      "capabilities": ["source"], "author": "...", "setup": "...",
      "download": { "url": "ytdlp-0.1.0.tar.gz", "sha256": "<64 hex chars>" }
    }
  ]
}
```

- `download.url` may be relative to the index. `sha256` is required: Lumen
  refuses a listing without one, and refuses a download that doesn't match.
- A package is a `.tar.gz` holding the extension folder (with `plugin.json`).
  Links inside the archive are rejected, and `plugin.json`'s `id` must match the listing.
- Installed extensions start **disabled**. The user switches them on.
- `python3 tools/package_extensions.py extensions/ dist/` builds the packages and the index.
