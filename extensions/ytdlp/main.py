#!/usr/bin/env python3
"""Lumen 'source' extension that shells out to a user-installed yt-dlp.

yt-dlp is NOT bundled: you install and update it yourself. It runs as a
short-lived child process per request, so this extension stays tiny while idle.

Reliability notes (why resolve() looks the way it does):
  * YouTube serves audio through several "player clients", and which one works changes from week to
    week and from video to video. resolve() tries them in turn instead of trusting the first.
  * A stream address that yt-dlp prints is not proof it plays: some come back 403 as soon as FFmpeg
    asks for them. Every address is test-fetched (one tiny ranged request, same headers) before it is
    handed to Lumen, and a bad one makes us fall through to the next client.
  * Plain HTTPS audio is preferred over DASH/HLS manifests, which FFmpeg opens far less reliably.
"""
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed


OUT_LOCK = threading.Lock()

# Tried in order until one yields an address that really opens. None = yt-dlp's own default.
YOUTUBE_CLIENTS = ["youtube:player_client=web_music,web", None, "youtube:player_client=tv,mweb", "youtube:player_client=ios"]
AUDIO_FORMAT = "bestaudio[protocol=https]/bestaudio[protocol=http]/bestaudio/best"
RESOLVE_BUDGET = 40          # seconds for everything one resolve may spend
COMMON = ["--ignore-config", "--no-warnings", "--socket-timeout", "10", "--retries", "2", "--extractor-retries", "2"]


BROWSERS = {"firefox", "chrome", "chromium", "brave", "edge", "vivaldi", "opera", "safari"}


def auth_args(p):
    """Optional sign-in, from the extension's settings (Lumen passes them as params["_options"]).
    A cookies file wins over a browser. Only known browser names are accepted."""
    o = (p or {}).get("_options") or {}
    f = (o.get("cookiesFile") or "").strip()
    if f:
        f = os.path.expanduser(f)
        if os.path.isfile(f):
            return ["--cookies", f]
    b = (o.get("cookiesBrowser") or "").strip().lower()
    if b in BROWSERS:
        return ["--cookies-from-browser", b]
    return []


def friendly(msg, signed_in):
    """Turn yt-dlp's raw complaint into something a listener can act on."""
    low = msg.lower()
    if "not a bot" in low or "sign in to confirm" in low:
        return ("YouTube is refusing this request and says the cookies aren't accepted. Sign in again in your browser."
                if signed_in else
                "YouTube wants you signed in (bot check). Sign in under Settings > Extensions > yt-dlp.")
    if "age" in low and ("restricted" in low or "confirm your age" in low):
        return "This video is age-restricted. Sign in under Settings > Extensions > yt-dlp to play it."
    if "cookies" in low and ("could not" in low or "failed" in low or "unable" in low):
        return "Couldn't read your browser's login (is the browser name right, and the profile unlocked?)."
    if "private video" in low or "video unavailable" in low or "not available" in low:
        return "This video isn't available (removed, private or blocked in your country)."
    return msg


def emit(obj):
    with OUT_LOCK:
        sys.stdout.write(json.dumps(obj) + "\n")
        sys.stdout.flush()


