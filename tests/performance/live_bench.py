#!/usr/bin/env python3
"""Run identical capture/scheduler/socket workloads against a live Xorg.

Compile client_info.c against the xrdp headers used for each build, then emit
its native struct to a binary file. Run this client using xorg-test-run.sh's
XCLIENT, or against a dedicated Xorg with the same XRDP_SOCKET_PATH.

Example config:
{"label":"before", "trials":9, "warmup_trials":1, "width":1024,
 "height":768, "frame_interval_ms":40, "pause_ms":100}

The JSON contains every trial, discarded warmups, and summaries. It does not
assert that a revision is faster. Software rendering and identical Xorg/build
configuration are the runner's responsibility. Run one benchmark at a time.

Scattered and full-frame controls wait two frame intervals plus 10 ms after
the preceding ACK, outside their timed draw-to-capture interval. This allows
the baseline's empty ACK timer and its resulting frame deadline to expire;
it also avoids the scheduler's same-millisecond rate-limit exception. Override
this idle period with draw_idle_ms. Each sample records its actual idle period
and ACK-to-draw time. These controls measure capture after idle, not throughput.

The dedicated empty_ack workload intentionally holds an ACK for one interval
plus 5 ms, releases it, then draws after ack_settle_ms. Fragmented-input peers
finish a partial message after pause_ms while XSync measures server response.
Blocked-output peers resume reading after backpressure_pause_ms and verify
every capability reply. These workload phases are deliberately distinct.
"""

import argparse
import array
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import resource
import signal
import socket
import statistics
import struct
import threading
import time


DEFAULTS = {
    "trials": 9,
    "warmup_trials": 1,
    "width": 1024,
    "height": 768,
    "frame_interval_ms": 40,
    "draw_idle_ms": None,
    "pause_ms": 100,
    "backpressure_pause_ms": 80,
    "backpressure_requests": 1000,
    "fragment_settle_ms": 10,
    "ack_settle_ms": 10,
    "timeout_seconds": 30,
    "workloads": ["scattered", "full_frame", "empty_ack",
                  "partial_header", "partial_body", "blocked_output"],
}


def milliseconds(start):
    return (time.perf_counter() - start) * 1000


