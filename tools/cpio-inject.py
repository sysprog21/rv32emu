#!/usr/bin/env python3
"""Inject a file into a cpio newc archive without extracting it.

The Linux kernel's initramfs unpacker needs character device nodes
(/dev/console etc.) to be present in the cpio. A naive extract+repack as a
non-root user silently drops those, leaving the guest without a console for
the initial /init process. This tool walks the cpio byte stream and splices a
new entry in just before TRAILER!!!, preserving every other entry verbatim.

Usage:
    cpio-inject.py <input.cpio> <output.cpio> <archive_path> <source_file>

archive_path is the path the new entry will have inside the cpio
(e.g. etc/init.d/S99automount). source_file is read from the host filesystem
and its mode is preserved.
"""

from __future__ import annotations

import os
import stat
import sys
import time

NEWC_MAGIC = b"070701"
NEWC_HEADER_LEN = 110
BLOCK_SIZE = 512


def _read_hex(buf: bytes, off: int) -> int:
    return int(buf[off : off + 8], 16)


def _pad4(n: int) -> int:
    return (4 - (n & 3)) & 3


def _parse_entry(buf: bytes, off: int):
    """Return (entry_bytes, name_str, new_offset)."""
    if off + NEWC_HEADER_LEN > len(buf):
        raise ValueError(f"truncated header at offset {off}")
    if buf[off : off + 6] != NEWC_MAGIC:
        raise ValueError(
            f"bad magic at offset {off}: {buf[off : off + 6]!r}; expected {NEWC_MAGIC!r}"
        )
    filesize = _read_hex(buf, off + 54)
    namesize = _read_hex(buf, off + 94)
    name_start = off + NEWC_HEADER_LEN
    name_end = name_start + namesize
    data_start = name_end + _pad4(name_end - off)
    data_end = data_start + filesize
    new_off = data_end + _pad4(data_end - off)
    name = buf[name_start : name_end - 1].decode("ascii", errors="replace")
    return buf[off:new_off], name, new_off


def _build_entry(
    archive_path: str, data: bytes, mode: int, mtime: int
) -> bytes:
    name_bytes = archive_path.encode("ascii") + b"\0"
    namesize = len(name_bytes)
    filesize = len(data)
    header = (
        NEWC_MAGIC
        + b"%08x" % 0  # ino
        + b"%08x" % mode  # mode (file type | perms)
        + b"%08x" % 0  # uid
        + b"%08x" % 0  # gid
        + b"%08x" % 1  # nlink
        + b"%08x" % mtime
        + b"%08x" % filesize
        + b"%08x" % 0  # devmajor
        + b"%08x" % 0  # devminor
        + b"%08x" % 0  # rdevmajor
        + b"%08x" % 0  # rdevminor
        + b"%08x" % namesize
        + b"%08x" % 0  # check
    )
    block = header + name_bytes
    block += b"\0" * _pad4(len(block))
    block += data
    block += b"\0" * _pad4(len(block))
    return block


def main() -> int:
    if len(sys.argv) != 5:
        sys.stderr.write(__doc__)
        return 2
    inp, outp, archive_path, source = sys.argv[1:]
    with open(inp, "rb") as f:
        cpio = f.read()
    with open(source, "rb") as f:
        payload = f.read()
    st = os.stat(source)
    # Force regular-file type bit; preserve perm bits from source.
    mode = stat.S_IFREG | (st.st_mode & 0o7777)

    out: list[bytes] = []
    off = 0
    inserted = False
    saw_target = False
    while off < len(cpio):
        try:
            entry, name, new_off = _parse_entry(cpio, off)
        except ValueError:
            # Trailing zero padding to the BLOCK_SIZE boundary — stop parsing.
            break
        if name == archive_path:
            # Replace existing entry with the new one (preserves position).
            saw_target = True
            out.append(
                _build_entry(archive_path, payload, mode, int(time.time()))
            )
            inserted = True
        elif name == "TRAILER!!!" and not saw_target and not inserted:
            # Splice the new entry immediately before the trailer.
            out.append(
                _build_entry(archive_path, payload, mode, int(time.time()))
            )
            out.append(entry)
            inserted = True
        else:
            out.append(entry)
        off = new_off
    if not inserted:
        sys.stderr.write(f"error: TRAILER!!! not found in {inp}\n")
        return 1

    blob = b"".join(out)
    # Pad final blob to BLOCK_SIZE (cpio convention; aids tools like `dd`).
    blob += b"\0" * ((BLOCK_SIZE - (len(blob) % BLOCK_SIZE)) % BLOCK_SIZE)
    with open(outp, "wb") as f:
        f.write(blob)
    return 0


if __name__ == "__main__":
    sys.exit(main())
