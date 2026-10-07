#!/usr/bin/env python3
"""Build the capture benchmark from actual revision-specific production code.

The generated translation unit contains unmodified function bodies from
rdpCapture.c, rdpClientCon.c, rdpReg.c and (when selected) rdpSimd.c. The harness
adapts Xorg regions to pixman and stubs the final network notification. No Xorg
server, GPU readback, encoder or RDP client is exercised by this benchmark.

Example:
  python3 tests/performance/capture_extract.py --revision HEAD --output /tmp/cap-before
  python3 tests/performance/capture_extract.py --revision working --output /tmp/cap-after
  /tmp/cap-before/capture_bench --verify-only
"""

import argparse
import hashlib
import json
import platform
import re
import shlex
import shutil
import subprocess
from pathlib import Path


def extract_function(source, name):
    pattern = rf"(?m)^(?:static\s+)?(?:int|Bool|void|uint64_t)\s*\n{name}\s*\("
    match = re.search(pattern, source)
    if match is None:
        raise ValueError(f"Cannot find production function {name}")
    opening = source.index("{", match.end())
    index = opening
    depth = 0
    state = "code"
    while index < len(source):
        char = source[index]
        pair = source[index:index + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if pair == "*/":
                state = "code"
                index += 1
        elif state in ('"', "'"):
            if char == "\\":
                index += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            index += 1
        elif pair == "/*":
            state = "comment"
            index += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1] + "\n"
        index += 1
    raise ValueError(f"Unterminated production function {name}")


def extract_define(source, name):
    match = re.search(rf"(?m)^#define {name}\b", source)
    if match is None:
        raise ValueError(f"Cannot find production macro {name}")
    # Read continuation lines explicitly.
    lines = source[match.start():].splitlines(keepends=True)
    result = []
    for line in lines:
        result.append(line)
        if not line.rstrip().endswith("\\"):
            break
    return "".join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True, help="Git revision or 'working'")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backend", choices=("auto", "sse2", "scalar"), default="auto")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cflags", default="-O2 -g -std=c99")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    revision = args.revision
    resolved = "working" if revision == "working" else subprocess.check_output(
        ["git", "rev-parse", revision], cwd=repo, text=True).strip()
    source_hashes = {}

    def read(relative):
        if revision == "working":
            text = (repo / relative).read_text()
        else:
            text = subprocess.check_output(
                ["git", "show", f"{resolved}:{relative}"], cwd=repo, text=True)
        source_hashes[relative] = hashlib.sha256(text.encode()).hexdigest()
        return text

    backend = args.backend
    can_sse2 = platform.machine().lower() in ("x86_64", "amd64") and shutil.which("nasm")
    if backend == "auto":
        backend = "sse2" if can_sse2 else "scalar"
    if backend == "sse2" and not can_sse2:
        parser.error("SSE2 benchmark requires x86-64 and nasm")

    capture = read("module/rdpCapture.c")
    client = read("module/rdpClientCon.c")
    common = read("module/rdp.h")
    client_header = read("module/rdpClientCon.h")
    capture_header = read("module/rdpCapture.h")
    cap_rect = extract_function(client, "rdpCapRect")
    uses_coalescing = "rdpRegionCoalesce(" in cap_rect
    generated = []
    function_hashes = {}

    def append_function(source, name):
        body = extract_function(source, name)
        generated.append(body)
        function_hashes[name] = hashlib.sha256(body.encode()).hexdigest()

    if uses_coalescing:
        (output / "rdpCoalesce.h").write_text(read("module/rdpCoalesce.h"))
        generated.append(read("module/rdpCoalesce.c"))
        append_function(read("module/rdpReg.c"), "rdpRegionCoalesce")
    for name in ("rdpFillBox_yuvalp", "a8r8g8b8_to_yuvalp_box",
                 "rdpCopyBox_a8r8g8b8_to_yuvalp", "isShmStatusActive",
                 "wyhash_rfx_tile", "rdpCaptureGfxPro"):
        append_function(capture, name)
    if backend == "sse2":
        append_function(read("module/rdpSimd.c"),
                        "a8r8g8b8_to_yuvalp_box_amd64_sse2_wrap")
    generated.append(cap_rect)
    function_hashes["rdpCapRect"] = hashlib.sha256(cap_rect.encode()).hexdigest()
    (output / "capture_generated.inc").write_text("\n".join(generated))
    (output / "wyhash.h").write_text(read("module/wyhash.h"))

    status = re.search(r"enum shared_memory_status\s*\{.*?\};", client_header, re.S)
    if status is None:
        raise ValueError("Cannot find shared memory status enum")
    config = [f"#define BENCH_REVISION {json.dumps(resolved)}\n",
              f"#define BENCH_BACKEND {json.dumps(backend)}\n",
              f"#define BENCH_CFLAGS {json.dumps(args.cflags)}\n",
              f"#define BENCH_SSE2 {int(backend == 'sse2')}\n",
              f"#define BENCH_COALESCING {int(uses_coalescing)}\n",
              status.group(0) + "\n"]
    for source, name in ((capture, "WYHASH_SEED"), (capture, "RGB_SPLIT"),
                         (common, "RDPCLAMP"), (common, "RDP_MAX_TILES"),
                         (common, "XRDP_RFX_ALIGN"), (capture_header, "MAX_CAPTURE_RECTS")):
        config.append(extract_define(source, name))
    (output / "capture_config.h").write_text("".join(config))

    objects = []
    if backend == "sse2":
        (output / "common.asm").write_text(read("module/common.asm"))
        asm = "a8r8g8b8_to_yuvalp_box_amd64_sse2.asm"
        (output / asm).write_text(read("module/amd64/" + asm))
        obj = output / "conversion.o"
        subprocess.run(["nasm", "-f", "elf64", "-DASM_ARCH_AMD64", f"-I{output}/",
                        str(output / asm), "-o", str(obj)], check=True)
        objects.append(str(obj))
    pixman = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "pixman-1"], text=True))
    compiler = shlex.split(args.cc)
    command = compiler + shlex.split(args.cflags) + ["-D_POSIX_C_SOURCE=200809L",
        "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
        f"-I{output}", str(repo / "tests/performance/capture_bench.c")] + objects + pixman + [
        "-o", str(output / "capture_bench")]
    subprocess.run(command, check=True)
    for relative in ("tests/performance/capture_bench.c", "tests/performance/capture_extract.py"):
        source_hashes[relative] = hashlib.sha256((repo / relative).read_bytes()).hexdigest()
    manifest = {"revision": resolved, "requested_revision": revision, "backend": backend,
                "capture_call_boundary": "noinline shim preserving production translation-unit boundary",
                "coalescing": uses_coalescing, "compiler_command": command,
                "compiler_version": subprocess.check_output(
                    compiler + ["--version"], text=True).splitlines()[0],
                "source_sha256": source_hashes,
                "production_function_sha256": function_hashes}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output / "capture_bench")


if __name__ == "__main__":
    main()