class XClient:
    def __init__(self, display):
        self.lib = ctypes.CDLL("libX11.so.6")
        pointer = ctypes.c_void_p
        ulong = ctypes.c_ulong
        integer = ctypes.c_int
        uint = ctypes.c_uint
        signatures = {
            "XOpenDisplay": ([ctypes.c_char_p], pointer),
            "XDefaultRootWindow": ([pointer], ulong),
            "XCreateGC": ([pointer, ulong, ulong, pointer], pointer),
            "XSetForeground": ([pointer, pointer, ulong], integer),
            "XFillRectangle": ([pointer, ulong, pointer, integer, integer,
                                uint, uint], integer),
            "XNoOp": ([pointer], integer),
            "XSync": ([pointer, integer], integer),
            "XFreeGC": ([pointer, pointer], integer),
            "XCloseDisplay": ([pointer], integer),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes = arguments
            function.restype = result
        self.display = self.lib.XOpenDisplay(display.encode())
        if not self.display:
            raise RuntimeError("Cannot open X display " + display)
        self.root = self.lib.XDefaultRootWindow(self.display)
        self.gc = self.lib.XCreateGC(self.display, self.root, 0, None)
        if not self.gc:
            self.lib.XCloseDisplay(self.display)
            raise RuntimeError("Cannot create X GC")

    def draw(self, rectangles, color):
        self.lib.XSetForeground(self.display, self.gc, color)
        for rectangle in rectangles:
            self.lib.XFillRectangle(self.display, self.root, self.gc, *rectangle)
            # Prevent Xlib from combining separate draws into one request.
            # Xorg reports bounding-box damage for a single PolyFillRectangle.
            self.lib.XNoOp(self.display)
        self.lib.XSync(self.display, 0)

    def sync(self):
        self.lib.XNoOp(self.display)
        self.lib.XSync(self.display, 0)

    def close(self):
        self.lib.XFreeGC(self.display, self.gc)
        self.lib.XCloseDisplay(self.display)


class CaptureClient:
    VERSION = struct.pack("<IH5I", 26, 103, 301, 1, 0, 0, 0)

    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(path)
        self.last_frame_id = 0
        self.last_ack_time = None
        self.metadata = []

    def exact(self, length):
        data = bytearray()
        while len(data) < length:
            part = self.socket.recv(length - len(data))
            if not part:
                raise RuntimeError("Unexpected Xorg disconnect")
            data.extend(part)
        return data

    def message(self):
        kind, count, size = struct.unpack("<HHI", self.exact(8))
        if size > 16 * 1024 * 1024:
            raise RuntimeError("Invalid protocol message size")
        return kind, count, self.exact(size)

    def version(self):
        kind, count, body = self.message()
        if kind != 2 or count != 1 or len(body) != 8:
            raise RuntimeError("Unexpected capability reply")
        self.capability_reply = struct.pack("<HHI", kind, count, len(body)) + body

    def descriptor(self):
        marker = bytearray()
        descriptors = []
        while len(marker) < 4:
            data, ancillary, flags, _ = self.socket.recvmsg(
                4 - len(marker), socket.CMSG_SPACE(4))
            if not data:
                raise RuntimeError("Disconnect before descriptor marker")
            marker.extend(data)
            if flags & socket.MSG_CTRUNC:
                raise RuntimeError("Truncated descriptor ancillary data")
            for level, kind, values in ancillary:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    fds = array.array("i")
                    fds.frombytes(values[:len(values) // fds.itemsize * fds.itemsize])
                    descriptors.extend(fds)
        if marker != b"int\0" or len(descriptors) != 1:
            for descriptor in descriptors:
                os.close(descriptor)
            raise RuntimeError("Invalid shared-memory descriptor marker")
        return descriptors[0]

    def capture(self):
        for _ in range(30):
            kind, count, body = self.message()
            if kind != 3:
                self.metadata.append({"kind": kind, "count": count,
                                      "bytes": len(body)})
                continue
            offset = 0
            frame = None
            cursor_descriptors = 0
            for _ in range(count):
                command, size = struct.unpack_from("<HH", body, offset)
                if size < 4 or offset + size > len(body):
                    raise RuntimeError("Invalid update command size")
                item = body[offset:offset + size]
                offset += size
                if command == 63:
                    cursor_descriptors += 1
                elif command == 64:
                    if frame is not None:
                        raise RuntimeError("Multiple captures in one update")
                    position = 4
                    regions = {}
                    for name in ("dirty", "copied"):
                        rectangles = struct.unpack_from("<H", item, position)[0]
                        position += 2
                        regions[name] = [struct.unpack_from("<4H", item,
                                        position + i * 8) for i in range(rectangles)]
                        position += rectangles * 8
                    flags, frame_id, shm_bytes, shm_offset = struct.unpack_from(
                        "<4I", item, position)
                    position += 16
                    left, top, width, height = struct.unpack_from("<4H", item,
                                                                position)
                    if position + 8 != len(item):
                        raise RuntimeError("Unexpected capture command layout")
                    frame = dict(regions, flags=flags, id=frame_id,
                                 shm_bytes=shm_bytes, shm_offset=shm_offset,
                                 left=left, top=top, width=width, height=height)
            if offset != len(body):
                raise RuntimeError("Update command count mismatch")
            for _ in range(cursor_descriptors):
                os.close(self.descriptor())
            if frame is not None:
                descriptor = self.descriptor()
                try:
                    if os.fstat(descriptor).st_size < frame["shm_bytes"]:
                        raise RuntimeError("Framebuffer descriptor is too small")
                    frame["memory"] = mmap.mmap(descriptor, frame["shm_bytes"],
                                                access=mmap.ACCESS_READ)
                finally:
                    os.close(descriptor)
                if frame["id"] <= self.last_frame_id:
                    frame["memory"].close()
                    raise RuntimeError("Capture IDs did not increase")
                self.last_frame_id = frame["id"]
                return frame
        raise RuntimeError("No capture after 30 protocol messages")

    def ack(self, frame):
        self.socket.sendall(struct.pack("<IHII", 14, 106,
                                       frame["flags"], frame["id"]))
        self.last_ack_time = time.perf_counter()

    def close(self):
        self.socket.close()


def verify(frame, rectangles, color, config):
    if (frame["width"], frame["height"]) != (config["width"], config["height"]):
        raise RuntimeError("Negotiated capture dimensions differ from config")
    checked = 0
    for x, y, width, height in rectangles:
        # Checking intersection area permits a draw to cross capture rectangles.
        covered = sum(max(0, min(x + width, rx + rw) - max(x, rx)) *
                      max(0, min(y + height, ry + rh) - max(y, ry))
                      for rx, ry, rw, rh in frame["copied"])
        if covered != width * height:
            raise RuntimeError("Capture rectangles do not cover the drawn region")
        if width * height <= 1024:
            points = ((col, row) for row in range(y, y + height)
                      for col in range(x, x + width))
        else:
            # Large controls use sampled color checks outside the timed interval.
            points = ((x + dx, y + dy) for dx in (0, width // 2, width - 1)
                      for dy in (0, height // 2, height - 1))
        for col, row in points:
            offset = frame["shm_offset"] + (row * frame["width"] + col) * 4
            pixel = struct.unpack_from("<I", frame["memory"], offset)[0] & 0xffffff
            if pixel != color:
                raise RuntimeError("Captured pixel differs from drawn color")
            checked += 1
    return checked


def frame_sample(frame, started, rectangles, color, config):
    elapsed = milliseconds(started)
    checked = verify(frame, rectangles, color, config)
    return {
        "draw_to_capture_ms": elapsed,
        "capture_rectangles": len(frame["copied"]),
        "captured_pixels": sum(w * h for x, y, w, h in frame["copied"]),
        "drawn_pixels": sum(w * h for x, y, w, h in rectangles),
        "checked_pixels": checked,
        "frame_id": frame["id"],
        "rectangles": frame["copied"],
    }


def draw_trial(xclient, peer, config, rectangles, color):
    # Quiesce both versions before measuring capture. Immediate draw-after-ACK
    # can otherwise alternate between 4 ms and a full interval depending on
    # whether the scheduler observes the same millisecond as the prior frame.
    idle_start = time.perf_counter()
    time.sleep(config["draw_idle_ms"] / 1000)
    start = time.perf_counter()
    xclient.draw(rectangles, color)
    frame = peer.capture()
    try:
        sample = frame_sample(frame, start, rectangles, color, config)
        sample["pacing"] = "idle_before_draw"
        sample["idle_before_draw_ms"] = (start - idle_start) * 1000
        sample["ack_to_draw_ms"] = (start - peer.last_ack_time) * 1000
        peer.ack(frame)
        return sample
    finally:
        frame["memory"].close()


def empty_ack_trial(xclient, peer, config, trial):
    rectangle = [(32, 32, 8, 8)]
    color = 0x713000 + trial
    xclient.draw(rectangle, color)
    previous = peer.capture()
    previous_time = time.perf_counter()
    try:
        verify(previous, rectangle, color, config)
        # Let the preceding capture's deadline expire while its ACK is withheld.
        time.sleep((config["frame_interval_ms"] + 5) / 1000)
        peer.ack(previous)
    finally:
        previous["memory"].close()
    # The baseline schedules an empty update here and moves lastUpdateTime.
    # The new code has no empty update; both versions receive the same timing.
    time.sleep(config["ack_settle_ms"] / 1000)
    start = time.perf_counter()
    rectangle = [(64, 32, 8, 8)]
    color = 0x427000 + trial
    xclient.draw(rectangle, color)
    frame = peer.capture()
    try:
        sample = frame_sample(frame, start, rectangle, color, config)
        sample["previous_capture_to_draw_ms"] = (start - previous_time) * 1000
        peer.ack(frame)
        return sample
    finally:
        frame["memory"].close()


def fragment_trial(xclient, peer, config, split):
    errors = []
    released = []
    start = time.perf_counter()
    peer.socket.sendall(peer.VERSION[:split])

    def finish_packet():
        try:
            peer.socket.sendall(peer.VERSION[split:])
            released.append(milliseconds(start))
        except Exception as error:
            errors.append(str(error))

    timer = threading.Timer(config["pause_ms"] / 1000, finish_packet)
    timer.start()
    try:
        time.sleep(config["fragment_settle_ms"] / 1000)
        sync_start = time.perf_counter()
        xclient.sync()
        sync_ms = milliseconds(sync_start)
    finally:
        timer.join()
    if errors:
        raise RuntimeError("Packet completion failed: " + errors[0])
    peer.version()
    return {"xsync_ms": sync_ms, "peer_completion_ms": released[0],
            "trial_ms": milliseconds(start), "prefix_bytes": split}


def blocked_output_trial(xclient, peer, config):
    count = config["backpressure_requests"]
    expected = peer.capability_reply * count
    outcome = {"requested_replies": count, "expected_reply_bytes": len(expected),
               "received_reply_bytes": 0, "replies_exact": False}
    start = time.perf_counter()
    peer.socket.sendall(peer.VERSION * count)

    def drain():
        outcome["drain_started_ms"] = milliseconds(start)
        try:
            received = peer.exact(len(expected))
            outcome["received_reply_bytes"] = len(received)
            outcome["replies_exact"] = received == expected
        except Exception as error:
            # A dropped reply is a functional failure, not a faster transfer.
            outcome["reply_error"] = repr(error)
        outcome["drain_finished_ms"] = milliseconds(start)

    # Resume before the old sender's approximately 100 ms retry limit.
    timer = threading.Timer(config["backpressure_pause_ms"] / 1000, drain)
    timer.start()
    try:
        time.sleep(config["fragment_settle_ms"] / 1000)
        sync_started = time.perf_counter()
        xclient.sync()
        outcome["xsync_ms"] = milliseconds(sync_started)
    finally:
        timer.join()
    outcome["trial_ms"] = milliseconds(start)
    return outcome


def summary(samples):
    numeric = {}
    for name, trials in samples.items():
        numeric[name] = {}
        if not trials:
            continue
        for key, value in trials[0].items():
            if key in ("trial", "frame_id") or not isinstance(value, (int, float)):
                continue
            values = [trial[key] for trial in trials]
            numeric[name][key] = {"median": statistics.median(values),
                                  "min": min(values), "max": max(values),
                                  "mean": statistics.mean(values)}
    return numeric


def run(arguments, config, result):
    peer = None
    xclient = XClient(arguments.display)
    try:
        number = arguments.display.rsplit(":", 1)[1].split(".", 1)[0]
        peer = CaptureClient(str(Path(arguments.socket_dir) /
                                 ("xrdp_display_" + number)))
        peer.socket.sendall(peer.VERSION)
        peer.version()
        info = Path(arguments.client_info).read_bytes()
        result["client_info_bytes"] = len(info)
        result["client_info_sha256"] = hashlib.sha256(info).hexdigest()
        peer.socket.sendall(struct.pack("<IH", 6 + len(info), 104) + info)
        initial = peer.capture()
        try:
            if (initial["width"], initial["height"]) != (config["width"], config["height"]):
                raise RuntimeError("Client info and config dimensions disagree")
            peer.ack(initial)
        finally:
            initial["memory"].close()

        width, height = config["width"], config["height"]
        scattered = [(16 + col * (width - 32) // 8,
                      16 + row * (height - 32) // 4, 4, 4)
                     for row in range(4) for col in range(8)]
        for workload in config["workloads"]:
            result["samples"][workload] = []
            result["warmups"][workload] = []
            for trial in range(-config["warmup_trials"], config["trials"]):
                sequence = trial + config["warmup_trials"]
                if workload == "scattered":
                    sample = draw_trial(xclient, peer, config, scattered,
                                        0x127000 + sequence)
                    sample["bounding_box_pixels"] = (
                        (scattered[-1][0] + 4 - scattered[0][0]) *
                        (scattered[-1][1] + 4 - scattered[0][1]))
                elif workload == "full_frame":
                    sample = draw_trial(xclient, peer, config,
                                        [(0, 0, width, height)], 0x352000 + sequence)
                elif workload == "empty_ack":
                    sample = empty_ack_trial(xclient, peer, config, sequence)
                elif workload in ("partial_header", "partial_body"):
                    sample = fragment_trial(xclient, peer, config,
                                            1 if workload == "partial_header" else 10)
                elif workload == "blocked_output":
                    sample = blocked_output_trial(xclient, peer, config)
                else:
                    raise ValueError("Unknown workload " + workload)
                sample["trial"] = trial
                target = "warmups" if trial < 0 else "samples"
                result[target][workload].append(sample)
                if workload == "blocked_output" and not sample["replies_exact"]:
                    raise RuntimeError("Blocked-output workload lost or corrupted replies")
            print(workload + ": " + str(config["trials"]) + " trials complete",
                  flush=True)
        result["metadata"] = peer.metadata
    finally:
        if peer is not None:
            peer.close()
        xclient.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--display", "-display", required=True)
    parser.add_argument("--client-info", default=os.environ.get("LIVE_BENCH_CLIENT_INFO"),
                        required=not os.environ.get("LIVE_BENCH_CLIENT_INFO"))
    parser.add_argument("--config", default=os.environ.get("LIVE_BENCH_CONFIG"),
                        required=not os.environ.get("LIVE_BENCH_CONFIG"),
                        help="Trial configuration JSON")
    parser.add_argument("--output", default=os.environ.get("LIVE_BENCH_OUTPUT"),
                        required=not os.environ.get("LIVE_BENCH_OUTPUT"),
                        help="Raw results and summary JSON")
    parser.add_argument("--socket-dir", default=os.environ.get("XRDP_SOCKET_PATH"))
    arguments = parser.parse_args()
    if not arguments.socket_dir:
        parser.error("Set XRDP_SOCKET_PATH or --socket-dir")
    config = dict(DEFAULTS)
    config.update(json.loads(Path(arguments.config).read_text()))
    if config["draw_idle_ms"] is None:
        config["draw_idle_ms"] = 2 * config["frame_interval_ms"] + 10
    if (not 1 <= config["trials"] <= 100 or
            not 0 <= config["warmup_trials"] <= 10 or
            config["draw_idle_ms"] < 0 or
            min(config["width"], config["height"]) < 128 or
            config["pause_ms"] <= config["fragment_settle_ms"] or
            config["backpressure_pause_ms"] <= config["fragment_settle_ms"] or
            not 1 <= config["backpressure_requests"] <= 10000 or
            config["frame_interval_ms"] <= config["ack_settle_ms"]):
        parser.error("Invalid dimensions, counts, or timing configuration")
    result = {"status": "running", "config": config, "samples": {}, "warmups": {},
              "display": arguments.display,
              "verification": "Complete small-rectangle pixels; nine samples per large rectangle"}
    started = time.perf_counter()
    cpu_started = resource.getrusage(resource.RUSAGE_SELF)

    def timeout(_signal, _frame):
        raise TimeoutError("Live benchmark exceeded configured timeout")

    signal.signal(signal.SIGALRM, timeout)
    signal.alarm(config["timeout_seconds"])
    error = None
    try:
        run(arguments, config, result)
        result["status"] = "passed"
    except Exception as caught:
        error = caught
        result["status"] = "failed"
        result["error"] = repr(caught)
    finally:
        signal.alarm(0)
        result["elapsed_seconds"] = time.perf_counter() - started
        cpu_finished = resource.getrusage(resource.RUSAGE_SELF)
        result["client_cpu_seconds"] = (cpu_finished.ru_utime + cpu_finished.ru_stime -
                                        cpu_started.ru_utime - cpu_started.ru_stime)
        result["summary"] = summary(result["samples"])
        Path(arguments.output).write_text(json.dumps(result, indent=2) + "\n")
    if error is not None:
        raise error
    print("Results: " + arguments.output, flush=True)


if __name__ == "__main__":
    main()
