#!/usr/bin/env python3
"""Capture a Chromium CPU profile and opt-in T1C counters from a Wasm demo."""

import argparse
import asyncio
import base64
from collections import defaultdict
import functools
import http.server
import json
from pathlib import Path
import threading
import time

from playwright.async_api import TimeoutError as PlaywrightTimeoutError
from playwright.async_api import async_playwright


# Not tools/dev-server.py's handler: its Cache-Control: no-store made headless
# Chromium's renderer crash (SIGSEGV) during long runs, even with T1C disabled.
class ProfileHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


def summarize(profile):
    """Attribute each sampling interval to its leaf frame, without double counting.

    timeDeltas[i] is the gap that ends at sample i, so a sample is credited
    with the gap that follows it, as Chromium DevTools does.
    """
    nodes = {node["id"]: node["callFrame"] for node in profile["nodes"]}
    weights = defaultdict(int)
    for sample, delta in zip(profile["samples"], profile["timeDeltas"][1:]):
        frame = nodes[sample]
        weights[(frame["functionName"], frame["url"])] += delta
    total = sum(weights.values())
    return [
        {
            "function": name,
            "url": url,
            "milliseconds": round(weight / 1000, 3),
            "percent": round(100 * weight / total, 2) if total else 0,
        }
        for (name, url), weight in sorted(
            weights.items(), key=lambda item: item[1], reverse=True
        )
    ]


async def capture(args, url):
    console = []
    output = []
    errors = []
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch()
        try:
            page = await browser.new_page()

            def log_console(message):
                console.append(message.text)
                # The user page echoes guest stdout through console.log.
                if message.type == "log" and args.mode == "user":
                    output.append(message.text)

            page.on("console", log_console)
            page.on("pageerror", lambda error: errors.append(str(error)))
            await page.goto(url, wait_until="domcontentloaded")
            # Wait for the page the COI service worker reloads with isolation:
            # driving the first one while it reloads crashed the renderer.
            # Exports and FS exist before the runtime finishes initializing,
            # and calling main that early crashes the renderer too. Newer
            # Emscripten releases drop Module.calledRun, so also accept the
            # pages' own signals: user.html sets emulatorReady and
            # system.html enables its boot button.
            await page.wait_for_function(
                "crossOriginIsolated && typeof Module !== 'undefined' && "
                "(!!Module.calledRun || "
                "(typeof emulatorReady !== 'undefined' && emulatorReady) || "
                "!!document.querySelector('#runSysButton:not([disabled])'))"
            )
            if args.mode == "user":
                await page.evaluate(
                    "data => Module.FS.writeFile('/profile.elf', "
                    "Uint8Array.from(atob(data), c => c.charCodeAt(0)))",
                    base64.b64encode(args.guest.read_bytes()).decode(),
                )
            session = await page.context.new_cdp_session(page)
            if not args.benchmark:
                await session.send("Profiler.enable")
                await session.send(
                    "Profiler.setSamplingInterval", {"interval": 1000}
                )
            if args.mode == "system":
                # The system page writes the UART straight to xterm, and
                # #terminal is xterm's DOM; record what it is given instead.
                await page.wait_for_function("term !== null")
                await page.evaluate(
                    """() => {
                    window.profileOutput = '';
                    const write = term.write.bind(term);
                    term.write = (data, callback) => {
                        window.profileOutput += data;
                        return write(data, callback);
                    };
                }"""
                )
                # The UI may download the kernel/rootfs before creating the VM.
                await page.locator("#runSysButton").click()
                await page.wait_for_function(
                    "!!Module._indirect_rv_alive()", timeout=60000
                )
            if not args.benchmark:
                await session.send("Profiler.start")
            start = time.monotonic()
            if args.mode == "user":
                await page.evaluate("Module.run_user('/profile.elf')")
            # Poll inside the page, so the elapsed time is not rounded up to
            # a host-side polling interval.
            try:
                await page.wait_for_function(
                    "!Module._indirect_rv_alive()",
                    polling=10,
                    timeout=args.seconds * 1000,
                )
                completed = True
            except PlaywrightTimeoutError:
                completed = False
            elapsed = time.monotonic() - start
            profile = None
            if not args.benchmark:
                profile = (await session.send("Profiler.stop"))["profile"]
            if not completed:
                await page.evaluate("Module._indirect_rv_halt()")
                await page.wait_for_function("!Module._indirect_rv_alive()")
            if args.mode == "system":
                output = (await page.evaluate("profileOutput")).splitlines()
            marker = "WASM T1C profile: "
            summary = {
                "browser": browser.version,
                "url": url,
                "mode": args.mode,
                "guest": str(args.guest) if args.guest else None,
                "seconds": round(elapsed, 3),
                "completed": completed,
                "frames": summarize(profile) if profile else [],
                "t1c": [
                    json.loads(message.split(marker, 1)[1])
                    for message in console
                    if marker in message
                ],
                "errors": errors,
                "terminal": output,
            }
            args.output.parent.mkdir(parents=True, exist_ok=True)
            base = str(args.output)
            if profile:
                Path(base + ".cpuprofile").write_text(json.dumps(profile))
            Path(base + ".json").write_text(
                json.dumps(summary, indent=2) + "\n"
            )
            Path(base + ".log").write_text("\n".join(console) + "\n")
            for frame in summary["frames"][:15]:
                print(f"{frame['percent']:6.2f}%  {frame['function']}")
            for stats in summary["t1c"]:
                print("T1C:", json.dumps(stats, sort_keys=True))
            print(f"Elapsed: {elapsed:.3f}s; completed: {completed}")
            print("Profile saved:", args.output)
            if errors:
                raise RuntimeError("; ".join(errors))
        finally:
            await browser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--url", help="full demo URL; otherwise serve --directory"
    )
    parser.add_argument("--directory", type=Path, default=Path("demo"))
    parser.add_argument("--mode", choices=("user", "system"), default="user")
    parser.add_argument(
        "--guest", type=Path, help="local guest ELF (user mode)"
    )
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument(
        "--benchmark",
        action="store_true",
        help="measure elapsed time without CPU sampling; inspect completed in JSON",
    )
    parser.add_argument(
        "--output", type=Path, default=Path("build/wasm-profile")
    )
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if args.mode == "user" and not args.guest:
        parser.error("--guest is required in user mode")
    server = None
    try:
        url = args.url
        if not url:
            handler = functools.partial(
                ProfileHandler, directory=args.directory
            )
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            url = f"http://127.0.0.1:{server.server_port}/{args.mode}/"
        asyncio.run(capture(args, url))
    finally:
        if server:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
