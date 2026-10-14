#!/usr/bin/env python3
"""Serve a verified Boards Manager package on localhost for private-repo installs.

Download the board ZIP while signed in to GitHub, then pass its path here.
Only the JSON index and matching ZIP are exposed, on 127.0.0.1 only.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    index = json.loads((root / "package_tinyptp_index.json").read_text())
    platform = index["packages"][0]["platforms"][0]
    archive = args.archive.read_bytes()
    if "SHA-256:" + hashlib.sha256(archive).hexdigest() != platform["checksum"]:
        parser.error("Archive hash does not match package_tinyptp_index.json")
    if len(archive) != int(platform["size"]):
        parser.error("Archive size does not match package_tinyptp_index.json")
    resources = {}

    class Handler(BaseHTTPRequestHandler):
        def do_HEAD(self):
            self.respond(False)

        def do_GET(self):
            self.respond(True)

        def respond(self, body):
            if self.path not in resources:
                self.send_error(404)
                return
            data, mime = resources[self.path]
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            if body:
                self.wfile.write(data)

    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        base = f"http://127.0.0.1:{server.server_port}"
        platform["url"] = base + "/" + platform["archiveFileName"]
        resources["/" + platform["archiveFileName"]] = (archive, "application/zip")
        resources["/package_tinyptp_index.json"] = (json.dumps(index, indent=2).encode(), "application/json")
        print("Additional Boards Manager URL: " + base + "/package_tinyptp_index.json", flush=True)
        print("Keep this process running during installation. Press Ctrl-C to stop.", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
