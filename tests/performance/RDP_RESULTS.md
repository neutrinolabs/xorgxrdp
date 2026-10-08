Actual RDP before/after comparison

1920×1080, target 60 source FPS, 10 measured seconds after 2 warmup seconds per trial; 3 paired trials per codec/workload. Adjacent trials are paired, with alternating execution order (before→after, then after→before).

Machine: AMD Ryzen 9 5900X 12-Core Processor; 8 logical CPUs; hypervisor: KVM.

The endpoint is the real FreeRDP software-decoded framebuffer after GFX EndFrame, using actual xrdp encoding and TLS RDP over TCP loopback. Complete decoded FPS counts distinct source IDs with matching valid top/bottom markers, observed within the measurement window. Duplicates and invalid or torn markers are excluded. This measures complete marked updates; it is not a count of every decoded surface or physical display scanout, and it does not simulate a WAN.

Latency runs from the first X11 drawing submission to the matching decoded-frame observation, using CLOCK_MONOTONIC on the same host. It includes X11 processing, capture, encoding, TCP/TLS and software decoding. Source texture generation before submission is excluded. Frames submitted during warmup but decoded inside the measurement window are included; frames decoded after that window are excluded.

Cells show median [minimum, maximum] across trials. Paired change is 100 × (after/before − 1); every pair is retained. Positive means higher, negative means lower: higher decoded FPS and lower latency/CPU are desirable. Source FPS describes delivered input, so changes there must be considered when interpreting decoded FPS. Ranges are observed extrema, not confidence intervals; these descriptive results do not establish statistical significance.

**RFX / Full frame**

| Metric | Before median [min, max] | After median [min, max] | Median paired change | Individual paired changes |
| --- | ---: | ---: | ---: | --- |
| Complete decoded FPS | 26.40 [26.30, 26.70] | 26.20 [26.10, 26.30] | -0.76% | P1: -2.25%; P2: -0.76%; P3: +0.00% |
| Latency p50 (ms) | 82.48 [81.52, 82.68] | 82.81 [82.62, 83.20] | +0.16% | P1: +2.07%; P2: +0.16%; P3: +0.15% |
| Latency p95 (ms) | 92.30 [89.76, 93.94] | 92.48 [91.39, 92.51] | +0.20% | P1: +3.06%; P2: +0.20%; P3: -2.72% |
| Source FPS | 59.90 [59.80, 60.00] | 60.00 [59.30, 60.00] | +0.00% | P1: +0.00%; P2: -0.84%; P3: +0.17% |
| Xorg CPU (% of one core) | 21.10 [19.20, 21.20] | 20.40 [19.10, 21.40] | -0.53% | P1: -0.53%; P2: +0.94%; P3: -3.33% |
| Xorg + xrdp CPU (ms/decoded frame) | 26.44 [25.40, 26.77] | 26.08 [25.63, 26.72] | +0.94% | P1: +0.94%; P2: +1.05%; P3: -2.56% |

**RFX / 32 sparse rectangles**

| Metric | Before median [min, max] | After median [min, max] | Median paired change | Individual paired changes |
| --- | ---: | ---: | ---: | --- |
| Complete decoded FPS | 31.10 [31.00, 31.30] | 35.50 [35.30, 40.00] | +13.50% | P1: +13.50%; P2: +29.03%; P3: +13.42% |
| Latency p50 (ms) | 15.08 [14.95, 15.30] | 8.45 [7.37, 8.67] | -43.51% | P1: -43.51%; P2: -51.84%; P3: -42.48% |
| Latency p95 (ms) | 22.60 [22.16, 22.69] | 20.81 [20.56, 20.91] | -7.84% | P1: -6.09%; P2: -7.84%; P3: -9.02% |
| Source FPS | 60.00 [60.00, 60.00] | 60.00 [60.00, 60.00] | +0.00% | P1: +0.00%; P2: +0.00%; P3: +0.00% |
| Xorg CPU (% of one core) | 5.60 [5.40, 5.60] | 1.80 [1.80, 1.90] | -67.86% | P1: -67.86%; P2: -64.81%; P3: -67.86% |
| Xorg + xrdp CPU (ms/decoded frame) | 3.15 [3.10, 3.16] | 1.83 [1.80, 1.87] | -41.87% | P1: -40.67%; P2: -41.87%; P3: -42.11% |

**AVC420 / Full frame**

