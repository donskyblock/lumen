#!/usr/bin/env python3
"""Example Lumen 'source' extension. Shows source.search and source.resolve."""
import json
import os
import sys

ROOT = os.environ.get("LUMEN_DEMO_DIR", os.path.expanduser("~/Music"))
EXT = (".flac", ".mp3", ".wav", ".ogg", ".opus", ".m4a")


def search(p):
    q = p.get("query", "").lower()
    out = []
    for dirpath, _, files in os.walk(ROOT):
        for f in files:
            if f.lower().endswith(EXT) and q in f.lower():
                path = os.path.join(dirpath, f)
                out.append({"id": path, "title": os.path.splitext(f)[0], "artist": os.path.basename(dirpath),
                            "album": "Demo", "durationMs": 0})
                if len(out) >= p.get("limit", 10):
                    return out
    return out


def resolve(p):
    path = p["id"]
    return {"url": "file://" + path, "format": os.path.splitext(path)[1][1:], "lossless": path.endswith((".flac", ".wav"))}


METHODS = {"source.search": search, "source.resolve": resolve}

for line in sys.stdin:
    if not line.strip():
        continue
    req = json.loads(line)
    try:
        resp = {"id": req["id"], "result": METHODS[req["method"]](req.get("params") or {})}
    except Exception as e:
        resp = {"id": req.get("id"), "error": {"message": str(e)}}
    sys.stdout.write(json.dumps(resp) + "\n")
    sys.stdout.flush()
