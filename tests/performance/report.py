#!/usr/bin/env python3
"""Validate run.py results and write a Markdown before/after report.

All timing summaries are recomputed from raw samples. Capture output digests
and emitted tile counts must agree before any performance report is written.
"""

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics
import sys


VARIANTS = ("before", "after")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def positive(value, context):
    number = float(value)
    require(math.isfinite(number) and number > 0,
            f"{context}: expected a finite positive value, got {value!r}")
    return number


def statistics_ms(values):
    require(values, "Cannot summarize an empty sample set")
    return statistics.median(values), min(values), max(values)


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def label(name):
    return name.replace("destination_plus4", "destination +4 bytes").replace(
        "different_strides", "different aligned strides").replace(
        "rotating_alignment", "changing row alignment").replace("_", " ")


def timing_cell(values):
    median, minimum, maximum = statistics_ms(values)
    return f"{median:.4f} [{minimum:.4f}–{maximum:.4f}]"


def timing_table(groups):
    lines = ["| Case | Before ms [range] | After ms [range] | Time reduction | Speedup |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for name, values in groups.items():
        before = statistics.median(values["before"])
        after = statistics.median(values["after"])
        lines.append(f"| {cell(label(name))} | {timing_cell(values['before'])} | "
                     f"{timing_cell(values['after'])} | "
                     f"{100 * (1 - after / before):.2f}% | {before / after:.3f}× |")
    return lines


def validate_variants(groups, context):
    require(groups, f"No {context} samples")
    for name, variants in groups.items():
        require(set(variants) == set(VARIANTS),
                f"{context} {name}: both before and after are required")


def conversion_results(rows, expected_rounds):
    records = defaultdict(lambda: defaultdict(dict))
    checks = set()
    for row in rows:
        kind = row["record"]
        if kind == "check":
            checks.add((row["case"], row["variant"]))
        elif kind == "sample":
            case, variant = row["case"], row["variant"]
            require(variant in VARIANTS, f"Unknown conversion variant: {variant}")
            round_number = int(row["round"])
            frames = int(row["frames"])
            elapsed = positive(row["total_ns"], f"conversion {case} {variant}")
            require(frames >= 20, f"conversion {case}: fewer than 20 frames")
            require(round_number not in records[case][variant],
                    f"Duplicate conversion round: {case} {variant} {round_number}")
            records[case][variant][round_number] = {
                "frames": frames, "ms": elapsed / frames / 1e6,
                "position": int(row["position"])}
    validate_variants(records, "conversion")
    groups = {}
    for case, variants in records.items():
        require(set(variants["before"]) == set(variants["after"]),
                f"conversion {case}: before/after rounds differ")
        rounds = sorted(variants["before"])
        require(rounds == list(range(1, expected_rounds + 1)),
                f"conversion {case}: expected {expected_rounds} rounds")
        frame_counts = {sample["frames"] for samples in variants.values()
                        for sample in samples.values()}
        require(len(frame_counts) == 1,
                f"conversion {case}: before/after frame counts differ")
        for round_number in rounds:
            require({variants[variant][round_number]["position"]
                     for variant in VARIANTS} == {1, 2},
                    f"conversion {case}: invalid paired execution order")
        groups[case] = {}
        for variant in VARIANTS:
            require((case, variant) in checks,
                    f"conversion {case} {variant}: missing correctness check")
            groups[case][variant] = [variants[variant][number]["ms"]
                                     for number in rounds]
    return groups


def capture_results(runs, expected_rounds):
    groups = defaultdict(lambda: defaultdict(list))
    metrics = defaultdict(lambda: defaultdict(list))
    seen_runs = set()
    case_names = None
    backends = set()
    compilers = set()
    pixman_versions = set()
    for run in runs:
        variant, round_number = run["variant"], int(run["round"])
        require(variant in VARIANTS, f"Unknown capture variant: {variant}")
        require((variant, round_number) not in seen_runs,
                f"Duplicate capture run: {variant} {round_number}")
        seen_runs.add((variant, round_number))
        result = run["result"]
        backends.add(result["backend"])
        compilers.add(result["cflags"])
        pixman_versions.add(result["pixman_version"])
        cases = result["cases"]
        names = {case["case"] for case in cases}
        require(len(names) == len(cases), "Duplicate capture case in one run")
        if case_names is None:
            case_names = names
        require(names == case_names, "Capture runs contain different cases")
        for case in cases:
            name = case["case"]
            iterations = int(case["iterations_per_sample"])
            require(iterations > 0, f"capture {name}: invalid iteration count")
            require(case["samples_ns_total"], f"capture {name}: no timed samples")
            for total in case["samples_ns_total"]:
                elapsed = positive(total, f"capture {name} {variant}")
                groups[name][variant].append(elapsed / iterations / 1e6)
            metrics[name][variant].append(case)
    validate_variants(groups, "capture")
    expected_runs = {(variant, number) for variant in VARIANTS
                     for number in range(1, expected_rounds + 1)}
    require(seen_runs == expected_runs, "Capture rounds are incomplete")
    require(len(backends) == len(compilers) == len(pixman_versions) == 1,
            "Capture backend, compiler flags, or Pixman version differ between runs")
    for name, variants in metrics.items():
        cases = [case for samples in variants.values() for case in samples]
        for field in ("validation_digest", "emitted_tiles", "width", "height",
                      "input_rectangles", "input_pixels", "iterations_per_sample"):
            require(len({case[field] for case in cases}) == 1,
                    f"capture {name}: before/after or repeated-run {field} mismatch")
        for variant, samples in variants.items():
            for field in ("capture_rectangles", "capture_pixels"):
                require(len({case[field] for case in samples}) == 1,
                        f"capture {name} {variant}: inconsistent {field}")
        require(len(groups[name]["before"]) == len(groups[name]["after"]),
                f"capture {name}: timing sample counts differ")
    settings = {"backend": next(iter(backends)), "cflags": next(iter(compilers)),
                "pixman": next(iter(pixman_versions))}
    return groups, metrics, settings


LIVE_METRICS = {
    "scattered": ("draw_to_capture_ms", "Scattered drawing after idle → local capture"),
    "full_frame": ("draw_to_capture_ms", "Full-frame drawing after idle → local capture"),
    "empty_ack": ("draw_to_capture_ms", "Drawing after an empty ACK → local capture"),
    "partial_header": ("xsync_ms", "XSync during a partial protocol header"),
    "partial_body": ("xsync_ms", "XSync during a partial protocol body"),
    "blocked_output": ("xsync_ms", "XSync while the local peer pauses reads"),
}


def live_results(runs):
    if not runs:
        return {}, {}, {}
    groups = defaultdict(lambda: defaultdict(list))
    work = defaultdict(lambda: defaultdict(list))
    sessions = defaultdict(set)
    configurations = []
    info_digests = set()
    workload_names = None
    for run in runs:
        variant, session = run["variant"], int(run["session"])
        require(variant in VARIANTS, f"Unknown live variant: {variant}")
        require(session not in sessions[variant],
                f"Duplicate live session: {variant} {session}")
        sessions[variant].add(session)
        result = run["result"]
        require(result["status"] == "passed",
                f"Live session {variant} {session} failed: {result.get('error', '')}")
        config = result["config"]
        if any(name in result["samples"] for name in ("scattered", "full_frame")):
            idle_ms = positive(config.get("draw_idle_ms", 0), "live draw idle interval")
            require(idle_ms >= 2 * config["frame_interval_ms"],
                    "Live draw controls must let previous frame deadlines expire")
        configurations.append({key: value for key, value in config.items()
                               if key != "label"})
        info_digests.add(result["client_info_sha256"])
        names = set(result["samples"])
        if workload_names is None:
            workload_names = names
        require(names == workload_names, "Live sessions contain different workloads")
        for workload, samples in result["samples"].items():
            require(workload in LIVE_METRICS, f"Unknown live workload: {workload}")
            metric, title = LIVE_METRICS[workload]
            require(len(samples) == config["trials"],
                    f"live {workload}: incomplete trial count")
            require({sample["trial"] for sample in samples} ==
                    set(range(config["trials"])), f"live {workload}: invalid trial IDs")
            for sample in samples:
                if workload in ("scattered", "full_frame"):
                    require(sample.get("pacing") == "idle_before_draw",
                            f"live {workload}: missing fixed idle precondition")
                    actual_idle = positive(sample.get("idle_before_draw_ms", 0),
                                           f"live {workload} actual idle")
                    ack_elapsed = positive(sample.get("ack_to_draw_ms", 0),
                                           f"live {workload} ACK-to-draw interval")
                    require(actual_idle >= config["draw_idle_ms"] * 0.95 and
                            ack_elapsed >= actual_idle,
                            f"live {workload}: idle precondition did not hold")
                groups[title][variant].append(positive(sample[metric],
                                                       f"live {workload} {variant}"))
                if "captured_pixels" in sample:
                    work[workload][variant].append(sample["captured_pixels"])
                if workload == "blocked_output":
                    require(sample["replies_exact"] is True and
                            sample["received_reply_bytes"] == sample["expected_reply_bytes"],
                            f"live {variant}: blocked output lost or corrupted replies")
    validate_variants(groups, "live")
    require(sessions["before"] == sessions["after"], "Live session counts differ")
    require(all(config == configurations[0] for config in configurations),
            "Live configurations differ between sessions")
    require(len(info_digests) == 1, "Live client capabilities differ between sessions")
    config = dict(configurations[0])
    config["session_count"] = len(sessions["before"])
    return groups, work, config


def environment_lines(metadata):
    cpu = "Not recorded"
    hypervisor = "Not reported"
    for line in metadata.get("cpu_info", "").splitlines():
        key, separator, value = line.partition(":")
        if separator and key.strip() == "Model name":
            cpu = value.strip()
        if separator and key.strip() == "Hypervisor vendor":
            hypervisor = value.strip()
    lines = [f"- Recorded UTC: {cell(metadata.get('created_utc', 'Not recorded'))}",
             f"- Machine: {cell(metadata.get('machine', 'Not recorded'))}",
             f"- CPU: {cell(cpu)}; affinity: {cell(metadata.get('cpu_affinity', []))}",
             f"- Hypervisor: {cell(hypervisor)}",
             f"- Compiler: {cell(metadata.get('compiler', 'Not recorded'))}",
             f"- Before revision: `{metadata['baseline']}`",
             f"- After: working tree based on `{metadata.get('working_head', 'unknown')}`"]
    if metadata.get("working_module_sha256"):
        value = metadata["working_module_sha256"]
        if isinstance(value, dict):
            lines.append(f"- Recorded after-source hashes: {len(value)} module files "
                         "(full hashes in results.json).")
        else:
            lines.append(f"- After module-source SHA-256: `{value}`")
    if metadata.get("working_diff_sha256"):
        lines.append(f"- Tracked module diff SHA-256: `{metadata['working_diff_sha256']}`")
    for variant in VARIANTS:
        build = metadata.get("module_builds", {}).get(variant)
        if build:
            lines.append(f"- {variant.title()} module configure options: "
                         f"`{cell(build['configure'])}`")
    return lines


def render(results, source):
    metadata = results["metadata"]
    rounds = int(metadata["rounds"])
    require(rounds >= 9, "At least nine rounds are required")
    conversion = conversion_results(results["conversion"], rounds)
    capture, capture_metrics, settings = capture_results(results["capture"], rounds)
    live, live_work, live_config = live_results(results.get("live", []))
    lines = ["# xorgxrdp performance comparison", "",
             "Measured results compare the original Git revision with the modified "
             "working tree. Values are medians; brackets show the full observed "
             "min–max range. Positive time reductions mean faster execution, and "
             "negative reductions mean slower execution. Speedup is before / after.",
             "", "## Environment and baseline", "", *environment_lines(metadata), "",
             f"Raw input: `{source}`. All summaries below are recomputed from raw "
             "samples; warmup trials are excluded.", "", "## ARGB-to-ABGR conversion", "",
             f"{rounds} alternating before/after rounds per case, at least 20 frames "
             "per round, with equal frame counts and the same source/destination "
             "buffers for both variants. Values are milliseconds per conversion. "
             "Both variants passed byte-exact pixel, alpha, padding and source "
             "immutability checks before timing.", "", *timing_table(conversion), "",
             "Different aligned strides retain 16-byte row alignment and serve as "
             "controls. Changing row alignment uses source/destination pitches of "
             "width × 4 + 4 / width × 4 + 12 bytes. These rows alternate between "
             "matched and mismatched 16-byte alignment.", "",
             "## Region preparation and RFX capture", "",
             f"{rounds} alternating rounds per variant. The production region "
             "preparation and CPU capture code are timed together; the network send "
             "is stubbed. Damage reset, source selection and pixel validation are "
             "outside the timed interval. Values are milliseconds per capture iteration.",
             "", "Sparse cases place 10 or 32 separate 8×8 rectangles in two clusters "
             "near opposite corners of a 4K screen. Their bounding box spans the screen; "
             "this deliberately stresses bounding-box amplification. Changed runs alter "
             "only those damaged pixels. The capture shim prevents inlining across the "
             "client/capture file boundary; other harness compiler decisions can still "
             "differ from the full Xorg module.",
             "", f"Backend: `{settings['backend']}`; Pixman: `{settings['pixman']}`; "
             f"compiler flags: `{cell(settings['cflags'])}`.", "",
             *timing_table(capture), "",
             "Validation digests and emitted tile counts match between before/after "
             "for every case and every repeated run. Capture work below is recorded "
             "outside timing; it describes the pixels considered by the capture path, "
             "not network bytes.", "",
             "| Case | Capture pixels before | Capture pixels after | Pixel reduction | Emitted tiles, both |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for name, variants in capture_metrics.items():
        before, after = variants["before"][0], variants["after"][0]
        pixels_before, pixels_after = before["capture_pixels"], after["capture_pixels"]
        require(pixels_before > 0, f"capture {name}: invalid pixel work count")
        lines.append(f"| {cell(label(name))} | {pixels_before:,} | {pixels_after:,} | "
                     f"{100 * (1 - pixels_after / pixels_before):.2f}% | "
                     f"{before['emitted_tiles']:,} |")
    lines.extend(["", "## Live Xorg and local transport", ""])
    if live:
        lines.extend([
            f"{live_config['session_count']} independent Xorg sessions per variant, "
            f"{live_config['trials']} measured trials per workload per session, "
            f"{live_config['width']}×{live_config['height']} pixels, and a "
            f"{live_config['frame_interval_ms']} ms frame interval. The table pools "
            "the measured trials across sessions. Each session alternates with the "
            "other variant; warmups are excluded.", "", *timing_table(live), "",
            "Drawing latency ends when the local benchmark peer receives the capture. "
            f"Scattered and full-frame controls wait {live_config.get('draw_idle_ms', 'unspecified')} "
            "ms before starting the timed draw, allowing prior frame deadlines to expire. "
            "XSync measures whether another X client can make progress while the local "
            "protocol peer delays data or reads.", "",
            f"Partial header/body tests delay completion by "
            f"{live_config['pause_ms']} ms; blocked output pauses reads for "
            f"{live_config['backpressure_pause_ms']} ms while requesting "
            f"{live_config['backpressure_requests']} replies. All live sessions "
            "reported successful pixel/protocol verification; blocked-output replies "
            "were complete and byte-exact."])
        if live_work:
            lines.extend(["", "Median captured pixel counts in the live draw tests:", "",
                          "| Workload | Before | After |",
                          "| --- | ---: | ---: |"])
            for name, variants in live_work.items():
                require(set(variants) == set(VARIANTS),
                        f"live {name}: missing captured-pixel counts")
                lines.append(f"| {cell(label(name))} | "
                             f"{statistics.median(variants['before']):,.0f} | "
                             f"{statistics.median(variants['after']):,.0f} |")
    else:
        lines.append("No live-session measurements were supplied.")
    lines.extend(["", "## Interpretation limits", "",
                  "- Results describe this machine and these workloads. Min–max "
                  "ranges show variability; they are not confidence intervals.",
                  "- Conversion uses reused CPU frame buffers and AMD64 assembly. "
                  "It does not time allocation, GPU readback, encoding, or display.",
                  "- Capture compares region preparation plus the RFX CPU capture "
                  "path. Pixel-work reduction is not a measured bandwidth saving.",
                  "- Live tests use a local Unix-socket peer and synthetic drawing. "
                  "They do not measure WAN latency, an RDP encoder, a remote client's "
                  "display rate, or end-to-end RDP FPS.",
                  "- Microbenchmark speedups do not imply the same percentage "
                  "improvement for an entire desktop session.", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        results = json.loads(args.input.read_text())
        report = render(results, args.input.resolve())
    except (KeyError, TypeError, ValueError) as error:
        print(f"Invalid benchmark results: {error}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report)
    print(f"Report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
