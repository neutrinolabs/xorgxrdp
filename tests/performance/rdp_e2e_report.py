#!/usr/bin/env python3
"""Validate rdp_e2e.py results and write a paired Markdown comparison.

The report uses complete source markers observed by the real FreeRDP decoder,
not drawing-loop or capture-command counts. It retains every requested pair
and reports signed effects, including regressions. Only Python's standard
library is required. No servers or measured workloads are started.
"""

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re
import statistics
import sys


VARIANTS = ("before", "after")
CODEC_IDS = {"rfx": 0x9, "avc420": 0xB}
CODEC_NAMES = {"rfx": "RFX", "avc420": "AVC420"}
MODE_NAMES = {"fullframe": "Full frame", "sparse32": "32 sparse rectangles"}
METRICS = (
    ("decoded_fps", "Complete decoded FPS"),
    ("latency_p50_ms", "Latency p50 (ms)"),
    ("latency_p95_ms", "Latency p95 (ms)"),
    ("source_fps", "Source FPS"),
    ("xorg_cpu_percent", "Xorg CPU (% of one core)"),
    ("server_cpu_ms_per_decoded_frame", "Xorg + xrdp CPU (ms/decoded frame)"),
)
SOURCE_SETTINGS = (
    "workload", "width", "height", "target_fps", "warmup_seconds",
    "measured_seconds", "clock", "marker_pixels_per_frame",
    "sparse_rectangles", "sparse_pixels_per_frame", "presentation",
    "sync_each_frame",
)
FIXED_HASHES = ("server_sha256", "client_sha256", "workload_sha256")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, context, positive=False):
    require(isinstance(value, (int, float)) and not isinstance(value, bool),
            f"{context}: expected a number")
    require(math.isfinite(value) and (value > 0 if positive else value >= 0),
            f"{context}: expected a finite {'positive' if positive else 'nonnegative'} number")
    return float(value)


def integer(value, context, positive=False):
    require(isinstance(value, int) and not isinstance(value, bool),
            f"{context}: expected an integer")
    number(value, context, positive)
    return value


def close(actual, expected, context):
    actual = number(actual, context)
    require(math.isclose(actual, expected, rel_tol=1e-7, abs_tol=1e-6),
            f"{context}: {actual} disagrees with recomputed {expected}")


def hash_value(value, context):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value),
            f"{context}: expected a SHA-256 digest")
    return value


def hash_map(values, context):
    require(isinstance(values, dict) and values, f"{context}: missing artifact hashes")
    for name, value in values.items():
        require(isinstance(name, str) and name, f"{context}: invalid artifact name")
        hash_value(value, f"{context}/{name}")


def percentile(values, fraction):
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(values) - 1)
    return values[lower] + (values[upper] - values[lower]) * (position - lower)


def validate_provenance(metadata, trials):
    """Require trial-local hashes so a global manifest alone cannot prove parity."""
    for name in FIXED_HASHES:
        hash_value(metadata[name], "metadata/" + name)
    modules = metadata["module_sha256"]
    require(set(modules) == set(VARIANTS), "Module manifests must contain before and after")
    for variant in VARIANTS:
        hash_map(modules[variant], f"metadata/module_sha256/{variant}")
    require(set(modules["before"]) == set(modules["after"]),
            "Before and after module artifact names differ")
    require(modules["before"] != modules["after"],
            "Before and after module manifests are identical")
    hash_map(metadata["harness_sha256"], "metadata/harness_sha256")
    relocation = metadata["server_relocation"]
    require(relocation["elf_text_unchanged"] is True,
            "Server relocation did not preserve executable .text")
    require(relocation["files"]["sbin/xrdp-optimized"]["after_sha256"] ==
            metadata["server_sha256"], "Fixed server hash disagrees with relocation manifest")
    common = None
    codec_configs = {}
    for row in trials:
        context = f"{row['codec']}/{row['mode']}/pair {row['pair']}/{row['variant']}"
        evidence = row.get("provenance")
        require(isinstance(evidence, dict), f"{context}: missing per-trial provenance")
        for name in FIXED_HASHES:
            require(evidence[name] == metadata[name], f"{context}: {name} changed")
        require(evidence["module_sha256"] == modules[row["variant"]],
                f"{context}: loaded module manifest differs from variant manifest")
        for name in ("ini_template_sha256", "gfx_config_sha256",
                     "xorg_config_sha256", "xorg_binary_sha256"):
            hash_value(evidence[name], context + "/" + name)
        hash_map(evidence["client_library_sha256"], context + "/client libraries")
        hash_map(evidence["server_library_sha256"], context + "/server libraries")
        # Keep any additional invariant fields, including shared library hashes,
        # in the equality check. Only modules and the requested codec may vary.
        invariant = {key: value for key, value in evidence.items()
                     if key not in ("module_sha256", "gfx_config_sha256")}
        if common is None:
            common = invariant
        require(invariant == common, f"{context}: non-module runtime/configuration changed")
        codec = row["codec"]
        codec_configs.setdefault(codec, evidence["gfx_config_sha256"])
        require(codec_configs[codec] == evidence["gfx_config_sha256"],
                f"{context}: codec configuration changed between trials")
    return common