def run(args, timeout):
    exe = shutil.which("yt-dlp")
    if not exe:
        raise RuntimeError("yt-dlp isn't installed. Install it (Arch: pacman -S yt-dlp) and try again.")
    p = subprocess.run([exe] + args, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        lines = [l for l in p.stderr.strip().splitlines() if l.strip()]
        raise RuntimeError(lines[-1] if lines else "yt-dlp failed")
    return p.stdout


def search(p, partial=None):
    q = p.get("query", "").strip()
    limit = max(4, int(p.get("limit", 10)))
    half = max(4, (limit + 1) // 2)
    per_site = {}
    errors = []
    def one_site(prefix, label):
        rows = []
        try:
            data = json.loads(run(["--flat-playlist", "-J", "--no-warnings", "%s%d:%s" % (prefix, half, q)], 25))
            for e in data.get("entries") or []:
                url = e.get("url") or e.get("webpage_url")
                if not url:
                    continue
                thumbs = e.get("thumbnails") or []
                rows.append({
                    "id": url,
                    "title": e.get("title") or url,
                    "artist": e.get("uploader") or e.get("channel") or "",
                    "album": label,
                    "durationMs": int((e.get("duration") or 0) * 1000),
                    "cover": thumbs[-1].get("url", "") if thumbs else "",
                })
        except Exception as ex:
            return label, rows, str(ex)
        return label, rows, None
    # These are independent network searches. Run them concurrently and notify
    # Lumen as each platform answers instead of making SoundCloud wait for YouTube.
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(one_site, prefix, label) for prefix, label in (("ytsearch", "YouTube"), ("scsearch", "SoundCloud"))]
        for future in as_completed(futures):
            label, rows, error = future.result()
            per_site[label] = rows
            if error:
                errors.append(error)
            elif partial and rows:
                partial(rows)
    if not any(per_site.values()) and errors:
        raise RuntimeError(friendly(errors[0], False))
    out = []                                   # interleave so neither site buries the other
    sites = [per_site.get("YouTube", []), per_site.get("SoundCloud", [])]
    for i in range(max((len(r) for r in sites), default=0)):
        for rows in sites:
            if i < len(rows):
                out.append(rows[i])
    return out[:limit]


def _video_id(url):
    if "v=" in url:
        return url.split("v=", 1)[1].split("&", 1)[0]
    if "youtu.be/" in url:
        return url.split("youtu.be/", 1)[1].split("?", 1)[0]
    return ""


def _keep(rows, seed_title):
    seed = (seed_title or "").strip().lower()
    out = []
    for r in rows:
        d = r.get("durationMs", 0)
        if d and not (60000 <= d <= 600000):
            continue
        if seed and r.get("title", "").strip().lower() == seed:
            continue
        out.append(r)
    return out


def recommend(p):
    """Songs that follow on from the one playing.

    First choice is YouTube's own "Mix" for that song (what its radio plays next): find the song's
    video, then read the RD<id> playlist. If that gives too little, fall back to a search for similar
    uploads. Lumen ranks the pooled answers with the listener's algorithm, so this just needs to be
    relevant, not final.
    """
    seed = ("%s %s" % (p.get("artist", ""), p.get("title", ""))).strip()
    if not seed:
        return []
    limit = max(4, int(p.get("limit", 20)))
    rows = []
    try:
        found = json.loads(run(["--flat-playlist", "-J", "--no-warnings", "ytsearch5:%s" % seed], 25))
        vid = _seed_video(found.get("entries") or [], p)
        if vid:
            mix = json.loads(run(["--flat-playlist", "-J", "--no-warnings", "--playlist-end", str(limit + 5),
                                  "https://www.youtube.com/watch?v=%s&list=RD%s" % (vid, vid)], 40))
            for e in mix.get("entries") or []:
                url = e.get("url") or e.get("webpage_url")
                if not url or _video_id(url) == vid:
                    continue
                thumbs = e.get("thumbnails") or []
                rows.append({
                    "id": url,
                    "title": e.get("title") or url,
                    "artist": e.get("uploader") or e.get("channel") or "",
                    "album": "YouTube Mix",
                    "durationMs": int((e.get("duration") or 0) * 1000),
                    "cover": thumbs[-1].get("url", "") if thumbs else "",
                })
    except Exception:
        rows = []
    auth = auth_args(p)
    if auth:
        # signed in: YouTube's own recommendations for this account (its home feed) join the pool,
        # and the Mix above is already personalised to the account too
        try:
            feed = json.loads(run(list(COMMON) + auth + ["--flat-playlist", "-J", "--playlist-end", "25", ":ytrec"], 40))
            added = 0
            for e in feed.get("entries") or []:
                url = e.get("url") or e.get("webpage_url")
                d = int((e.get("duration") or 0) * 1000)
                if not url or not (60000 <= d <= 600000) or added >= 12:
                    continue
                thumbs = e.get("thumbnails") or []
                rows.append({
                    "id": url,
                    "title": e.get("title") or url,
                    "artist": e.get("uploader") or e.get("channel") or "",
                    "album": "Your YouTube recommendations",
                    "durationMs": d,
                    "cover": thumbs[-1].get("url", "") if thumbs else "",
                })
                added += 1
        except Exception:
            pass
    rows = _keep(rows, p.get("title"))
    # Deliberately no keyword fallback: searching YouTube for the song's name only returns more songs
    # with that name. Lumen fills the rest from its own index of what you listen to.
    return rows[:limit]


def _words(s):
    return set("".join(ch.lower() if ch.isalnum() else " " for ch in (s or "")).split())


def _seed_video(entries, p):
    """The upload of the seed song itself (not a cover, a slowed edit or a reaction): its title has all the
    song's words and the artist's name, and it does not announce itself as something else."""
    want = _words(p.get("title"))
    artist = _words(p.get("artist"))
    bad = {"cover", "slowed", "reverb", "reaction", "remix", "sped", "nightcore", "live", "karaoke", "instrumental"}
    best = None
    for e in entries:
        t = _words(e.get("title"))
        if not want or not want <= t:
            continue
        if artist and not (artist <= (t | _words(e.get("uploader") or e.get("channel")))):
            continue
        if (t & bad) - want:
            continue
        best = e
        break
    if best is None:
        return ""
    return _video_id(best.get("url") or best.get("webpage_url") or "")


def _is_youtube(url):
    return "youtube.com" in url or "youtu.be" in url


def _pick(j):
    """The chosen audio stream from yt-dlp's JSON: (url, headers, codec, abr)."""
    url = j.get("url")
    headers = dict(j.get("http_headers") or {})
    codec = j.get("acodec") or j.get("ext") or ""
    abr = j.get("abr") or 0
    if not url:
        fmts = [f for f in j.get("formats", []) if f.get("url") and f.get("vcodec") in (None, "none")]
        # same preference as AUDIO_FORMAT: plain https first, then by bitrate
        fmts.sort(key=lambda f: (f.get("protocol") in ("https", "http"), f.get("abr") or 0))
        if fmts:
            f = fmts[-1]
            url = f["url"]
            headers = dict(f.get("http_headers") or headers)
            codec = f.get("acodec") or f.get("ext") or codec
            abr = f.get("abr") or abr
    return url, headers, codec, abr


def _opens(url, headers):
    """One tiny ranged request, made the way FFmpeg will make it. False only on a definite HTTP refusal."""
    if ".m3u8" in url or url.startswith("file:"):
        return True
    h = dict(headers)
    h["Range"] = "bytes=0-1023"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=8) as r:
            r.read(256)
            return r.status in (200, 206)
    except urllib.error.HTTPError as e:
        return e.code == 416                  # range past the end of a tiny file still means it exists
    except Exception:
        return True                           # timeouts / TLS quirks: not proof of a bad link, let FFmpeg decide


