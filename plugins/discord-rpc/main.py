#!/usr/bin/env python3
"""Lumen 'presence' extension: Spotify-style Discord Rich Presence through EquiPresence.

Lumen sends the whole now-playing state with every call (it is stateless on this side), so the process
can be stopped when idle and restarted for the next song without losing anything.

  presence.update  params: title, artist, album, cover (https URL or ""), durationMs, positionMs, playing
  presence.clear   params: none

EquiPresence (a Vencord/Equicord plugin) listens on 127.0.0.1:3210 and publishes the activity to Discord:
    POST /api/v1/activity   Authorization: Bearer <contents of ~/.equipresence/api-token>

Settings, all optional, as environment variables of the Lumen process:
    EQUIPRESENCE_URL         default http://127.0.0.1:3210
    EQUIPRESENCE_TOKEN_FILE  default ~/.equipresence/api-token
    LUMEN_RPC_BUTTON_LABEL   default "Listen for free on Lumen"
    LUMEN_RPC_BUTTON_URL     default https://github.com/donskyblock/lumen
    LUMEN_RPC_FALLBACK_IMAGE image shown when a song has no web cover (local files); default none
"""
import http.client
import json
import os
import sys
import threading
import time
from urllib.parse import urlsplit

BASE = os.environ.get("EQUIPRESENCE_URL", "http://127.0.0.1:3210")
TOKEN_FILE = os.path.expanduser(os.environ.get("EQUIPRESENCE_TOKEN_FILE", "~/.equipresence/api-token"))
BUTTON_LABEL = os.environ.get("LUMEN_RPC_BUTTON_LABEL", "Listen for free on Lumen")
BUTTON_URL = os.environ.get("LUMEN_RPC_BUTTON_URL", "https://github.com/donskyblock/lumen")
FALLBACK_IMAGE = os.environ.get("LUMEN_RPC_FALLBACK_IMAGE", "")
OUT_LOCK = threading.Lock()


def emit(obj):
    with OUT_LOCK:
        sys.stdout.write(json.dumps(obj) + "\n")
        sys.stdout.flush()


def token():
    try:
        with open(TOKEN_FILE, "r", encoding="utf-8") as f:
            t = f.read().strip()
    except OSError:
        raise RuntimeError("EquiPresence isn't set up: no token at %s. Enable the EquiPresence plugin in Discord first." % TOKEN_FILE)
    if not t:
        raise RuntimeError("The EquiPresence token file is empty.")
    return t


def request(method, path, payload):
    """One call to EquiPresence. Returns (status, body text). The token is never logged or put in errors."""
    u = urlsplit(BASE)
    conn = http.client.HTTPConnection(u.hostname or "127.0.0.1", u.port or 3210, timeout=4)
    try:
        body = json.dumps(payload) if payload is not None else None
        conn.request(method, path, body=body, headers={
            "Authorization": "Bearer " + token(),
            "Content-Type": "application/json",
        })
        r = conn.getresponse()
        return r.status, r.read(2048).decode("utf-8", "replace")
    except OSError:
        raise RuntimeError("Can't reach EquiPresence at %s. Is Discord open with the EquiPresence plugin enabled?" % BASE)
    finally:
        conn.close()


def clip(s, n=128):
    """Discord wants 2..128 characters per text line."""
    s = " ".join((s or "").split())
    if len(s) > n:
        s = s[: n - 1] + "\u2026"
    return s


def build(p):
    title = clip(p.get("title")) or "Unknown song"
    artist = clip(p.get("artist")) or "Unknown artist"
    album = clip(p.get("album"))
    now = time.time()
    start = int(now - max(0, int(p.get("positionMs") or 0)) / 1000.0)
    duration = int(p.get("durationMs") or 0)
    act = {
        "type": "LISTENING",
        "name": artist,                   # the header reads "Listening to <artist>"
        "details": title,                 # first line: the song
        "state": artist,                  # second line: the artist
        "startTimestamp": start,
    }
    if duration > 0:
        act["endTimestamp"] = start + duration // 1000          # gives the progress bar, like Spotify
    cover = p.get("cover") or ""
    if cover.startswith(("http://", "https://")):
        act["largeImage"] = cover
    elif FALLBACK_IMAGE:
        act["largeImage"] = FALLBACK_IMAGE
    if "largeImage" in act:
        act["largeText"] = album or title      # hover text over the artwork: the album, like Spotify
    act["buttons"] = [{"label": BUTTON_LABEL, "url": BUTTON_URL}]
    return act


def update(p):
    if not p.get("playing"):
        return clear({})
    act = build(p)
    status, body = request("POST", "/api/v1/activity", act)
    if status >= 300:
        raise RuntimeError("EquiPresence answered HTTP %d: %s" % (status, body[:120]))
    return {"ok": True}


def clear(_p):
    # The guide lists no confirmed clear route; {"clear": true} is its candidate, DELETE the usual alternative.
    status, body = request("POST", "/api/v1/activity", {"clear": True})
    if status >= 300:
        status, body = request("DELETE", "/api/v1/activity", None)
    if status >= 300:
        raise RuntimeError("EquiPresence has no working clear route (HTTP %d)." % status)
    return {"ok": True}


METHODS = {"presence.update": update, "presence.clear": clear}


def handle(req):
    try:
        fn = METHODS.get(req.get("method"))
        if not fn:
            raise RuntimeError("Unknown method: %s" % req.get("method"))
        resp = {"id": req["id"], "result": fn(req.get("params") or {})}
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
    handle(req)          # in order: a quick update followed by a clear must reach Discord in that order
