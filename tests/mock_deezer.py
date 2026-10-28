#!/usr/bin/env python3
"""A tiny stand-in for api.deezer.com so the online catalog can be tested offline.

    python3 tests/mock_deezer.py 8765 &
    LUMEN_DEEZER_URL=http://127.0.0.1:8765 ./build/lumen

Serves the same JSON shapes as the real API for: search (artist/track/album), artist (+top, albums, related),
album (+tracks), track, chart. Previews are a generated 30 s sine wave.
"""
import io, json, math, struct, sys, wave, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
BASE = "http://127.0.0.1:%d" % PORT

ARTISTS = {1: "Juice WRLD", 2: "Trippie Redd", 3: "Lil Uzi Vert", 4: "The Kid LAROI", 5: "Polo G"}
RELATED = {1: [2, 3, 4], 2: [1, 3], 3: [1, 2, 5], 4: [1, 5], 5: [4, 3]}


def png():
    w = h = 8
    raw = b"".join(b"\x00" + bytes([200, 80, 120] * w) for _ in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def wav(seconds=30, freq=440):
    buf = io.BytesIO()
    with wave.open(buf, "wb") as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(22050)
        f.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * freq * i / 22050))) for i in range(22050 * seconds)))
    return buf.getvalue()


def art(i):
    return {"id": i, "name": ARTISTS[i], "picture_big": BASE + "/img/%d.png" % i, "nb_fan": 1000000 - i * 1000, "nb_album": 4, "type": "artist"}


def album(aid, n):
    # per artist: n=1,2 albums, n=3 single, n=4 duplicate (explicit/clean) of n=2
    typ = {1: "album", 2: "album", 3: "single", 4: "album"}[n]
    title = {1: "First Light", 2: "Second Wind", 3: "Loose Single", 4: "Second Wind"}[n]
    return {"id": aid * 100 + n, "title": title, "cover_big": BASE + "/img.png", "release_date": "20%02d-01-01" % (15 + n),
            "record_type": typ, "explicit_lyrics": n == 4, "artist": {"id": aid, "name": ARTISTS[aid]}, "type": "album"}


def track(aid, n, k):
    al = album(aid, n)
    names = ["Lucid", "Burn", "Robbery", "Hear Me", "Wasted", "Legends"]
    return {"id": aid * 10000 + n * 100 + k, "title": "%s %s" % (names[(k + n) % 6], ARTISTS[aid].split()[0]),
            "duration": 180 + k, "rank": 900000 - aid * 1000 - n * 100 - k, "track_position": k, "disk_number": 1,
            "preview": BASE + "/p.wav", "artist": {"id": aid, "name": ARTISTS[aid]},
            "album": {k_: v for k_, v in al.items() if k_ in ("id", "title", "cover_big", "release_date")}, "type": "track"}


def page(items, q):
    limit = int(q.get("limit", ["25"])[0]); index = int(q.get("index", ["0"])[0])
    return {"data": items[index:index + limit], "total": len(items)}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, body, ctype="application/json"):
        if not isinstance(body, bytes): body = json.dumps(body).encode()
        self.send_response(200); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query); p = [x for x in u.path.split("/") if x]
        text = (q.get("q", [""])[0]).lower()
        if p == ["img.png"] or p[:1] == ["img"]: return self.send(png(), "image/png")
        if p == ["p.wav"]: return self.send(wav(), "audio/wav")
        if p[:2] == ["search", "artist"]: return self.send(page([art(i) for i, n in ARTISTS.items() if all(w in n.lower() for w in text.split())], q))
        if p[:2] == ["search", "album"]: return self.send(page([album(i, 1) for i, n in ARTISTS.items() if text.split()[0] in n.lower()], q))
        if p == ["search"]:
            # supports plain text and artist:"x" track:"y"
            t = text.replace('artist:"', "").replace('" track:"', " ").replace('"', "")
            out = [track(i, n, k) for i in ARTISTS for n in (1, 2) for k in (1, 2, 3)]
            return self.send(page([x for x in out if all(w in (x["title"] + " " + x["artist"]["name"]).lower() for w in t.split())], q))
        if p[:1] == ["artist"]:
            i = int(p[1])
            if i not in ARTISTS: return self.send({"error": {"type": "DataException", "message": "no data", "code": 800}})
            if len(p) == 2: return self.send(art(i))
            if p[2] == "top": return self.send(page([track(i, 1, k) for k in range(1, 4)] + [track(i, 2, k) for k in range(1, 4)], q))
            if p[2] == "albums": return self.send(page([album(i, n) for n in (1, 2, 3, 4)], q))
            if p[2] == "related": return self.send(page([art(r) for r in RELATED[i]], q))
        if p[:1] == ["album"]:
            aid = int(p[1]); i, n = aid // 100, aid % 100
            if len(p) == 2: return self.send(album(i, n))
            ts = [track(i, n, k) for k in range(1, 5)]
            for t in ts: t.pop("album")
            return self.send(page(ts, q))
        if p[:1] == ["track"]:
            tid = int(p[1]); return self.send(track(tid // 10000, (tid // 100) % 100, tid % 100))
        if p[:2] == ["chart", "0"]:
            if p[2] == "artists": return self.send(page([art(i) for i in ARTISTS], q))
            if p[2] == "tracks": return self.send(page([track(i, 1, 1) for i in ARTISTS], q))
            if p[2] == "albums": return self.send(page([album(i, 1) for i in ARTISTS], q))
        self.send_response(404); self.end_headers()


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
