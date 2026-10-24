#!/usr/bin/env python3
"""Generate a common source/configuration fingerprint for the AOT C ABI."""

import hashlib
from pathlib import Path
import sys


def main():
    output = Path(sys.argv[1])
    digest = hashlib.sha256()
    sources = sorted(Path("src").rglob("*.h")) + sorted(
        Path("src").rglob("*.c")
    )
    # Generated device trees do not define the collector's C ABI.
    for source in sources:
        if source.name == "minimal_dtb.h":
            continue
        digest.update(str(source).encode() + b"\0")
        digest.update(source.read_bytes())
    for flag in sys.argv[2:]:
        digest.update(flag.encode() + b"\0")
    text = (
        f"#define RV_AOT_ABI (0x{digest.hexdigest()[:16]}ULL ^ "
        "((uint64_t) sizeof(riscv_t) << 32) ^ "
        "offsetof(riscv_t, X) ^ (offsetof(riscv_t, io) << 16))\n"
    )
    if not output.exists() or output.read_text() != text:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text)


if __name__ == "__main__":
    main()