| Metric | Before median [min, max] | After median [min, max] | Median paired change | Individual paired changes |
| --- | ---: | ---: | ---: | --- |
| Complete decoded FPS | 57.30 [56.90, 59.00] | 57.90 [57.10, 57.90] | +0.35% | P1: +0.35%; P2: -1.86%; P3: +1.05% |
| Latency p50 (ms) | 37.23 [36.67, 38.69] | 39.08 [37.53, 40.06] | +3.54% | P1: +3.54%; P2: +2.36%; P3: +4.96% |
| Latency p95 (ms) | 49.59 [46.09, 49.82] | 49.52 [49.46, 49.87] | +0.10% | P1: +0.10%; P2: +7.29%; P3: -0.14% |
| Source FPS | 60.00 [60.00, 60.00] | 60.00 [60.00, 60.00] | +0.00% | P1: +0.00%; P2: +0.00%; P3: +0.00% |
| Xorg CPU (% of one core) | 24.40 [23.80, 25.40] | 26.30 [25.70, 26.50] | +8.62% | P1: +10.49%; P2: +8.62%; P3: +1.17% |
| Xorg + xrdp CPU (ms/decoded frame) | 14.53 [14.20, 14.84] | 14.98 [14.87, 15.01] | +3.10% | P1: +5.68%; P2: +3.10%; P3: +0.23% |

**AVC420 / 32 sparse rectangles**

| Metric | Before median [min, max] | After median [min, max] | Median paired change | Individual paired changes |
| --- | ---: | ---: | ---: | --- |
| Complete decoded FPS | 60.00 [60.00, 60.00] | 60.00 [60.00, 60.00] | +0.00% | P1: +0.00%; P2: +0.00%; P3: +0.00% |
| Latency p50 (ms) | 25.67 [24.58, 25.84] | 11.31 [11.25, 11.36] | -55.92% | P1: -54.24%; P2: -56.03%; P3: -55.92% |
| Latency p95 (ms) | 33.92 [33.77, 34.31] | 13.25 [12.54, 13.31] | -61.21% | P1: -62.86%; P2: -60.93%; P3: -61.21% |
| Source FPS | 60.00 [60.00, 60.00] | 60.00 [60.00, 60.00] | +0.00% | P1: +0.00%; P2: +0.00%; P3: +0.00% |
| Xorg CPU (% of one core) | 9.90 [9.60, 10.20] | 1.50 [1.50, 1.60] | -84.38% | P1: -83.84%; P2: -85.29%; P3: -84.38% |
| Xorg + xrdp CPU (ms/decoded frame) | 5.98 [5.67, 6.07] | 3.90 [3.77, 4.12] | -33.53% | P1: -34.82%; P2: -33.53%; P3: -32.14% |

Xorg CPU is process CPU time as a percentage of one logical core. The combined Xorg + xrdp CPU metric includes encoder worker threads and divides sampled CPU seconds/second by observed complete decoded FPS. It excludes client decoding and source generation. CPU samples bracket the same steady-state measurement window; a process using multiple cores can exceed 100%.

The fixed private server configuration uses 32 ms for the RFX capture interval and 16 ms for AVC420. These scheduling intervals and the source's target FPS can limit decoded FPS even when CPU work per frame improves.

Full-frame motion scrolls a deterministic textured desktop uploaded to an offscreen pixmap and presented with XCopyArea. Sparse mode changes 32 separate 16×16 rectangles (8,192 pixels) plus two marker strips (16,384 submitted pixels) per source frame. Both modes use XSync after each frame and skip missed pacing slots. Valid markers identify matching source frames; they do not verify pixel-exact quality over the entire image.

Validation passed: all requested pairs are complete, client exit/error codes and backwards-marker counts are zero, decoded FPS and latency summaries match the retained samples, and CPU metrics match sampled process times. Negotiated main codec IDs are checked as RFX 0x9 and AVC420 0xb; planar surfaces (0xa) and frames with no encoded surface are allowed. Per-trial binary/configuration hashes agree across variants except the selected xorgxrdp modules; codec configuration is fixed within each codec. Source settings, workload/client binaries, marker layout and FreeRDP version match across trials.

| Provenance | Recorded value |
| --- | --- |
| Results | /home/define42/git/xorgxrdp/tests/performance/rdp-e2e-results.json |
| Created UTC | 2026-10-07T18:59:02Z |
| Baseline source revision | 59d0735c4321b5baff3feaa980a01f78a1c1e96f |
| Before module build | /tmp/xorgxrdp-before-after.Zu0PUf/before-build |
| After module build | /tmp/xorgxrdp-build.KnDgcJ |
| Fixed xrdp SHA-256 | b1e233e297c2ceef4a8d6188e0832b65d584bec568de4d7a6a031139d697330d |
| FreeRDP client SHA-256 | b1fff040de6e773eb7a21b440323805a3c51afa669ff746cd53cc4d38eb6c684 |
| FreeRDP version | 3.32.1 |
| Source workload SHA-256 | be98ca315c048a18e4aa839382176b4fddebc69bfc8aeabc60820d9ed333c95c |
| Fixed Xorg SHA-256 | e829caa5d156e4190462b7fd94f6617ddaa66aaa86e645bcd3aa6c80dc7e3148 |
| CPU affinity | Xorg: 4; xrdp: 0,1; client: 2,3; source: 5 |

Raw per-trial frame timestamps, client summaries and logs are retained in each trial's raw_directory in results.json. Startup/warmup/drain invalid or torn marker counts in client summaries cover the entire session, so they must not be treated as measured-window loss rates.
