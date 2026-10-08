# xorgxrdp performance comparison

Measured results compare the original Git revision with the modified working tree. Values are medians; brackets show the full observed min–max range. Positive time reductions mean faster execution, and negative reductions mean slower execution. Speedup is before / after.

## Environment and baseline

- Recorded UTC: 2026-10-07T18:30:13Z
- Machine: Linux-7.0.0-38-generic-x86_64-with-glibc2.43
- CPU: AMD Ryzen 9 5900X 12-Core Processor; affinity: [2]
- Hypervisor: KVM
- Compiler: cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
- Before revision: `59d0735c4321b5baff3feaa980a01f78a1c1e96f`
- After: working tree based on `59d0735c4321b5baff3feaa980a01f78a1c1e96f`
- Recorded after-source hashes: 111 module files (full hashes in results.json).
- Tracked module diff SHA-256: `85ac8f7b5817ec2ec07c40d99ac06d517fe7dcc08ea8cea770e3228508066b12`
- Before module configure options: `--enable-strict-locations XRDP_CFLAGS=-I/home/define42/git/xrdp/common 'CFLAGS=-O2 -g -Wall -Wwrite-strings -Werror' PKG_CONFIG_PATH=/tmp/xorgxrdp-build-deps.q69FsT/root/usr/lib/x86_64-linux-gnu/pkgconfig`
- After module configure options: `--enable-strict-locations XRDP_CFLAGS=-I/home/define42/git/xrdp/common 'CFLAGS=-O2 -g -Wall -Wwrite-strings -Werror' PKG_CONFIG_PATH=/tmp/xorgxrdp-build-deps.q69FsT/root/usr/lib/x86_64-linux-gnu/pkgconfig`

Raw input: `/home/define42/git/xorgxrdp/tests/performance/results.json`. All summaries below are recomputed from raw samples; warmup trials are excluded.

## ARGB-to-ABGR conversion

9 alternating before/after rounds per case, at least 20 frames per round, with equal frame counts and the same source/destination buffers for both variants. Values are milliseconds per conversion. Both variants passed byte-exact pixel, alpha, padding and source immutability checks before timing.

| Case | Before ms [range] | After ms [range] | Time reduction | Speedup |
| --- | ---: | ---: | ---: | ---: |
| 1080p aligned | 0.3129 [0.3097–0.3352] | 0.3483 [0.3440–0.3683] | -11.31% | 0.898× |
| 1080p destination +4 bytes | 1.7903 [1.7788–1.8061] | 0.3446 [0.3413–0.3499] | 80.75% | 5.195× |
| 1080p different aligned strides | 0.3274 [0.3202–0.3617] | 0.3600 [0.3541–0.3770] | -9.98% | 0.909× |
| 1080p changing row alignment | 1.0656 [1.0548–1.1089] | 0.3546 [0.3452–0.3668] | 66.72% | 3.005× |
| 4k aligned | 2.8573 [2.6412–4.1915] | 2.9725 [2.6483–3.8080] | -4.03% | 0.961× |
| 4k destination +4 bytes | 7.1701 [7.1481–7.2421] | 2.9547 [2.6358–3.1407] | 58.79% | 2.427× |
| 4k different aligned strides | 3.0072 [2.9248–3.1923] | 2.9818 [2.9416–3.2931] | 0.84% | 1.009× |
| 4k changing row alignment | 4.8311 [4.8145–4.8531] | 2.6639 [2.6452–2.6881] | 44.86% | 1.814× |

Different aligned strides retain 16-byte row alignment and serve as controls. Changing row alignment uses source/destination pitches of width × 4 + 4 / width × 4 + 12 bytes. These rows alternate between matched and mismatched 16-byte alignment.

## Region preparation and RFX capture

9 alternating rounds per variant. The production region preparation and CPU capture code are timed together; the network send is stubbed. Damage reset, source selection and pixel validation are outside the timed interval. Values are milliseconds per capture iteration.

Sparse cases place 10 or 32 separate 8×8 rectangles in two clusters near opposite corners of a 4K screen. Their bounding box spans the screen; this deliberately stresses bounding-box amplification. Changed runs alter only those damaged pixels. The capture shim prevents inlining across the client/capture file boundary; other harness compiler decisions can still differ from the full Xorg module.

Backend: `sse2`; Pixman: `0.46.4`; compiler flags: `-O2 -g -std=c99`.