def resolve(p):
    target = p["id"]
    auth = auth_args(p)
    deadline = time.monotonic() + RESOLVE_BUDGET
    clients = YOUTUBE_CLIENTS if _is_youtube(target) else [None]
    last = "No playable audio stream was found."
    for client in clients:
        remaining = deadline - time.monotonic()
        if remaining < 6:
            break
        args = list(COMMON) + auth + ["-f", AUDIO_FORMAT, "-J", "--no-playlist"]
        if client:
            args += ["--extractor-args", client]
        try:
            j = json.loads(run(args + [target], min(28, remaining)))
        except Exception as ex:
            last = str(ex)
            continue
        url, headers, codec, abr = _pick(j)
        if not url:
            last = "No playable audio stream was found."
            continue
        if not _opens(url, headers):
            last = "The stream was refused (HTTP error); trying another way."
            continue
        return {
            "url": url,
            "headers": headers,
            "format": codec.split(".")[0],
            "bitrateKbps": int(abr or 0),
            "lossless": False,
        }
    raise RuntimeError(friendly(last, bool(auth)))


METHODS = {"source.search": search, "source.resolve": resolve, "source.recommend": recommend}

def handle(req):
    try:
        method = req["method"]
        if method == "source.search":
            result = search(req.get("params") or {}, lambda rows: emit({"id": req["id"], "partial": True, "result": rows}))
        elif method in METHODS:
            result = METHODS[method](req.get("params") or {})
        else:
            raise RuntimeError("Unknown method: %s" % method)
        resp = {"id": req["id"], "result": result}
    except Exception as e:
        resp = {"id": req.get("id"), "error": {"message": str(e)}}
    emit(resp)


for line in sys.stdin:
    if not line.strip():
        continue
    try:
        req = json.loads(line)
    except Exception:
        continue
    threading.Thread(target=handle, args=(req,), daemon=True).start()
