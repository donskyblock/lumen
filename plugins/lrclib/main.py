#!/usr/bin/env python3
"""Lumen lyrics provider backed by https://lrclib.net.

Speaks newline-delimited JSON-RPC on stdin/stdout (see docs/PLUGINS.md).
Standard library only, to stay far below the 20 MB extension budget.
"""
import json
import os
import sys
import urllib.parse
import urllib.request

BASE = os.environ.get("LRCLIB_URL", "https://lrclib.net").rstrip("/")
UA = "Lumen/0.2 (music player extension)"


def fetch(params):
    url = BASE + "/api/search?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def lyrics_search(p):
    title = p.get("title", "")
    artist = p.get("artist", "")
    rows = []
    if artist:
        rows = fetch({"track_name": title, "artist_name": artist})
    if not rows:                       # loose retry: title only, the host ranks the results
        rows = fetch({"track_name": title})
    out = []
    for d in rows[:20]:
        if d.get("instrumental"):
            continue
        out.append({
            "title": d.get("trackName") or "",
            "artist": d.get("artistName") or "",
            "album": d.get("albumName") or "",
            "durationMs": int((d.get("duration") or 0) * 1000),
            "synced": d.get("syncedLyrics") or "",
            "plain": d.get("plainLyrics") or "",
        })
    return out


METHODS = {"lyrics.search": lyrics_search}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            fn = METHODS.get(req.get("method"))
            if fn is None:
                raise ValueError("unknown method: %s" % req.get("method"))
            resp = {"id": req["id"], "result": fn(req.get("params") or {})}
        except Exception as e:                                   # report, don't die
            resp = {"id": (req or {}).get("id") if isinstance(req, dict) else None,
                    "error": {"message": str(e)}}
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