def validate_trial(row, args):
    context = f"{row['codec']}/{row['mode']}/pair {row['pair']}/{row['variant']}"
    source, client = row["source"], row["client"]
    require(client["connected"] is True and client["decoder_ready"] is True,
            f"{context}: client did not connect and decode")
    for key in ("exit_status", "freerdp_error", "regression_marker_frames"):
        require(client[key] == 0, f"{context}: nonzero client {key}")
    require(row["backwards_frame_ids"] == 0, f"{context}: backwards source markers")
    require(client["requested_codec"] == row["codec"], f"{context}: wrong requested codec")
    counts = client["codec_command_counts"]
    for key, value in counts.items():
        integer(value, f"{context}: codec {key} command count")
    expected_id = CODEC_IDS[row["codec"]]
    require(counts.get(str(expected_id), 0) > 0,
            f"{context}: no commands with negotiated codec ID 0x{expected_id:x}")
    measured_codecs = row["measured_frame_codec_counts"]
    require(measured_codecs.get(str(expected_id), 0) > 0,
            f"{context}: expected codec missing from measured interval")
    for codec_id, count in measured_codecs.items():
        integer(count, context + "/measured codec frame count")
        require(int(codec_id) in (expected_id, 10, 4294967295),
                f"{context}: unexpected measured codec ID {codec_id}")
    measured_statuses = row["measured_marker_statuses"]
    for status, count in measured_statuses.items():
        integer(count, context + "/measured marker status " + status)
    require(sum(measured_codecs.values()) == sum(measured_statuses.values()),
            f"{context}: measured codec and marker frame counts differ")
    number(row["codec_payload_mbps"], context + "/encoded codec payload Mbps")
    require(source["clock"] == client["clock"] == "CLOCK_MONOTONIC",
            f"{context}: timestamps do not use the same monotonic clock")
    require(source["workload"] == row["mode"], f"{context}: source workload mismatch")
    for source_name, arg_name in (("width", "width"), ("height", "height"),
                                  ("target_fps", "fps"), ("warmup_seconds", "warmup"),
                                  ("measured_seconds", "seconds")):
        require(source[source_name] == args[arg_name],
                f"{context}: source {source_name} differs from configured {arg_name}")
    require(source["sync_each_frame"] is True, f"{context}: source XSync pacing changed")
    require(source["marker_pixels_per_frame"] == 16384, f"{context}: marker layout changed")
    sparse = row["mode"] == "sparse32"
    require(source["sparse_rectangles"] == (32 if sparse else 0) and
            source["sparse_pixels_per_frame"] == (8192 if sparse else 0),
            f"{context}: source rectangle pattern changed")
    require(source["presentation"] == ("separate_rectangles" if sparse else
                                        "offscreen_upload_then_copy"),
            f"{context}: source presentation changed")
    begin = integer(source["measurement_start_ns"], context + "/measurement_start", True)
    end = integer(source["measurement_end_ns"], context + "/measurement_end", True)
    require(end > begin, f"{context}: empty measurement window")
    seconds = (end - begin) / 1e9
    close(row["measurement_seconds"], seconds, context + "/measurement_seconds")
    close(source["measured_seconds"], seconds, context + "/source duration")
    generated = integer(source["measured_frames_generated"], context + "/source frames", True)
    close(source["source_fps"], generated / seconds, context + "/source FPS")
    close(row["source_fps"], source["source_fps"], context + "/source FPS summary")
    integer(source["skipped_source_slots"], context + "/skipped source slots")
    frames = integer(row["decoded_frames"], context + "/decoded frames", True)
    require(frames >= 5, f"{context}: fewer than five complete decoded frames")
    require(measured_statuses.get("unique", 0) == frames,
            f"{context}: measured unique marker count differs from decoded frames")
    require(frames <= client["unique_marker_frames"], f"{context}: invalid decoded frame count")
    samples = row["decoded_latency_samples_ms"]
    require(len(samples) == frames, f"{context}: latency sample count differs from decoded frames")
    samples = [number(value, context + "/latency sample") for value in samples]
    expected = {"decoded_fps": frames / seconds,
                "latency_p50_ms": statistics.median(samples),
                "latency_p95_ms": percentile(samples, .95),
                "latency_min_ms": min(samples), "latency_max_ms": max(samples)}
    elapsed = number(row["cpu_sampling_seconds"], context + "/CPU sampling seconds", True)
    require(abs(elapsed - seconds) <= max(.05, .02 * seconds),
            f"{context}: CPU sampling duration differs materially from frame window")
    cpu = row["cpu_seconds"]
    for name in ("Xorg", "xrdp", "client", "workload"):
        amount = number(cpu[name], context + "/" + name + " CPU seconds")
        close(row["cpu_percent_one_core"][name], 100 * amount / elapsed,
              context + "/" + name + " CPU percent")
    expected["server_cpu_ms_per_decoded_frame"] = (
        1000 * (cpu["Xorg"] + cpu["xrdp"]) / elapsed / expected["decoded_fps"])
    expected["client_cpu_ms_per_decoded_frame"] = (
        1000 * cpu["client"] / elapsed / expected["decoded_fps"])
    for key, value in expected.items():
        close(row[key], value, context + "/" + key)
    expected["source_fps"] = source["source_fps"]
    expected["xorg_cpu_percent"] = row["cpu_percent_one_core"]["Xorg"]
    return expected


