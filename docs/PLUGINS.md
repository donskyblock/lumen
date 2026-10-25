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
