#!/usr/bin/env python3

import argparse
import functools
import http.server
import pathlib
import socketserver


class DevHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
    }

    def guess_type(self, path):
        if path.endswith(".wasm"):
            return "application/wasm"
        return super().guess_type(path)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("X-Content-Type-Options", "nosniff")
        # Dev server: defeat browser caching entirely so every reload reflects
        # whatever `make` just produced. `no-cache` only forces revalidation
        # (still returns 304 when mtime matches), which is confusing when you
        # are iterating on regenerated binaries (rootfs.cpio, rv32emu.wasm).
        self.send_header("Cache-Control", "no-store, max-age=0")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="rv32emu WebAssembly dev server"
    )
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--directory", default=".")
    args = parser.parse_args()

    directory = pathlib.Path(args.directory).resolve()
    handler = functools.partial(DevHTTPRequestHandler, directory=str(directory))

    with socketserver.TCPServer((args.bind, args.port), handler) as httpd:
        print(f"Serving {directory} at http://{args.bind}:{args.port}")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
