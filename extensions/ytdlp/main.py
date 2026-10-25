#!/usr/bin/env python3
"""Lumen 'source' extension that shells out to a user-installed yt-dlp.

yt-dlp is NOT bundled: you install and update it yourself. It runs as a
short-lived child process per request, so this extension stays tiny while idle.
"""
import json
import shutil
import subprocess
import sys


def run(args, timeout):
    exe = shutil.which("yt-dlp")
    if not exe:
        raise RuntimeError("yt-dlp isn't installed. Install it (Arch: pacman -S yt-dlp) and try again.")
    p = subprocess.run([exe] + args, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        lines = [l for l in p.stderr.strip().splitlines() if l.strip()]
        raise RuntimeError(lines[-1] if lines else "yt-dlp failed")
    return p.stdout


def search(p):
    q = p.get("query", "").strip()
    limit = max(4, int(p.get("limit", 10)))
    half = max(2, limit // 2)
    per_site = []
    errors = []
    for prefix, label in (("ytsearch", "YouTube"), ("scsearch", "SoundCloud")):
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
            errors.append(str(ex))
        per_site.append(rows)
    if not any(per_site) and errors:
        raise RuntimeError(errors[0])
    out = []                                   # interleave so neither site buries the other
    for i in range(max(len(r) for r in per_site)):
        for rows in per_site:
            if i < len(rows):
                out.append(rows[i])
    return out[:limit]


def resolve(p):
    j = json.loads(run(["-f", "bestaudio/best", "-J", "--no-playlist", "--no-warnings", p["id"]], 40))
    url = j.get("url")
    if not url:
        fmts = [f for f in j.get("formats", []) if f.get("url") and f.get("vcodec") in (None, "none")]
        fmts.sort(key=lambda f: f.get("abr") or 0)
        url = fmts[-1]["url"] if fmts else None
    if not url:
        raise RuntimeError("No playable audio stream was found.")
    codec = j.get("acodec") or j.get("ext") or ""
    return {
        "url": url,
        "headers": j.get("http_headers") or {},
        "format": codec.split(".")[0],
        "bitrateKbps": int(j.get("abr") or 0),
        "lossless": False,
    }


METHODS = {"source.search": search, "source.resolve": resolve}

for line in sys.stdin:
    if not line.strip():
        continue
    req = {}
    try:
        req = json.loads(line)
        resp = {"id": req["id"], "result": METHODS[req["method"]](req.get("params") or {})}
    except Exception as e:
        resp = {"id": req.get("id"), "error": {"message": str(e)}}
    sys.stdout.write(json.dumps(resp) + "\n")
    sys.stdout.flush()
