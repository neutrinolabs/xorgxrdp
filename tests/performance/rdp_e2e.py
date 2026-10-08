#!/usr/bin/env python3
"""Measure actual RDP encode/transport/decode with two Xorg module builds.

Only the selected xorgxrdp module build changes between variants. Supply a
private server runtime prepared by prepare_rdp_server.py, a built headless
FreeRDP client, and a built desktop_workload. No installed services are changed.
"""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import statistics
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
HZ = os.sysconf("SC_CLK_TCK")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def start(command, log, cpus, env=None):
    with log.open("w") as stream:
        return subprocess.Popen(["taskset", "-c", cpus, *map(str, command)],
            stdout=stream, stderr=subprocess.STDOUT,
            env=dict(os.environ, **(env or {})), start_new_session=True)


def stop(process):
    if process is not None and process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def wait_for(condition, processes, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for process in processes:
            if process.poll() is not None:
                raise RuntimeError(f"Process {process.pid} exited {process.returncode}")
        if condition():
            return
        time.sleep(0.025)
    raise RuntimeError("Startup or measurement deadline expired")


def has(path, text):
    return path.exists() and text in path.read_text(errors="replace")


def sleep_until(target):
    while True:
        remaining = (target - time.monotonic_ns()) / 1e9
        if remaining <= 0:
            return
        time.sleep(min(remaining, 0.05))


def snapshot(processes):
    values = {}
    for name, process in processes.items():
        fields = Path(f"/proc/{process.pid}/stat").read_text().rsplit(")", 1)[1].split()
        values[name] = int(fields[11]) + int(fields[12])
    return {"time_ns": time.monotonic_ns(), "ticks": values}


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        raise ValueError("No latency observations")
    position = (len(values) - 1) * fraction
    lo = int(position)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (position - lo)


def analyze_trial(directory, cpu_before, cpu_after):
    source = json.loads((directory / "source.summary.json").read_text())
    client = json.loads((directory / "client.summary.json").read_text())
    if client["exit_status"] != 0 or client["freerdp_error"] != 0 or not client["decoder_ready"]:
        raise RuntimeError("RDP client failed or never decoded a frame")
    if client["regression_marker_frames"]:
        raise RuntimeError("Client observed backwards source frame IDs")
    with (directory / "source.frames.csv").open() as stream:
        submitted = {int(row["frame_id"]): row for row in csv.DictReader(stream)}
    with (directory / "client.frames.csv").open() as stream:
        decoded = list(csv.DictReader(stream))
    begin = int(source["measurement_start_ns"])
    end = int(source["measurement_end_ns"])
    seconds = (end - begin) / 1e9
    observed = {}
    backwards = 0
    previous = 0
    statuses = {}
    for row in decoded:
        status = row["status"]
        statuses[status] = statuses.get(status, 0) + 1
        frame_id = int(row["marker_frame_id"])
        if frame_id <= 0 or frame_id not in submitted:
            continue
        if status != "unique":
            continue
        if frame_id < previous:
            backwards += 1
        previous = max(previous, frame_id)
        observed.setdefault(frame_id, int(row["decoded_ns"]))
    if backwards:
        raise RuntimeError(f"Decoded source frame IDs went backwards {backwards} times")
    timed = [(frame_id, timestamp) for frame_id, timestamp in observed.items()
             if begin <= timestamp < end]
    expected_codec = 9 if client["requested_codec"] == "rfx" else 11
    timed_rows = [row for row in decoded if begin <= int(row["decoded_ns"]) < end]
    if not any(int(row["codec_id"]) == expected_codec for row in timed_rows):
        raise RuntimeError("Requested codec was not used in the measured interval")
    if any(int(row["codec_id"]) not in (expected_codec, 10, 4294967295) for row in timed_rows):
        raise RuntimeError("Unexpected codec in the measured interval")
    if len(timed) < 5:
        raise RuntimeError(f"Too few complete decoded frames: {len(timed)}; statuses={statuses}")
    latencies = [(timestamp - int(submitted[frame_id]["submit_ns"])) / 1e6
                 for frame_id, timestamp in timed]
    if min(latencies) < 0:
        raise RuntimeError("Decoded frame timestamp precedes submission")
    elapsed = (cpu_after["time_ns"] - cpu_before["time_ns"]) / 1e9
    cpu_seconds = {name: (cpu_after["ticks"][name] - ticks) / HZ
                   for name, ticks in cpu_before["ticks"].items()}
    cpu_percent = {name: 100 * amount / elapsed for name, amount in cpu_seconds.items()}
    fps = len(timed) / seconds
    server_cpu = cpu_seconds["Xorg"] + cpu_seconds["xrdp"]
    return {"decoded_fps": fps, "decoded_frames": len(timed),
            "measurement_seconds": seconds, "source_fps": source["source_fps"],
            "latency_p50_ms": statistics.median(latencies),
            "latency_p95_ms": percentile(latencies, .95),
            "latency_min_ms": min(latencies), "latency_max_ms": max(latencies),
            "cpu_seconds": cpu_seconds, "cpu_percent_one_core": cpu_percent,
            "cpu_sampling_seconds": elapsed,
            "server_cpu_ms_per_decoded_frame": 1000 * server_cpu / elapsed / fps,
            "client_cpu_ms_per_decoded_frame": 1000 * cpu_seconds["client"] / elapsed / fps,
            "source": source, "client": client, "marker_statuses": statuses,
            "measured_marker_statuses": {status: sum(row["status"] == status for row in timed_rows)
                                         for status in {row["status"] for row in timed_rows}},
            "measured_frame_codec_counts": {str(codec): sum(int(row["codec_id"]) == codec for row in timed_rows)
                                            for codec in {int(row["codec_id"]) for row in timed_rows}},
            "codec_payload_mbps": sum(int(row["encoded_surface_bytes"]) for row in timed_rows) * 8 / seconds / 1e6,
            "backwards_frame_ids": backwards, "raw_directory": str(directory),
            "decoded_latency_samples_ms": latencies}


def trial(args, output, index, pair, variant, codec, mode):
    directory = output / f"{index:02d}-{codec}-{mode}-{variant}"
    directory.mkdir()
    sockets = directory / "sockets"
    sockets.mkdir()
    number = args.display[1:]
    module_build = args.before_build if variant == "before" else args.after_build
    modules = ",".join(str(module_build / sub / ".libs")
                       for sub in ("xrdpmouse", "xrdpkeyb", "xrdpdev", "module"))
    modules += ",/usr/lib/xorg/modules"
    subprocess.run([sys.executable, HERE / "prepare_rdp_server.py",
                    "--destination", args.server_runtime, "--write-codec-config", codec],
                   check=True, stdout=subprocess.DEVNULL)
    config = (args.server_runtime / "benchmark-templates/xrdp.ini.in").read_text()
    config_template_hash = hashlib.sha256(config.encode()).hexdigest()
    substitutions = {"PORT": str(args.port),
        "CERTIFICATE": str(args.server_runtime / "etc/xrdp/cert.pem"),
        "KEY_FILE": str(args.server_runtime / "etc/xrdp/key.pem"),
        "LOG_FILE": str(directory / "server.log"),
        "XORG_SOCKET": str(sockets / ("xrdp_display_" + number))}
    for name, value in substitutions.items():
        config = config.replace("@" + name + "@", value)
    (directory / "xrdp.ini").write_text(config)
    (directory / "gfx.toml").write_text(
        (args.server_runtime / "etc/xrdp/gfx.toml").read_text())
    processes = {}
    print(f"START {directory.name}", flush=True)
    try:
        processes["Xorg"] = start([args.xorg, args.display, "-config",
            REPO / "xrdpdev/xorg.conf", "-modulepath", modules, "-logfile",
            directory / "Xorg.log", "-ac", "-noreset", "-nolisten", "tcp",
            "-novtswitch", "-sharevts"], directory / "Xorg-console.log", args.xorg_cpus,
            {"XRDP_SOCKET_PATH": str(sockets)})
        wait_for(lambda: Path(substitutions["XORG_SOCKET"]).exists(), list(processes.values()))
        processes["xrdp"] = start([args.server_runtime / "sbin/xrdp-optimized",
            "--nodaemon", "--config", directory / "xrdp.ini"],
            directory / "server-console.log", args.server_cpus)
        wait_for(lambda: has(directory / "server.log", f"listening to port {args.port}"),
                 list(processes.values()))
        processes["client"] = start([args.client, "--host", "127.0.0.1", "--port",
            args.port, "--codec", codec, "--width", args.width, "--height", args.height,
            "--username", "bench", "--password-env", "RDP_BENCH_PASSWORD",
            "--output", directory / "client", "--ready-file", directory / "client.ready",
            "--duration", args.seconds + args.warmup + 45],
            directory / "client.log", args.client_cpus,
            {"LD_LIBRARY_PATH": str(args.client_library_dir), "RDP_BENCH_PASSWORD": "bench"})
        expected = "got RFX capture" if codec == "rfx" else "got H264 capture"
        wait_for(lambda: (directory / "client.ready").exists() and
                 has(directory / "Xorg.log", expected), list(processes.values()))
        marker = "starting gfx rfx pro codec session" if codec == "rfx" else "using OpenH264 for software encoder"
        if not has(directory / "server.log", marker):
            raise RuntimeError("Server did not negotiate the requested codec")
        start_ns = time.monotonic_ns() + 3_000_000_000
        processes["workload"] = start([args.workload, "--display", args.display,
            "--output-prefix", directory / "source", "--mode", mode,
            "--width", args.width, "--height", args.height, "--fps", args.fps,
            "--warmup", args.warmup, "--seconds", args.seconds,
            "--start-ns", start_ns, "--hold-seconds", "2"],
            directory / "source.log", args.workload_cpus)
        wait_for(lambda: has(directory / "source.log", "WORKLOAD_READY"),
                 list(processes.values()))
        begin = start_ns + int(args.warmup * 1e9)
        end = begin + int(args.seconds * 1e9)
        sleep_until(begin)
        before = snapshot(processes)
        sleep_until(end)
        after = snapshot(processes)
        if processes["workload"].wait(timeout=10) != 0:
            raise RuntimeError("Drawing workload failed")
        stop(processes["client"])
        result = analyze_trial(directory, before, after)
        result.update(variant=variant, codec=codec, mode=mode, pair=pair, index=index)
        result["provenance"] = {
            "server_sha256": digest(args.server_runtime / "sbin/xrdp-optimized"),
            "client_sha256": digest(args.client), "workload_sha256": digest(args.workload),
            "module_sha256": {str(path.relative_to(module_build)): digest(path)
                              for path in sorted(module_build.glob("*/.libs/*.so"))},
            "ini_template_sha256": config_template_hash,
            "gfx_config_sha256": digest(directory / "gfx.toml"),
            "xorg_config_sha256": digest(REPO / "xrdpdev/xorg.conf"),
            "xorg_binary_sha256": digest(args.xorg),
            "client_library_sha256": {path.name: digest(path)
                                      for path in sorted(args.client_library_dir.glob("*.so.*"))},
            "server_library_sha256": {path.name: digest(path)
                                      for path in sorted((args.server_runtime / "lib/xrdp").glob("*.so*"))}}
        (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"DONE {directory.name}: {result['decoded_fps']:.2f} decoded FPS; "
              f"p95 {result['latency_p95_ms']:.2f} ms; "
              f"server CPU {result['server_cpu_ms_per_decoded_frame']:.2f} ms/frame", flush=True)
        return result
    finally:
        for process in reversed(list(processes.values())):
            stop(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before-build", "after-build", "server-runtime", "client", "client-library-dir", "workload", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--xorg", type=Path, default=Path("/usr/lib/xorg/Xorg"))
    parser.add_argument("--baseline", required=True, help="Git revision used for --before-build")
    parser.add_argument("--display", default=":98")
    parser.add_argument("--port", type=int, default=3397)
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--codecs", default="rfx,avc420")
    parser.add_argument("--modes", default="fullframe,sparse32")
    parser.add_argument("--server-cpus", default="0,1")
    parser.add_argument("--client-cpus", default="2,3")
    parser.add_argument("--xorg-cpus", default="4")
    parser.add_argument("--workload-cpus", default="5")
    args = parser.parse_args()
    if not args.display.startswith(":") or not args.display[1:].isdigit() or int(args.display[1:]) < 20:
        parser.error("Use an explicit unused local display numbered 20 or higher")
    if Path("/tmp/.X11-unix/X" + args.display[1:]).exists():
        parser.error("Display already exists; it will not be touched")
    if args.pairs < 1 or args.seconds < 2 or args.warmup < 1:
        parser.error("Require positive pairs, >=2 measured seconds, and >=1 warmup second")
    codecs, modes = args.codecs.split(","), args.modes.split(",")
    if not set(codecs) <= {"rfx", "avc420"} or not set(modes) <= {"fullframe", "sparse32"}:
        parser.error("Unsupported codec or workload")
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", args.port))
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "arguments": {name: str(value) if isinstance(value, Path) else value
                      for name, value in vars(args).items()},
        "cpu_info": subprocess.check_output(["lscpu"], text=True),
        "clock": "CLOCK_MONOTONIC on one host", "network": "TLS RDP over TCP loopback",
        "endpoint": "FreeRDP software-decoded framebuffer after GFX EndFrame",
        "baseline_revision": subprocess.check_output(["git", "rev-parse", args.baseline], cwd=REPO, text=True).strip(),
        "working_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
        "working_module_source_sha256": {str(path.relative_to(REPO)): digest(path)
            for path in sorted((REPO / "module").rglob("*"))
            if path.is_file() and path.suffix in (".c", ".h", ".asm")},
        "server_sha256": digest(args.server_runtime / "sbin/xrdp-optimized"),
        "client_sha256": digest(args.client), "workload_sha256": digest(args.workload),
        "module_sha256": {variant: {str(path.relative_to(build)): digest(path)
                          for path in sorted(build.glob("*/.libs/*.so"))}
                          for variant, build in (("before", args.before_build), ("after", args.after_build))},
        "harness_sha256": {path.name: digest(path) for path in sorted(HERE.iterdir())
                           if path.suffix in (".c", ".py")},
        "server_relocation": json.loads((args.server_runtime / "relocation-manifest.json").read_text())}
    results = {"metadata": metadata, "trials": []}
    result_path = args.output / "results.json"
    result_path.write_text(json.dumps(results, indent=2) + "\n")
    index = 0
    for codec in codecs:
        for mode in modes:
            for pair in range(1, args.pairs + 1):
                for variant in (("before", "after") if pair % 2 else ("after", "before")):
                    index += 1
                    results["trials"].append(trial(args, args.output, index, pair, variant, codec, mode))
                    result_path.write_text(json.dumps(results, indent=2) + "\n")
    print(f"Results: {result_path}", flush=True)


if __name__ == "__main__":
    main()
