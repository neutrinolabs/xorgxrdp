# Before/after performance benchmarks

This opt-in suite compares a Git revision with the working tree. It checks output
correctness and records raw timing samples. It has no timing thresholds and is
not part of `make check`.

The recorded comparison is in [RESULTS.md](RESULTS.md), with all measured samples
and build/source provenance in [results.json](results.json).

The subsequent full RDP encode/transport/decode comparison is in
[RDP_RESULTS.md](RDP_RESULTS.md), with [reproduction instructions](RDP_E2E.md).

The complete runner requires Linux on x86-64, Python 3, a C compiler, NASM,
binutils, pkg-config, pixman development files, Xorg, and libX11. Live tests also
need two built xorgxrdp trees and the xrdp headers used to build them.

Build the original revision in a separate source/build directory and build the
working tree with identical compiler flags, configure options, and dependencies.
For example, export the original with `git archive BASELINE | tar -x -C DIR`, run
its `./bootstrap`, and configure an out-of-tree build. Do the same for the working
tree. The recorded run used `-O2 -g -Wall -Wwrite-strings -Werror`,
`--enable-strict-locations`, and software capture. Both builds used the same xrdp
headers. Do not run this against an existing user's Xorg session.

```sh
python3 tests/performance/run.py \
  --baseline 59d0735c4321b5baff3feaa980a01f78a1c1e96f \
  --before-build /tmp/xorgxrdp-before-build \
  --after-build /tmp/xorgxrdp-after-build \
  --xrdp-source /path/to/xrdp \
  --output /tmp/xorgxrdp-performance-results \
  --cpu 2 --rounds 9 --display :99
```

Use an unused display and an available CPU. The output directory must be new;
source, output, and module build paths must not contain whitespace for live tests.
`--skip-live` runs just the conversion and capture benchmarks without requiring
module builds or xrdp headers. Compilation completes before measurement starts.
Measurements run sequentially on one CPU; before/after order alternates. Avoid
other heavy workloads while measuring.

The runner saves `results.json`, a Markdown report, each live Xorg session's logs,
and the generated benchmark sources/binaries. Record the baseline explicitly:
`HEAD` changes after commits. The report can be regenerated independently:

```sh
python3 tests/performance/report.py \
  --input /tmp/xorgxrdp-performance-results/results.json \
  --output /tmp/xorgxrdp-performance-results/report.md
```

| Workload | Measurement | Correctness check |
| --- | --- | --- |
| ABGR conversion, 1080p and 4K | Actual original/current assembly, aligned and independently misaligned buffers, differing row strides | Every output byte, alpha, padding, guards, unchanged source |
| RFX capture, 4K | Actual revision-specific region preparation, conversion, hashing, and tile processing; 10 sparse, 32 sparse, and full-screen rectangles; changed/unchanged pixels | Damage coverage, matching damaged-pixel digests and emitted tile counts |
| Live scattered/full-screen draws | Capture area and local draw-to-capture latency after 90 ms idle at a 40 ms frame interval | Captured damage coverage, pixels, increasing frame IDs, shared-memory FD markers |
| Live empty ACK | Delay from new damage after an ACK with no pending damage | Captured pixels and frame IDs |
| Live partial input | XSync response while a packet header/body remains incomplete for 100 ms | Valid reply after completion |
| Live blocked output | XSync response while the peer pauses reading for 80 ms | All 1,000 replies received byte for byte |

Conversion uses warmed, reused frame buffers. Capture extracts unmodified
production function bodies and adapts Xorg regions to pixman; network sends are
stubbed. A non-inlined capture shim preserves the call boundary between the
production client and capture files, avoiding cross-file compiler optimizations
that the actual module build does not make. It excludes GPU readback and encoding.
Live tests use a local synthetic
protocol peer with software capture, not a complete remote desktop client.
Results describe these workloads on the recorded machine; they do not establish
remote FPS, network throughput, or GPU performance. Min/max ranges describe sample
variation, not statistical confidence intervals.
