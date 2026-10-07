# Actual RDP desktop workload

`desktop_workload.c` paints an isolated X11 display. It does not emulate RDP,
encode video, or measure client FPS itself. Run it alongside a real RDP client
that decodes the markers below. Do not run it on a user's desktop; explicit
display numbers 0 and 10 are refused.

```sh
cc -O2 -std=c11 -Wall -Wextra tests/performance/desktop_workload.c \
  -o /tmp/desktop_workload $(pkg-config --cflags --libs x11)
/tmp/desktop_workload --self-test
/tmp/desktop_workload --display :99 --mode fullframe \
  --width 1920 --height 1080 --fps 60 --warmup 3 --seconds 20 \
  --output-prefix /tmp/fullframe-source
```

The display dimensions must exactly match the workload dimensions. The default
is 1920x1080, 60 FPS, 3 seconds warmup, and 20 seconds measurement. `--mode`
accepts `fullframe` or `sparse32`. `--start-ns` optionally sets a future start
in the host's `CLOCK_MONOTONIC` domain. Otherwise drawing starts one second
after setup. `--hold-seconds` defaults to 2 and preserves the final window while
the decoder drains. `WORKLOAD_READY` on stderr reports the exact measurement
window before the workload starts.

Full-frame mode scrolls a deterministic detailed texture in both axes, including
color gradients, fine texture, panels and text-like lines. Every frame is
uploaded through `XPutImage` into an offscreen pixmap, then presented with one
`XCopyArea` onto the visible window. This avoids exposing an incomplete frame
when Xlib splits the upload into several protocol requests. The upload and
presentation copy both occur after the recorded submission timestamp. This is
synthetic desktop/video-like motion, not playback of a compressed video file.
The texture is precomputed, then copied into a new frame with the current marker.
Source frames are distinct because their marker IDs are unique.

Sparse mode first paints the same textured desktop, then changes 32 independent
16x16 status rectangles in an 8x4 grid spanning the screen. It uses separate
`XFillRectangle` calls with different foreground colors so Xlib does not combine
the rectangle requests. Both marker strips also change. These marker strips add
16,384 submitted pixels per frame to the 8,192 status pixels, which must be
disclosed when reporting the sparse workload. Xorg and RDP remain free to merge
these requests into frames normally.

Pacing uses absolute `CLOCK_MONOTONIC` deadlines. Missed source slots are skipped
instead of generating catch-up bursts. Each frame ends with `XFlush` and `XSync`,
so a slow X server can reduce source FPS and cannot create an unbounded source
request queue. Report source FPS together with decoded client FPS.

## Marker contract

There are two identical 512x16 pixel strips. Their top-left coordinates are
`(32, 32)` and `(32, height - 48)`. Each strip has 64 columns and two rows of
8x8 cells. Column 0 is the least-significant bit. A set bit is white `0xffffff`;
an unset bit is black. The second row complements the first.

The 64-bit packet is:

| Bits | Meaning |
| --- | --- |
| 0–23 | Frame ID, starting at 1; setup marker 0 is not a measured frame |
| 24–39 | Reserved, always zero |
| 40–47 | CRC-8 of bits 0–39 |
| 48–63 | Magic `0xb67a` |

CRC starts at `0x5a`. Process the five payload bytes least-significant first.
For each byte, XOR it into the CRC, then repeat eight times:
`crc = ((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0)) & 0xff`.

The center sample coordinates are `x = 36 + 8 * bit`, with top-strip rows
`y = 36, 44` and bottom-strip rows `y = height - 44, height - 36`. Use a luma or
RGB-sum threshold midway between black and white. Validate complements, magic,
CRC, reserved bits, and agreement between both strips. Observe the decoded
client buffer after its paint callback completes; reject torn or corrupt
markers. Count each new valid ID only once. Dropped source IDs indicate frames
not observed as complete, rather than necessarily network packet loss.

## Source records and latency

`PREFIX.frames.csv` and `PREFIX.summary.json` are written after drawing finishes;
records are buffered in memory during the measured workload. Timestamps use
nanoseconds from `CLOCK_MONOTONIC`, matching the client observer on the same host.

CSV columns are `frame_id,phase,target_ns,generation_begin_ns,ready_ns,submit_ns,`
`flushed_ns,server_done_ns`. `phase` is `warmup` or `measured`, determined by the
frame's submission timestamp. `target_ns` is its source pacing deadline;
`generation_begin_ns` and `ready_ns` bracket source image preparation;
`submit_ns` precedes the first X11 drawing call; `flushed_ns` follows `XFlush`;
and `server_done_ns` follows `XSync`, confirming Xorg processed the requests.

For an observed client frame, subtract its matching source `submit_ns` from the
client observation timestamp to measure drawing-submission-to-decoded-paint
latency. This includes local X11 submission, server capture and encoding, RDP
transport, and client decode. It does not measure physical display scanout.
`server_done_ns` can occur after client observation and should not replace the
submission timestamp in this calculation.

Restrict decoded FPS counts to the summary's measurement time window. Retain
post-window observations for drain diagnostics; do not increase the FPS count
by counting them in a shorter interval. Summaries expose generated frame counts,
source FPS, skipped pacing slots, and sparse/marker pixel counts.
