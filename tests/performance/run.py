#!/usr/bin/env python3
"""Compare an original Git revision with the working tree, sequentially.

Build both Xorg module versions with matching configure/compiler options before
running. Build products and raw measurement files go in --output, outside the
source tree. This deliberately does not run as part of make check.
"""

import argparse
import csv
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def run(command, *, output=None, cwd=REPO, env=None):
    print("+ " + shlex.join(map(str, command)), flush=True)
    if output is None:
        subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)
    else:
        with output.open("wb") as stream:
            subprocess.run(list(map(str, command)), cwd=cwd, env=env,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)


def git(*args):
    return subprocess.check_output(["git", *args], cwd=REPO)


def read_json(path):
    return json.loads(path.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--before-build", type=Path)
    parser.add_argument("--after-build", type=Path)
    parser.add_argument("--xrdp-source", type=Path)
    parser.add_argument("--skip-live", action="store_true")
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--rounds", type=int, default=9)
    parser.add_argument("--capture-iterations", type=int, default=64)
    parser.add_argument("--display", default=":99")
    args = parser.parse_args()
    if not 9 <= args.rounds <= 999 or args.capture_iterations < 1:
        parser.error("Use 9–999 rounds and a positive iteration count")
    if not args.skip_live and args.rounds > 100:
        parser.error("Live tests support at most 100 rounds")
    if not args.skip_live and not all((args.before_build, args.after_build,
                                       args.xrdp_source)):
        parser.error("Live tests require both build directories and --xrdp-source")
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        parser.error("This runner requires Linux on x86-64")
    for program in ("cc", "nasm", "objcopy", "pkg-config", "git", "lscpu"):
        if shutil.which(program) is None:
            parser.error(f"Required program is missing: {program}")
    output = args.output.resolve()
    if not args.skip_live:
        # The existing Xorg smoke-test shell script expands these paths unquoted.
        for path in (output, REPO, args.before_build.resolve(), args.after_build.resolve()):
            if any(character.isspace() for character in str(path)):
                parser.error("Live-test source, output and build paths must not contain whitespace")
    output.mkdir(parents=True, exist_ok=False)
    build = output / "build"
    build.mkdir()
    baseline = git("rev-parse", args.baseline).decode().strip()
    affinity = sorted(os.sched_getaffinity(0))
    cpu = args.cpu if args.cpu is not None else affinity[0]
    if cpu not in affinity:
        parser.error(f"CPU {cpu} unavailable; allowed CPUs: {affinity}")
    os.sched_setaffinity(0, {cpu})
    metadata = {
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "baseline": baseline,
        "working_head": git("rev-parse", "HEAD").decode().strip(),
        "working_diff_sha256": hashlib.sha256(git("diff", "HEAD", "--", "module")).hexdigest(),
        "machine": platform.platform(),
        "cpu_affinity": [cpu], "rounds": args.rounds,
        "capture_iterations": args.capture_iterations,
        "compiler": subprocess.check_output(["cc", "--version"], text=True).splitlines()[0],
        "cpu_info": subprocess.check_output(["lscpu"], text=True),
        "module_builds": {},
        "working_module_sha256": {
            str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted((REPO / "module").rglob("*"))
            if path.is_file() and path.suffix in (".c", ".h", ".asm")
        },
        "benchmark_sha256": {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(HERE.iterdir()) if path.suffix in (".py", ".c")
        },
    }
    for variant, directory in (("before", args.before_build), ("after", args.after_build)):
        if directory:
            directory = directory.resolve()
            metadata["module_builds"][variant] = {
                "directory": str(directory),
                "configure": subprocess.check_output(
                    [str(directory / "config.status"), "--config"], text=True).strip(),
                "module_sha256": {
                    str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted(directory.glob("*/.libs/*.so"))
                },
            }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")

    # Compile first; do not overlap compilation or independent benchmarks.
    objects = []
    asm_name = "a8r8g8b8_to_a8b8g8r8_box_amd64_sse2"
    for variant, revision in (("before", baseline), ("after", "working")):
        directory = build / variant
        directory.mkdir()
        for relative in ("module/common.asm", f"module/amd64/{asm_name}.asm"):
            data = ((REPO / relative).read_bytes() if revision == "working"
                    else git("show", f"{baseline}:{relative}"))
            (directory / Path(relative).name).write_bytes(data)
        obj = directory / "convert.o"
        run(["nasm", "-f", "elf64", "-DASM_ARCH_AMD64", f"-I{directory}/",
             directory / f"{asm_name}.asm", "-o", obj])
        run(["objcopy", "--redefine-sym", f"{asm_name}=convert_{variant}", obj])
        objects.append(obj)
        run([sys.executable, HERE / "capture_extract.py", "--revision", revision,
             "--output", directory / "capture", "--backend", "sse2"])
    conversion = build / "convert_bench"
    run(["cc", "-O2", "-g", "-std=c99", "-Wall", "-Wextra", "-Werror",
         HERE / "convert_bench.c", *objects, "-o", conversion])
    if not args.skip_live:
        run(["cc", "-O2", "-Wall", "-Wextra", "-Werror",
             f"-I{args.xrdp_source.resolve() / 'common'}", HERE / "client_info.c",
             "-o", build / "client_info"])
        run([build / "client_info", "1024", "768", "40"], output=build / "client_info.bin")

    print("Compilation complete. Starting sequential measurements.", flush=True)
    run([conversion, "--rounds", args.rounds, "--target-ms", "75"],
        output=output / "conversion.csv")
    captures = []
    for round_index in range(args.rounds):
        order = ("before", "after") if round_index % 2 == 0 else ("after", "before")
        for position, variant in enumerate(order):
            path = output / f"capture-{round_index + 1}-{variant}.json"
            run([build / variant / "capture/capture_bench", "--iterations",
                 args.capture_iterations, "--warmup", "3", "--samples", "1"], output=path)
            captures.append({"variant": variant, "round": round_index + 1,
                             "position": position + 1, "result": read_json(path)})

    live = []
    if not args.skip_live:
        # Alternate independent Xorg sessions to reduce fixed run-order bias.
        for session in range(3):
            order = ("before", "after") if session % 2 == 0 else ("after", "before")
            for variant in order:
                directory = output / f"live-{session + 1}-{variant}"
                directory.mkdir()
                sockets = directory / "sockets"
                sockets.mkdir()
                config = directory / "config.json"
                config.write_text(json.dumps({"trials": args.rounds,
                    "warmup_trials": 1, "width": 1024, "height": 768,
                    "frame_interval_ms": 40, "pause_ms": 100,
                    "timeout_seconds": 30 + args.rounds}) + "\n")
                result = directory / "result.json"
                wrapper = directory / "client.sh"
                wrapper.write_text("#!/bin/sh\nexec " + shlex.join([
                    sys.executable, str(HERE / "live_bench.py"), "--client-info",
                    str(build / "client_info.bin"), "--config", str(config),
                    "--output", str(result)]) + ' "$@"\n')
                wrapper.chmod(0o755)
                module_build = args.before_build if variant == "before" else args.after_build
                env = dict(os.environ, XRDP_SOCKET_PATH=str(sockets), XCLIENT=str(wrapper),
                    TESTNAME="benchmark", TEST_DISPLAY=args.display,
                    top_builddir=str(module_build.resolve()), srcdir=str(REPO / "tests"))
                run(["/bin/sh", REPO / "tests/xorg-test-run.sh"],
                    output=directory / "runner.log", cwd=directory, env=env)
                live.append({"variant": variant, "session": session + 1,
                             "result": read_json(result)})

    with (output / "conversion.csv").open() as stream:
        conversion_rows = list(csv.DictReader(stream))
    results = {"metadata": metadata, "conversion": conversion_rows,
               "capture": captures, "live": live,
               "capture_manifests": {variant: read_json(build / variant / "capture/manifest.json")
                                     for variant in ("before", "after")}}
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"Raw results: {output / 'results.json'}", flush=True)
    run([sys.executable, HERE / "report.py", "--input", output / "results.json",
         "--output", output / "report.md"])


if __name__ == "__main__":
    main()
