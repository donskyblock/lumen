#!/usr/bin/env python3
"""Tiny stand-in for lrclib.net so the lyrics path can be tested offline."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

ROWS = [
    {"trackName": "Signal 11", "artistName": "Someone Else", "albumName": "X", "duration": 40,
     "plainLyrics": "wrong song", "syncedLyrics": ""},
    {"trackName": "Signal 1 (Remastered 2021)", "artistName": "Aurora Test", "albumName": "Night Signals", "duration": 40,
     "plainLyrics": "plain fallback",
     "syncedLyrics": "[ar:Aurora Test]\n[00:01.00]First light on the wire\n[00:05.50]Second line comes in\n[00:10.00]\n[00:12.25]Carrying the signal home\n[00:20.00]Static settles down\n[00:30.00]Last line before the end"},
]


class H(BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps(ROWS).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


HTTPServer(("127.0.0.1", 8765), H).serve_forever()
