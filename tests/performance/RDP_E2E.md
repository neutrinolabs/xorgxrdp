# Full RDP frame-rate comparison

This suite drives a real Xorg display through xrdp, TCP/TLS, and FreeRDP's
software decoder. It compares original and modified **xorgxrdp modules** while
keeping the xrdp encoder and FreeRDP client fixed. It measures complete decoded
frames, drawing-to-decoded-frame latency, and process CPU time.

See the [recorded results](RDP_RESULTS.md) and [measurement data](rdp-e2e-results.json).

The recorded comparison uses a 1920×1080 synthetic desktop at 60 submitted
frames/second, two seconds of warmup, ten seconds of measurement, and three
alternating before/after pairs for each workload and codec. RFX retains its
32 ms capture interval; AVC420 uses 16 ms, OpenH264, a 20 Mbps target, and no
encoder frame skipping. The source may still be coalesced by the display stack.

Full-screen motion uploads a deterministic scrolling texture to an offscreen
pixmap, then presents it with one XCopyArea request. Sparse drawing changes 32
separate 16×16 rectangles over a static textured desktop. Both workloads carry
two frame-ID strips, adding 16,384 pixels per submitted frame. The
[marker specification](desktop_workload.md) describes CRC and consistency checks.

The client observes the composed software framebuffer after an actual GFX
EndFrame. It verifies matching top/bottom markers and counts each complete source
ID once. Drawing-loop iterations and protocol frame counts are not used as FPS.
Dropped source IDs can reflect capture coalescing; they do not establish packet
loss. CSV traces are retained for independent joins and analysis.

Requirements: Linux, Xorg, Xlib development files, FreeRDP 3 development files
with GFX and software RFX/H.264 decoding, matching before/after xorgxrdp builds,
and a private compatible xrdp runtime with RFX/OpenH264 support. The existing
system services and displays must remain separate from this suite.

Example compilation with a private FreeRDP installation:

```sh
cc -O2 -std=c11 -Wall -Wextra -Werror \
  tests/performance/desktop_workload.c -o /tmp/desktop_workload -lX11
cc -O2 -g -std=c11 -Wall -Wextra -Werror \
  -isystem /tmp/xrdp-e2e/client-build/runtime/include/freerdp3 \
  -isystem /tmp/xrdp-e2e/client-build/runtime/include/winpr3 \
  tests/performance/rdp_client_bench.c -o /tmp/rdp_client_bench \
  -L/tmp/xrdp-e2e/client-build/runtime/lib \
  -lfreerdp-client3 -lfreerdp3 -lwinpr3 \
  -Wl,-rpath,/tmp/xrdp-e2e/client-build/runtime/lib
/tmp/desktop_workload --self-test
/tmp/rdp_client_bench --self-test
```

xrdp embeds the path to `gfx.toml` independently of `--config`. For this test,
the preparation helper copies an existing private runtime to a new prefix of
the same byte length and updates embedded paths. It verifies that every copied
ELF executable code section remains identical. Existing destinations are refused;
the original runtime is unchanged. The runtime used here has an executable named
`sbin/xrdp-optimized`; it is held fixed for **both** xorgxrdp variants.

```sh
python3 tests/performance/prepare_rdp_server.py \
  --source-runtime /tmp/xrdp-e2e/runtime \
  --destination /tmp/xrdp-fps/runtime --write-codec-config rfx
python3 tests/performance/rdp_e2e.py \
  --baseline 59d0735c4321b5baff3feaa980a01f78a1c1e96f \
  --before-build /tmp/xorgxrdp-before-build \
  --after-build /tmp/xorgxrdp-after-build \
  --server-runtime /tmp/xrdp-fps/runtime \
  --client /tmp/rdp_client_bench \
  --client-library-dir /tmp/xrdp-e2e/client-build/runtime/lib \
  --workload /tmp/desktop_workload \
  --output /tmp/xorgxrdp-rdp-results --pairs 3 --seconds 10 --warmup 2
python3 tests/performance/rdp_e2e_report.py \
  --input /tmp/xorgxrdp-rdp-results/results.json \
  --output /tmp/xorgxrdp-rdp-results/report.md
```

Build both module trees with the same dependencies, configure options, and
compiler flags. Supply an unused local display numbered 20 or higher and an
unused loopback TCP port; defaults are `:98` and `3397`. The output directory
must not exist. The prepared runtime must be dedicated to this suite because
the runner changes its private codec configuration between sessions.

Default CPU affinities are xrdp on CPUs 0–1, FreeRDP on 2–3, Xorg on 4, and the
source workload on 5. Override these with the runner's `--*-cpus` arguments when
necessary. CPU percentages use one core as 100%; xrdp and FreeRDP can exceed 100%.
All trials are sequential, and the order alternates within each adjacent pair.
The runner records binary/library/configuration hashes per trial and cleans up
only the processes it starts.

These measurements include real software encoding and decoding over local RDP.
They exclude physical monitor scanout, input-device latency, WAN delay, network
congestion, hardware codecs, and video-quality assessment. A fixed frame cap
can hide CPU savings from the FPS number, so compare CPU per decoded frame too.
Three paired trials provide an exploratory comparison; frame observations
within one trial are not independent statistical replicates.