def validate(data):
    metadata, trials = data["metadata"], data["trials"]
    args = metadata["arguments"]
    pairs = integer(args["pairs"], "arguments/pairs", True)
    codecs, modes = args["codecs"].split(","), args["modes"].split(",")
    require(codecs and len(set(codecs)) == len(codecs) and set(codecs) <= set(CODEC_IDS),
            "Unsupported or repeated codecs")
    require(modes and len(set(modes)) == len(modes) and set(modes) <= set(MODE_NAMES),
            "Unsupported or repeated source workloads")
    require(metadata["network"] == "TLS RDP over TCP loopback", "Unexpected RDP transport")
    require(metadata["endpoint"] == "FreeRDP software-decoded framebuffer after GFX EndFrame",
            "Unexpected observation endpoint")
    require(metadata["clock"] == "CLOCK_MONOTONIC on one host", "Unexpected clock domain")
    for name in ("seconds", "warmup", "fps", "width", "height"):
        number(args[name], "arguments/" + name, True)
    expected = {(codec, mode, pair, variant)
                for codec in codecs for mode in modes
                for pair in range(1, pairs + 1) for variant in VARIANTS}
    seen = set()
    groups = defaultdict(lambda: defaultdict(dict))
    settings = {}
    versions = set()
    indices = []
    for row in trials:
        key = row["codec"], row["mode"], row["pair"], row["variant"]
        require(key in expected and key not in seen, f"Unexpected or repeated trial: {key}")
        seen.add(key)
        integer(row["pair"], "trial pair", True)
        indices.append(integer(row["index"], "trial index", True))
        groups[key[:2]][key[2]][key[3]] = (row, validate_trial(row, args))
        source_settings = {name: row["source"][name] for name in SOURCE_SETTINGS}
        settings.setdefault(row["mode"], source_settings)
        require(settings[row["mode"]] == source_settings,
                f"{key}: source pattern, marker, dimensions or pacing settings changed")
        versions.add(row["client"]["freerdp_version"])
    require(seen == expected, f"Missing paired trials: {sorted(expected - seen)}")
    require(indices == list(range(1, len(trials) + 1)), "Trial indices/order are inconsistent")
    require(len(versions) == 1, "FreeRDP version changed between trials")
    for case, case_pairs in groups.items():
        for pair, variants in case_pairs.items():
            before = variants["before"][0]["index"]
            after = variants["after"][0]["index"]
            require(after - before == (1 if pair % 2 else -1),
                    f"{case}/pair {pair}: paired execution did not alternate order")
    invariant = validate_provenance(metadata, trials)
    return groups, invariant


