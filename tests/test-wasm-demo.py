#!/usr/bin/env python3
"""Browser regressions; requires Playwright and its Chromium browser.

Serves the demo staged by `make prepare-web` without isolation headers, so the
test exercises the same service-worker bootstrap as GitHub Pages:
    python3 tests/test-wasm-demo.py --mode user
"""

import argparse
import asyncio
import functools
import http.server
import struct
import threading

from playwright.async_api import async_playwright


def loop_elf(instructions):
    """Build a minimal RV32 ELF with one executable segment and no sections."""
    code = struct.pack("<" + "I" * len(instructions), *instructions)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        ident,
        2,
        243,
        1,
        0x10000,
        52,
        0,
        0,
        52,
        32,
        1,
        40,
        0,
        0,
    )
    segment = struct.pack(
        "<IIIIIIII", 1, 84, 0x10000, 0x10000, len(code), len(code), 5, 4
    )
    return list(header + segment + code)


async def check_demo(browser, url, mode):
    # A fresh context ensures the COI service worker has not been installed.
    context = await browser.new_context()
    page = await context.new_page()
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    await page.goto(f"{url.rstrip('/')}/{mode}/")
    await page.wait_for_function(
        "() => crossOriginIsolated && typeof Module !== 'undefined' && "
        "typeof Module._indirect_rv_halt === 'function'"
    )
    if mode == "user":
        await page.locator("select").select_option("hello.elf")
        await page.locator("#runButton").click()
        await page.wait_for_function(
            "() => document.getElementById('statusText').textContent "
            "=== 'Completed'"
        )
        await page.wait_for_function(
            "() => document.getElementById('terminal').textContent "
            ".includes('Hello World!')"
        )
        # Each loop must yield to the browser and honor Stop, even when the
        # learned edge targets its own block.
        loops = [
            # j .
            [0x0000006F],
            # li t0, -1; 1: addi t0, t0, -1; bnez t0, 1b
            [0xFFF00293, 0xFFF28293, 0xFE029EE3],
            # li s0, -1; 1: c.addi s0, -1; c.bnez s0, 1b (compressed branches
            # follow their learned edge in the tail-call chain)
            [0xFFF00413, 0xFC7D147D],
            # auipc t0, 0; addi t0, t0, 8; 1: jr t0 (re-enters through the
            # branch history table; t0 is set first so it cannot be folded)
            [0x00000297, 0x00828293, 0x00028067],
        ]
        for instructions in loops:
            await page.evaluate(
                """data => {
                    Module.FS.writeFile('/yield.elf', new Uint8Array(data));
                    Module.run_user('/yield.elf');
                }""",
                loop_elf(instructions),
            )
            await page.wait_for_timeout(250)
            assert await page.evaluate("!!Module._indirect_rv_alive()")
            await page.evaluate("Module._indirect_rv_halt()")
            await page.wait_for_function("() => !Module._indirect_rv_alive()")
    else:
        await page.locator("#runSysButton").click()
        await page.wait_for_function(
            "() => document.getElementById('terminal').textContent "
            ".includes('buildroot login:')",
            timeout=120000,
        )
        output = await page.locator("#terminal").inner_text()
        assert "Segmentation fault" not in output, output
        assert "Kernel panic" not in output, output
    assert not errors, errors
    await context.close()
    print(f"{mode}: browser smoke test passed", flush=True)


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


async def main(args):
    url = args.url
    if url is None:
        handler = functools.partial(QuietHandler, directory=args.directory)
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        url = f"http://127.0.0.1:{server.server_port}"
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch()
        modes = ["user", "system"] if args.mode == "both" else [args.mode]
        for mode in modes:
            # A broken dispatch loop can block every page operation. Bound
            # the entire test, including evaluate(), rather than only waits.
            await asyncio.wait_for(check_demo(browser, url, mode), 180)
        await browser.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--url", help="test an already running server instead of serving"
    )
    parser.add_argument("--directory", default="demo")
    parser.add_argument(
        "--mode", choices=["user", "system", "both"], default="both"
    )
    asyncio.run(main(parser.parse_args()))
