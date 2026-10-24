#!/usr/bin/env python3
"""Compile rv32emu's collected LLVM regions into a reusable native library."""

import argparse
import ctypes
import functools
import hashlib
import shlex
import stat
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import re
import tempfile


@functools.lru_cache(maxsize=None)
def llvm_libraries(llvm_config):
    """Return the shared LLVM libraries that llvm-config links."""
    return tuple(
        shlex.split(
            subprocess.check_output(
                [llvm_config, "--link-shared", "--libfiles"], text=True
            )
        )
    )


def private_cache_dir(path):
    """Accept an object cache only the current user can write.

    Cached objects are linked into the library without being rebuilt, so a
    directory that others can write to could inject native code.
    """
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    path = path.resolve()
    if not stat.S_ISDIR(check_private(path).st_mode):
        raise ValueError(f"object cache is not a directory: {path}")
    return path


def check_private(path):
    """Require current-user ownership and no group or other write access."""
    info = path.stat()
    if info.st_uid != os.getuid() or info.st_mode & (
        stat.S_IWGRP | stat.S_IWOTH
    ):
        raise ValueError(
            f"object cache must be owned by you and not writable by group "
            f"or others: {path}"
        )
    return info


@functools.lru_cache(maxsize=None)
def llvm_c(llvm_config):
    """Load and bind the LLVM-C entry points once per llvm-config."""
    library = ctypes.CDLL(llvm_libraries(llvm_config)[0])
    pointer = ctypes.c_void_p
    callback_type = ctypes.CFUNCTYPE(None, pointer, pointer)
    signatures = {
        "LLVMContextCreate": (pointer, []),
        "LLVMContextDispose": (None, [pointer]),
        "LLVMContextSetDiagnosticHandler": (
            None,
            [pointer, callback_type, pointer],
        ),
        "LLVMGetDiagInfoDescription": (pointer, [pointer]),
        "LLVMCreateMemoryBufferWithContentsOfFile": (
            ctypes.c_int,
            [ctypes.c_char_p, ctypes.POINTER(pointer), ctypes.POINTER(pointer)],
        ),
        "LLVMDisposeMemoryBuffer": (None, [pointer]),
        "LLVMDisposeMessage": (None, [pointer]),
        "LLVMParseBitcodeInContext2": (
            ctypes.c_int,
            [pointer, pointer, ctypes.POINTER(pointer)],
        ),
        "LLVMGetNamedFunction": (pointer, [pointer, ctypes.c_char_p]),
        "LLVMSetValueName2": (
            None,
            [pointer, ctypes.c_char_p, ctypes.c_size_t],
        ),
        "LLVMWriteBitcodeToFile": (ctypes.c_int, [pointer, ctypes.c_char_p]),
        "LLVMDisposeModule": (None, [pointer]),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(library, name)
        function.restype, function.argtypes = result, arguments
    return library, callback_type


def rename_region(region, destination, old_name, new_name, llvm_config):
    """Use LLVM-C to rename conflicting page graphs without rewriting IR."""
    library, callback_type = llvm_c(llvm_config)
    pointer = ctypes.c_void_p
    diagnostics = []

    @callback_type
    def diagnostic_handler(info, opaque):
        description = library.LLVMGetDiagInfoDescription(info)
        if description:
            diagnostics.append(
                ctypes.string_at(description).decode(errors="replace")
            )
            library.LLVMDisposeMessage(description)

    context = library.LLVMContextCreate()
    library.LLVMContextSetDiagnosticHandler(context, diagnostic_handler, None)
    buffer, module, message = pointer(), pointer(), pointer()
    try:
        if library.LLVMCreateMemoryBufferWithContentsOfFile(
            os.fsencode(region), ctypes.byref(buffer), ctypes.byref(message)
        ):
            raise ValueError(f"cannot read region: {region}")
        if library.LLVMParseBitcodeInContext2(
            context, buffer, ctypes.byref(module)
        ):
            raise ValueError(
                f"cannot parse region {region}: {'; '.join(diagnostics)}"
            )
        function = library.LLVMGetNamedFunction(module, old_name.encode())
        if not function:
            raise ValueError(f"missing entry {old_name}: {region}")
        library.LLVMSetValueName2(function, new_name.encode(), len(new_name))
        if library.LLVMWriteBitcodeToFile(module, os.fsencode(destination)):
            raise ValueError(f"cannot write merged region: {destination}")
    finally:
        if message:
            library.LLVMDisposeMessage(message)
        if module:
            library.LLVMDisposeModule(module)
        if buffer:
            library.LLVMDisposeMemoryBuffer(buffer)
        library.LLVMContextDispose(context)


def compile_profile(
    profiles,
    output,
    llvm_config,
    level,
    jobs,
    cache_dir=None,
    profile_generate=False,
    profile_use=None,
    profile_runtime=None,
    thin_lto=False,
):
    identity, producer, conventions, regions = None, 0, set(), []
    for directory in profiles:
        words = (directory / "manifest").read_text().split()
        if len(words) != 8 or any(len(word) != 16 for word in words):
            raise ValueError("invalid or incomplete collection manifest")
        found = [int(word, 16) for word in words[:6]]
        if found[0] != 3:
            raise ValueError("unsupported collection format")
        if identity is not None and found != identity:
            raise ValueError("cannot merge profiles with different identities")
        identity = found
        version = int(words[6], 16)
        if not 19 <= version <= 23:
            raise ValueError("unsupported producer LLVM version")
        producer = max(producer, version)
        conventions.add(int(words[7], 16))
        regions.extend(sorted(directory.glob("*.bc")))
    if not regions:
        raise ValueError("no collected regions")
    # Matching llvm-config keeps the producer, linker, and clang bitcode readers
    # on the same LLVM installation. subprocess uses argv, never shell strings.
    bindir = Path(
        subprocess.check_output([llvm_config, "--bindir"], text=True).strip()
    )
    compiler = int(
        subprocess.check_output([llvm_config, "--version"], text=True).split(
            "."
        )[0]
    )
    if not 19 <= producer <= compiler <= 23:
        raise ValueError(
            "LLVM compiler must be 19–23 and at least the producer version"
        )
    # The collector records the calling convention of its regions
    # (T2C_CALL_CONV in src/t2c.c): C, or preserve_none, which the emulator
    # enters through a trampoline added here.
    if len(conventions) != 1 or not conventions <= {0, 21}:
        raise ValueError("profiles disagree on or use an unknown convention")
    preserve_none = conventions == {21}
    profile_flags = []
    codegen_flags, linker_flags = [], []
    if thin_lto:
        linker = bindir / "ld.lld"
        if not linker.is_file():
            raise ValueError("--thin-lto requires ld.lld beside llvm-config")
        codegen_flags = ["-flto=thin"]
        linker_flags = [
            f"-fuse-ld={linker}",
            f"-Wl,--thinlto-jobs={jobs}",
        ]
    if profile_generate:
        profile_flags.append("-fprofile-generate")
        # IR PGO records its CFG before SLP. Skip that costly late pass for
        # training artifacts; production code retains the complete pipeline.
        codegen_flags.append("-fno-slp-vectorize")
    if profile_use:
        profile_use = profile_use.resolve()
        profile_flags.append(f"-fprofile-use={profile_use}")
    if profile_runtime:
        profile_runtime = profile_runtime.resolve()
        if not profile_generate or not profile_runtime.is_file():
            raise ValueError(
                "--profile-runtime requires generation and an existing archive"
            )
    cache_dir = private_cache_dir(cache_dir or profiles[0] / ".objects")
    if thin_lto:
        linker_flags.append(f"-Wl,--thinlto-cache-dir={cache_dir / 'thinlto'}")
    tool_hash = hashlib.sha256(Path(__file__).read_bytes())
    # Both tools that produce a cached object: llvm-link merges the regions
    # and clang compiles the result.
    for tool in ("clang", "llvm-link"):
        tool_hash.update((bindir / tool).read_bytes())
    tool_hash.update(subprocess.check_output([llvm_config, "--version"]))
    # clang's code generation lives in libLLVM and libclang-cpp, which a
    # rebuild can replace without changing the version string.
    libdir = Path(
        subprocess.check_output([llvm_config, "--libdir"], text=True).strip()
    )
    for library in (
        *llvm_libraries(llvm_config),
        *sorted(libdir.glob("libclang-cpp.so*")),
    ):
        info = os.stat(library)
        tool_hash.update(
            f"{library}\0{info.st_size}\0{info.st_mtime_ns}".encode()
        )
    tool_hash.update("\0".join(profile_flags).encode())
    tool_hash.update("\0".join(codegen_flags).encode())
    if profile_use:
        tool_hash.update(hashlib.sha256(profile_use.read_bytes()).digest())
    tool_identity = tool_hash.digest()
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="rv32emu-aot-", dir=output.parent
    ) as temporary:
        work = Path(temporary)
        metadata = work / "identity.c"
        values = ", ".join(f"0x{value:016x}ULL" for value in identity)
        # The index layout comes from the emulator's own header.
        declarations = [
            '#include "aot_entry.h"\n',
            f"const unsigned long long rv32_aot_identity[6] = {{{values}}};\n",
        ]
        declarations.append(
            "typedef void (*rv32_entry)(struct riscv_internal *);\n"
        )
        convention = ""
        if preserve_none:
            convention = "__attribute__((preserve_none)) "
            declarations.append(
                "typedef void (__attribute__((preserve_none)) *rv32_region)"
                "(struct riscv_internal *);\n"
                "void rv32_aot_enter(struct riscv_internal *rv, void *entry)\n"
                "{\n    ((rv32_region) entry)(rv);\n}\n"
            )
        # Regions are read for selection and again for object-cache keys.
        region_hash = functools.cache(
            lambda path: hashlib.sha256(path.read_bytes()).digest()
        )
        region_size = functools.cache(lambda path: path.stat().st_size)

        candidates = []
        for region in regions:
            match = re.fullmatch(
                r"([0-9a-f]{8})_([0-9a-f]{16})(?:_v[0-9a-f]{16})?", region.stem
            )
            if match:
                pc, fingerprint = (int(word, 16) for word in match.groups())
                digest_file = region.with_suffix(".bc.digest")
                page_digest = digest_file.read_bytes()
                if len(page_digest) != 32:
                    raise ValueError(f"invalid page digest: {digest_file}")
                entry_file = region.with_suffix(".bc.entries")
                if not entry_file.exists():
                    raise ValueError(f"missing page entries: {entry_file}")
                entry_pcs = [
                    int(word, 16) for word in entry_file.read_text().split()
                ]
                if not entry_pcs or any(
                    (entry & ~4095) != pc or entry & 1 for entry in entry_pcs
                ):
                    raise ValueError(f"invalid page entries: {entry_file}")
            elif re.fullmatch(r"[0-9a-f]{8}", region.stem):
                entry_pcs, fingerprint = [int(region.stem, 16)], 0
                page_digest = None
            else:
                raise ValueError(f"invalid region filename: {region}")
            candidates.append((region, fingerprint, entry_pcs, page_digest))
        # Prefer the broadest graph for shared entries, and retain other graphs
        # only when they contribute an entry not yet covered. LLVM-C renaming
        # lets different observations of one page coexist without collisions.
        candidates.sort(key=lambda item: (-len(item[2]), -region_size(item[0])))
        selected, entry_map, symbols = [], {}, set()
        digests, bodies = set(), {}
        for region, fingerprint, entry_pcs, page_digest in candidates:
            uncovered = [
                pc
                for pc in entry_pcs
                if (pc, fingerprint, page_digest) not in entry_map
            ]
            if not uncovered:
                continue
            original_name = f"rv32_aot_{region.stem}"
            body_hash = region_hash(region).hex()
            body_key = (original_name, body_hash)
            if body_key in bodies:
                name = bodies[body_key]
            else:
                name = original_name
                if name in symbols:
                    name = f"{original_name}_v{body_hash}"
                    attempt = 0
                    while name in symbols:
                        attempt += 1
                        name = f"{original_name}_v{body_hash}_{attempt}"
                    destination = work / f"{name}.bc"
                    rename_region(
                        region, destination, original_name, name, llvm_config
                    )
                    region = destination
                bodies[body_key] = name
                symbols.add(name)
                selected.append(region)
                declarations.append(
                    f"extern {convention}void {name}(struct riscv_internal *);\n"
                )
            digest_name = "0"
            if page_digest is not None:
                digest_name = f"rv32_digest_{page_digest.hex()}"
                if page_digest not in digests:
                    values = ",".join(str(byte) for byte in page_digest)
                    declarations.append(
                        f"static const unsigned char {digest_name}[32] = {{{values}}};\n"
                    )
                    digests.add(page_digest)
            for pc in uncovered:
                entry_map[(pc, fingerprint, page_digest)] = (
                    pc,
                    fingerprint,
                    name,
                    digest_name,
                )
        regions = selected
        entries = list(entry_map.values())
        if entries:
            size = 1
            while size < 2 * len(entries):
                size *= 2
            table = [None] * size
            for entry in entries:
                # Must match aot_index_find() in src/aot.c.
                product = ((entry[0] >> 1) * 2654435761) & 0xFFFFFFFF
                slot = (product ^ (product >> 16)) & (size - 1)
                while table[slot] is not None:
                    slot = (slot + 1) & (size - 1)
                table[slot] = entry
            declarations.append(
                f"const struct aot_entry rv32_aot_index[{size}] = {{\n"
            )
            for slot, entry in enumerate(table):
                if entry:
                    pc, fingerprint, name, digest_name = entry
                    declarations.append(
                        f"[{slot}] = {{0x{pc:08x}, "
                        f"0x{fingerprint:016x}ULL, (rv32_entry) {name}, "
                        f"{digest_name}}},\n"
                    )
            declarations.append(
                "};\n"
                "const uint32_t rv32_aot_index_mask =\n"
                "    sizeof(rv32_aot_index) / sizeof(rv32_aot_index[0]) - 1;\n"
            )
        metadata.write_text("".join(declarations))
        # Page graphs are independent optimization units. Stable hash buckets
        # preserve object-cache hits when another page is added or changed;
        # global size-based partitioning would reshuffle all later chunks.
        buckets = [[] for _ in range(256)]
        for region in regions:
            bucket = hashlib.sha256(region.name.encode()).digest()[0]
            buckets[bucket].append(region)
        chunks = []
        for bucket in buckets:
            chunk, chunk_bytes = [], 0
            for region in sorted(bucket, key=lambda path: path.name):
                size = region_size(region)
                if chunk and (
                    len(chunk) >= 1024 or chunk_bytes + size > 8 * 1024 * 1024
                ):
                    chunks.append(chunk)
                    chunk, chunk_bytes = [], 0
                chunk.append(region)
                chunk_bytes += size
            if chunk:
                chunks.append(chunk)
        # Start expensive buckets first to reduce a slow final worker tail.
        chunks.sort(
            key=lambda paths: sum(map(region_size, paths)), reverse=True
        )

        def compile_chunk(item):
            index, paths = item
            digest = hashlib.sha256(tool_identity + str(level).encode())
            for path in paths:
                digest.update(region_hash(path))
            cached = cache_dir / f"{digest.hexdigest()}.o"
            if cached.exists():
                check_private(cached)
                return cached
            merged = work / f"regions-{index}.bc"
            response = work / f"regions-{index}.rsp"
            response.write_text(
                "\n".join(
                    '"'
                    + str(path.resolve())
                    .replace("\\", "\\\\")
                    .replace('"', '\\"')
                    + '"'
                    for path in paths
                )
            )
            subprocess.run(
                [str(bindir / "llvm-link"), f"@{response}", "-o", str(merged)],
                check=True,
            )
            # Compile into a unique file in the cache, then publish it
            # atomically; concurrent builds never see a partial object.
            descriptor, staging = tempfile.mkstemp(
                dir=cache_dir, prefix=cached.name + "."
            )
            os.close(descriptor)
            try:
                subprocess.run(
                    [
                        str(bindir / "clang"),
                        f"-O{level}",
                        *profile_flags,
                        *codegen_flags,
                        "-fPIC",
                        "-fno-unwind-tables",
                        "-fno-asynchronous-unwind-tables",
                        "-ffunction-sections",
                        "-c",
                        str(merged),
                        "-o",
                        staging,
                    ],
                    check=True,
                )
            except BaseException:
                os.unlink(staging)
                raise
            merged.unlink()
            # Clang replaces the staging file under the umask, which leaves it
            # group-writable where per-user groups use 002. check_private()
            # rejects that, so publish the object writable by its owner only.
            mode = stat.S_IMODE(os.stat(staging).st_mode)
            os.chmod(staging, mode & ~(stat.S_IWGRP | stat.S_IWOTH))
            os.replace(staging, cached)
            return cached

        with ThreadPoolExecutor(max_workers=jobs) as workers:
            objects = list(workers.map(compile_chunk, enumerate(chunks)))
        library = work / "artifact.so"
        subprocess.run(
            [
                str(bindir / "clang"),
                "-fPIC",
                "-shared",
                f"-O{level}",
                *codegen_flags,
                *linker_flags,
                *(profile_flags if not profile_runtime else []),
                *(["-Wl,-u,__llvm_profile_runtime"] if profile_runtime else []),
                "-Wl,-z,defs",
                "-Wl,-Bsymbolic",
                "-Wl,--gc-sections",
                f"-I{Path(__file__).resolve().parent.parent / 'src'}",
                *(str(obj) for obj in objects),
                str(metadata),
                *([str(profile_runtime)] if profile_runtime else []),
                "-o",
                str(library),
            ],
            check=True,
        )
        library.replace(output)
    print(f"Compiled {len(regions)} regions: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, nargs="+")
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("--llvm-config", default="llvm-config")
    parser.add_argument("-O", type=int, choices=range(4), default=3)
    parser.add_argument("--cache-dir", type=Path)
    pgo = parser.add_mutually_exclusive_group()
    pgo.add_argument("--profile-generate", action="store_true")
    pgo.add_argument("--profile-use", type=Path)
    parser.add_argument("--profile-runtime", type=Path)
    parser.add_argument("--thin-lto", action="store_true")
    parser.add_argument(
        "-j", "--jobs", type=int, default=min(os.cpu_count() or 1, 8)
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        compile_profile(
            args.profile,
            args.output,
            args.llvm_config,
            args.O,
            args.jobs,
            args.cache_dir,
            args.profile_generate,
            args.profile_use,
            args.profile_runtime,
            args.thin_lto,
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"AOT compilation failed: {error}\n")


if __name__ == "__main__":
    main()