def summary(values):
    return f"{statistics.median(values):.2f} [{min(values):.2f}, {max(values):.2f}]"


def effect(before, after):
    return None if before == 0 else 100 * (after / before - 1)


def percent(value):
    return "n/a (zero baseline)" if value is None else f"{value:+.2f}%"


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def report(data, input_path):
    groups, invariant = validate(data)
    metadata = data["metadata"]
    args = metadata["arguments"]
    pairs = args["pairs"]
    intervals = metadata["server_relocation"]["frame_intervals_ms"]
    cpu_info = dict(line.split(":", 1) for line in metadata.get("cpu_info", "").splitlines()
                    if ":" in line)
    lines = [
        "Actual RDP before/after comparison", "",
        f"{args['width']}×{args['height']}, target {args['fps']} source FPS, "
        f"{args['seconds']} measured seconds after {args['warmup']} warmup seconds per trial; "
        f"{pairs} paired trials per codec/workload. Adjacent trials are paired, with alternating execution order "
        "(before→after, then after→before).", "",
        f"Machine: {cpu_info.get('Model name', 'not recorded').strip()}; "
        f"{cpu_info.get('CPU(s)', 'unknown').strip()} logical CPUs; "
        f"hypervisor: {cpu_info.get('Hypervisor vendor', 'not reported').strip()}.", "",
        "The endpoint is the real FreeRDP software-decoded framebuffer after GFX EndFrame, "
        "using actual xrdp encoding and TLS RDP over TCP loopback. Complete decoded FPS "
        "counts distinct source IDs with matching valid top/bottom markers, observed within "
        "the measurement window. Duplicates and invalid or torn markers are excluded. "
        "This measures complete marked updates; it is not a count of every decoded surface "
        "or physical display scanout, and it does not simulate a WAN.", "",
        "Latency runs from the first X11 drawing submission to the matching decoded-frame "
        "observation, using CLOCK_MONOTONIC on the same host. It includes X11 processing, "
        "capture, encoding, TCP/TLS and software decoding. Source texture generation before "
        "submission is excluded. Frames submitted during warmup but decoded inside the "
        "measurement window are included; frames decoded after that window are excluded.", "",
        "Cells show median [minimum, maximum] across trials. Paired change is "
        "100 × (after/before − 1); every pair is retained. Positive means higher, negative "
        "means lower: higher decoded FPS and lower latency/CPU are desirable. Source FPS "
        "describes delivered input, so changes there must be considered when interpreting "
        "decoded FPS. Ranges are observed extrema, not confidence intervals; these "
        "descriptive results do not establish statistical significance.", "",
    ]
    for codec in args["codecs"].split(","):
        for mode in args["modes"].split(","):
            case_pairs = groups[(codec, mode)]
            lines += [f"**{CODEC_NAMES[codec]} / {MODE_NAMES[mode]}**", "",
                      "| Metric | Before median [min, max] | After median [min, max] | Median paired change | Individual paired changes |",
                      "| --- | ---: | ---: | ---: | --- |"]
            for name, label in METRICS:
                before = [case_pairs[pair]["before"][1][name] for pair in range(1, pairs + 1)]
                after = [case_pairs[pair]["after"][1][name] for pair in range(1, pairs + 1)]
                effects = [effect(old, new) for old, new in zip(before, after)]
                median_effect = None if None in effects else statistics.median(effects)
                individual = "; ".join(f"P{pair}: {percent(value)}"
                                       for pair, value in enumerate(effects, 1))
                lines.append(f"| {label} | {summary(before)} | {summary(after)} | "
                             f"{percent(median_effect)} | {individual} |")
            lines.append("")
    lines += [
        "Xorg CPU is process CPU time as a percentage of one logical core. The combined "
        "Xorg + xrdp CPU metric includes encoder worker threads and divides sampled CPU "
        "seconds/second by observed complete decoded FPS. It excludes client decoding and "
        "source generation. CPU samples bracket the same steady-state measurement window; "
        "a process using multiple cores can exceed 100%.", "",
        f"The fixed private server configuration uses {intervals['rfx']} ms for the RFX "
        f"capture interval and {intervals['avc420']} ms for AVC420. These scheduling "
        "intervals and the source's target FPS can limit decoded FPS even when CPU "
        "work per frame improves.", "",
        "Full-frame motion scrolls a deterministic textured desktop uploaded to an "
        "offscreen pixmap and presented with XCopyArea. Sparse mode changes 32 separate "
        "16×16 rectangles (8,192 pixels) plus two marker strips (16,384 submitted pixels) "
        "per source frame. Both modes use XSync after each frame and skip missed pacing "
        "slots. Valid markers identify matching source frames; they do not verify "
        "pixel-exact quality over the entire image.", "",
        "Validation passed: all requested pairs are complete, client exit/error codes "
        "and backwards-marker counts are zero, decoded FPS and latency summaries match "
        "the retained samples, and CPU metrics match sampled process times. Negotiated "
        "main codec IDs are checked as RFX 0x9 and AVC420 0xb; planar surfaces (0xa) "
        "and frames with no encoded surface are allowed. Per-trial binary/configuration hashes agree across "
        "variants except the selected xorgxrdp modules; codec configuration is fixed "
        "within each codec. Source settings, workload/client binaries, marker layout "
        "and FreeRDP version match across trials.", "",
        "| Provenance | Recorded value |", "| --- | --- |",
        f"| Results | {cell(input_path)} |",
        f"| Created UTC | {cell(metadata['created_utc'])} |",
        f"| Baseline source revision | {cell(metadata.get('baseline_revision', 'not recorded'))} |",
        f"| Before module build | {cell(args['before_build'])} |",
        f"| After module build | {cell(args['after_build'])} |",
        f"| Fixed xrdp SHA-256 | {metadata['server_sha256']} |",
        f"| FreeRDP client SHA-256 | {metadata['client_sha256']} |",
        f"| FreeRDP version | {cell(data['trials'][0]['client']['freerdp_version'])} |",
        f"| Source workload SHA-256 | {metadata['workload_sha256']} |",
        f"| Fixed Xorg SHA-256 | {invariant['xorg_binary_sha256']} |",
        f"| CPU affinity | Xorg: {cell(args['xorg_cpus'])}; "
        f"xrdp: {cell(args['server_cpus'])}; client: {cell(args['client_cpus'])}; "
        f"source: {cell(args['workload_cpus'])} |", "",
        "Raw per-trial frame timestamps, client summaries and logs are retained in each "
        "trial's raw_directory in results.json. Startup/warmup/drain invalid or torn "
        "marker counts in client summaries cover the entire session, so they must not "
        "be treated as measured-window loss rates.", "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="rdp_e2e.py results.json")
    parser.add_argument("--output", type=Path, required=True, help="Markdown report destination")
    args = parser.parse_args()
    try:
        data = json.loads(args.input.read_text())
        rendered = report(data, args.input.resolve())
        args.output.write_text(rendered)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Invalid RDP benchmark results: {error}", file=sys.stderr)
        return 1
    print(f"Report: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