| Case | Before ms [range] | After ms [range] | Time reduction | Speedup |
| --- | ---: | ---: | ---: | ---: |
| sparse10 unchanged | 0.0255 [0.0254–0.0269] | 0.0109 [0.0108–0.0112] | 57.31% | 2.343× |
| sparse10 changed | 0.1048 [0.1030–0.1166] | 0.0205 [0.0204–0.0220] | 80.46% | 5.118× |
| sparse32 unchanged | 3.4748 [2.9029–3.9847] | 0.0178 [0.0177–0.0193] | 99.49% | 195.260× |
| sparse32 changed | 3.9823 [3.7059–4.7850] | 0.0244 [0.0240–0.0291] | 99.39% | 163.005× |
| fullframe unchanged | 3.1628 [2.9820–3.4544] | 3.2103 [3.1426–4.3180] | -1.50% | 0.985× |
| fullframe changed | 6.3585 [6.2497–7.2908] | 6.5875 [6.2772–7.0288] | -3.60% | 0.965× |

Validation digests and emitted tile counts match between before/after for every case and every repeated run. Capture work below is recorded outside timing; it describes the pixels considered by the capture path, not network bytes.

| Case | Capture pixels before | Capture pixels after | Pixel reduction | Emitted tiles, both |
| --- | ---: | ---: | ---: | ---: |
| sparse10 unchanged | 640 | 640 | 0.00% | 0 |
| sparse10 changed | 640 | 640 | 0.00% | 4 |
| sparse32 unchanged | 8,294,400 | 3,136 | 99.96% | 0 |
| sparse32 changed | 8,294,400 | 3,136 | 99.96% | 8 |
| fullframe unchanged | 8,294,400 | 8,294,400 | 0.00% | 0 |
| fullframe changed | 8,294,400 | 8,294,400 | 0.00% | 2,040 |

## Live Xorg and local transport

3 independent Xorg sessions per variant, 9 measured trials per workload per session, 1024×768 pixels, and a 40 ms frame interval. The table pools the measured trials across sessions. Each session alternates with the other variant; warmups are excluded.

| Case | Before ms [range] | After ms [range] | Time reduction | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Scattered drawing after idle → local capture | 4.6896 [3.4832–6.2606] | 4.4095 [3.3285–5.5373] | 5.97% | 1.064× |
| Full-frame drawing after idle → local capture | 4.9096 [4.5481–6.5368] | 4.8096 [4.2384–7.4586] | 2.04% | 1.021× |
| Drawing after an empty ACK → local capture | 34.3199 [33.2723–36.3902] | 4.3218 [3.3073–6.1057] | 87.41% | 7.941× |
| XSync during a partial protocol header | 90.3110 [90.1167–91.1343] | 0.0725 [0.0561–0.6047] | 99.92% | 1245.720× |
| XSync during a partial protocol body | 90.5373 [90.0941–92.4129] | 0.0767 [0.0506–0.1172] | 99.92% | 1180.331× |
| XSync while the local peer pauses reads | 74.2500 [70.3829–75.1329] | 0.0664 [0.0478–0.9417] | 99.91% | 1118.627× |

Drawing latency ends when the local benchmark peer receives the capture. Scattered and full-frame controls wait 90 ms before starting the timed draw, allowing prior frame deadlines to expire. XSync measures whether another X client can make progress while the local protocol peer delays data or reads.

Partial header/body tests delay completion by 100 ms; blocked output pauses reads for 80 ms while requesting 1000 replies. All live sessions reported successful pixel/protocol verification; blocked-output replies were complete and byte-exact.

Median captured pixel counts in the live draw tests:

| Workload | Before | After |
| --- | ---: | ---: |
| scattered | 484,832 | 8,672 |
| full frame | 786,432 | 786,432 |
| empty ack | 64 | 64 |

## Interpretation limits

- Results describe this machine and these workloads. Min–max ranges show variability; they are not confidence intervals.
- Conversion uses reused CPU frame buffers and AMD64 assembly. It does not time allocation, GPU readback, encoding, or display.
- Capture compares region preparation plus the RFX CPU capture path. Pixel-work reduction is not a measured bandwidth saving.
- Live tests use a local Unix-socket peer and synthetic drawing. They do not measure WAN latency, an RDP encoder, a remote client's display rate, or end-to-end RDP FPS.
- Microbenchmark speedups do not imply the same percentage improvement for an entire desktop session.
