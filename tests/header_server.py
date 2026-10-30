#!/usr/bin/env python3
"""Serves one file at /t.flac but only to requests carrying 'X-Test: yes'."""
# comment added to test webhook bot lmfao
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
FILE = sys.argv[1]
class H(BaseHTTPRequestHandler):
    def do_GET(self):
        ok = self.headers.get("X-Test") == "yes"
        open("/tmp/header_server.log", "a").write("GET %s X-Test=%s\n" % (self.path, self.headers.get("X-Test")))
        if not ok:
            self.send_response(403); self.end_headers(); return
        data = open(FILE, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", "audio/flac")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Accept-Ranges", "none")
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, *a): pass
HTTPServer(("127.0.0.1", 8770), H).serve_forever()
