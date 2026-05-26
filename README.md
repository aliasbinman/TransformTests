# TransformTests

Benchmark for 2D point transform implementations in C++. Compares out-of-place vs in-place, scalar vs SSE2, AoS vs SoA vs AoSoA layouts, single-threaded vs multi-threaded (4 and 10 threads via a fixed thread pool). Includes a verify pass (every method must match scalar) and an optional cold-cache pass that thrashes 128 MB of scratch memory between iterations to evict src/dst from cache.

## Build

Visual Studio (MSVC, x64):

```
msbuild transform2d.sln /p:Configuration=Release /p:Platform=x64
```

Or open `transform2d.sln` in Visual Studio and build.

## Run

```
./transform2d.exe [--verbose] [--verify] [--cold] [--no-warm]
```

| Flag | Effect |
|---|---|
| `--verify`   | Run correctness pass first: each method runs at N=1,000,000 and is compared to scalar. Reports `IDENTICAL` or `max_abs_diff` plus worst index. |
| `--cold`     | After warm bench, run a cold-cache bench. Before every timed iteration a 128 MB scratch buffer is touched (one byte per 64-byte line, increment) to evict src/dst from all cache levels. Thrash/reset time is not included in the sample. Cold bench uses 20 iterations per cell. |
| `--no-warm`  | Skip the default warm bench. Useful with `--verify` and/or `--cold` alone. |
| `--verbose`  | Verbose printf banner. |

Default (no flags): run warm bench only (21 iterations per cell, first iter discarded as warmup, mean of the remaining 20).

## Output

CSV files written to the working directory:

| File | Contents |
|---|---|
| `results_oop_1t.csv`       | Warm out-of-place, 1 thread |
| `results_ip_1t.csv`        | Warm in-place, 1 thread (rotation-only matrix) |
| `results_oop_4t.csv`       | Warm out-of-place, 4 threads |
| `results_ip_4t.csv`        | Warm in-place, 4 threads |
| `results_oop_10t.csv`      | Warm out-of-place, 10 threads |
| `results_ip_10t.csv`       | Warm in-place, 10 threads |
| `results_long.csv`         | Warm long-form combined results |
| `results_cold_*.csv`       | Same shape as above, cold-cache pass (only when `--cold`) |
| `results_cold_long.csv`    | Cold long-form combined results |

Rows: method. Columns: `N`. Values: mean time in ms.

## Verification

`--verify` runs every method at N=1,000,000 and compares its output to the scalar reference, elementwise, in `double` precision. Each layout (AoS / SoA / AoSoA) is read back through a per-method getter so the comparison is independent of storage format. Result on this machine:

```
=== Verify: Out-of-place (1 thread)   (N=1000000) ===   all 7 methods IDENTICAL to scalar
=== Verify: In-place   (1 thread)     (N=1000000) ===   all 7 methods IDENTICAL to scalar
=== Verify: Out-of-place (4 threads)  (N=1000000) ===   all 7 methods IDENTICAL to scalar
=== Verify: In-place   (4 threads)    (N=1000000) ===   all 7 methods IDENTICAL to scalar
=== Verify: Out-of-place (10 threads) (N=1000000) ===   all 7 methods IDENTICAL to scalar
=== Verify: In-place   (10 threads)   (N=1000000) ===   all 7 methods IDENTICAL to scalar
```

`max_abs_diff == 0.0` for every method. The SSE2 lanes carry out the same FMA-free `a*x + b*y + tx` sequence in the same order as the scalar loop, so bit-equality holds. If a future change introduced a different summation order (e.g. an FMA path), the verify pass would print the worst index and both scalar/method points there.

## Results

CPU: **Intel(R) Core(TM) i7-14700HX**

All times are mean ms per iteration, truncated to 3 significant figures (2 digits after the first non-zero). `(NN%)` is time relative to the scalar baseline of the same table — 100% = same as scalar, lower = faster. Percent is integer-rounded.

### Disclaimer — small-N numbers are noise

Take everything below N=1,000,000 with a grain of salt. A single run at N=1,000 takes ~1 µs; `std::chrono::high_resolution_clock` overhead alone is ~50–100 ns per timestamp pair, so 5–10% of the signal is the clock itself. Add to that:

- **Thermal / turbo flicker.** Modern CPUs change frequency on millisecond timescales. Two back-to-back runs of the same method can land on different P-states.
- **Hybrid scheduling (i7-14700HX has P-cores + E-cores).** No thread affinity is pinned, so a worker may be scheduled onto an E-core mid-run and skew that sample.
- **Cache state.** Bench order matters — the first size pays cold-cache costs, later sizes inherit warm L1/L2 from prior iterations. The first iteration of each cell is now discarded as a warmup, which removes the worst of that effect at small N but not all of it. Reordering sizes still changes the numbers.
- **OS noise.** Context switches, interrupts, and background processes add microsecond-scale jitter that dominates µs-scale work.
- **NT-store variance.** Streaming stores depend on write-combine buffer pressure and memory-controller queue depth; few stores total at small N = no averaging.
- **Sample size.** Warm bench: 20 timed iterations per cell (after dropping one warmup). Cold bench: 20 iterations per cell, no warmup discard. Outliers aren't trimmed; the mean (not median) is reported.

Consequence: the same method/size can swing ±50% (occasionally more) between runs at N ≤ 100,000. Don't read small-N ordering as ranking signal — only the N=1,000,000 and N=10,000,000 columns are reliable enough to compare methods. The small-N columns are kept as a rough indication of fixed overhead (function call, thread-pool wakeup, etc.), not throughput.

### Method names

- `scalar` — plain scalar loop, one point at a time.
- `unroll4` — same scalar math, manually unrolled 4 points per iteration.
- `sse` — SSE2 SIMD, AoS layout (interleaved x,y).
- `sse_soa` — SSE2 SIMD, SoA layout (separate x[] and y[] arrays).
- `sse_aosoa` — SSE2 SIMD, AoSoA layout (blocks of 8 x's then 8 y's).
- `*_stream` — SoA / AoSoA variant using **non-temporal stores** (`_mm_stream_ps`). These writes bypass the cache: they skip the read-for-ownership / write-allocate that normal stores incur, so they avoid polluting cache with output the producer won't reread. Win on large buffers that overflow cache; cost is a stall if the data *is* immediately read again, and a small fixed overhead that makes them lose at small N (visible in the in-place 1-thread row at N=10000 where they hit ~200% of scalar). Require the destination to be 16-byte aligned.

## Warm cache

First iteration of each (method, N) cell is discarded; the remaining 20 are averaged. All buffers stay resident across iterations.

### Out-of-place, 1 thread

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.000765 (100%) | 0.00607 (100%) | 0.0584 (100%) | 0.625 (100%) | 8.45 (100%) |
| unroll4           | 0.000780 (102%) | 0.00550  (91%) | 0.0568  (97%) | 0.608  (97%) | 8.03  (95%) |
| sse               | 0.000255  (33%) | 0.00238  (39%) | 0.0234  (40%) | 0.263  (42%) | 6.81  (81%) |
| sse_soa           | 0.000200  (26%) | 0.00261  (43%) | 0.0257  (44%) | 0.319  (51%) | 5.91  (70%) |
| sse_aosoa         | 0.000220  (29%) | 0.00139  (23%) | 0.0143  (24%) | 0.285  (46%) | 6.78  (80%) |
| sse_soa_stream    | 0.000215  (28%) | 0.00204  (34%) | 0.0201  (34%) | 0.239  (38%) | 4.44  (53%) |
| sse_aosoa_stream  | 0.000210  (27%) | 0.00180  (30%) | 0.0226  (39%) | 0.197  (32%) | 5.65  (67%) |

### In-place, 1 thread (rotation-only mat)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.000800 (100%) | 0.00578 (100%) | 0.0570 (100%) | 0.596 (100%) | 7.28 (100%) |
| unroll4           | 0.000645  (81%) | 0.00559  (97%) | 0.0560  (98%) | 0.568  (95%) | 7.54 (104%) |
| sse               | 0.000300  (37%) | 0.00244  (42%) | 0.0245  (43%) | 0.271  (45%) | 5.67  (78%) |
| sse_soa           | 0.000145  (18%) | 0.00140  (24%) | 0.0138  (24%) | 0.191  (32%) | 4.24  (58%) |
| sse_aosoa         | 0.000150  (19%) | 0.00140  (24%) | 0.0138  (24%) | 0.187  (31%) | 5.03  (69%) |
| sse_soa_stream    | 0.00146  (182%) | 0.0124  (214%) | 0.0508  (89%) | 0.502  (84%) | 4.75  (65%) |
| sse_aosoa_stream  | 0.00134  (167%) | 0.0135  (234%) | 0.0575 (101%) | 0.578  (97%) | 5.63  (77%) |

### Out-of-place, 4 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0772 (100%) | 0.0187 (100%) | 0.0394 (100%) | 0.181 (100%) | 3.67 (100%) |
| unroll4           | 0.0154  (20%) | 0.0160  (86%) | 0.0382  (97%) | 0.175  (97%) | 3.59  (98%) |
| sse               | 0.0142  (18%) | 0.0133  (71%) | 0.0219  (55%) | 0.110  (61%) | 3.16  (86%) |
| sse_soa           | 0.0241  (31%) | 0.0132  (71%) | 0.0220  (56%) | 0.140  (77%) | 3.40  (93%) |
| sse_aosoa         | 0.0187  (24%) | 0.0126  (67%) | 0.0298  (76%) | 0.0940 (52%) | 3.00  (82%) |
| sse_soa_stream    | 0.0128  (17%) | 0.0124  (66%) | 0.0200  (51%) | 0.139  (77%) | 2.32  (63%) |
| sse_aosoa_stream  | 0.00674  (9%) | 0.0124  (66%) | 0.0194  (49%) | 0.125  (69%) | 2.53  (69%) |

### In-place, 4 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0136 (100%) | 0.0140 (100%) | 0.0308 (100%) | 0.201 (100%) | 2.86 (100%) |
| unroll4           | 0.0164 (120%) | 0.0145 (104%) | 0.0262  (85%) | 0.173  (86%) | 3.17 (111%) |
| sse               | 0.0180 (132%) | 0.00580 (41%) | 0.0190  (62%) | 0.109  (54%) | 2.52  (88%) |
| sse_soa           | 0.0145 (107%) | 0.0103  (74%) | 0.0169  (55%) | 0.0727 (36%) | 2.28  (80%) |
| sse_aosoa         | 0.00664 (49%) | 0.00529 (38%) | 0.0175  (57%) | 0.0787 (39%) | 2.36  (83%) |
| sse_soa_stream    | 0.00640 (47%) | 0.0146 (104%) | 0.0425 (138%) | 0.263 (130%) | 2.53  (89%) |
| sse_aosoa_stream  | 0.00929 (68%) | 0.0132  (95%) | 0.0393 (128%) | 0.272 (135%) | 2.61  (91%) |

### Out-of-place, 10 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0348 (100%) | 0.0237 (100%) | 0.0397 (100%) | 0.155 (100%) | 2.73 (100%) |
| unroll4           | 0.0252  (72%) | 0.0222  (94%) | 0.0321  (81%) | 0.135  (87%) | 2.62  (96%) |
| sse               | 0.0207  (60%) | 0.0211  (89%) | 0.0266  (67%) | 0.0853 (55%) | 2.56  (94%) |
| sse_soa           | 0.0234  (67%) | 0.0206  (87%) | 0.0276  (70%) | 0.113  (73%) | 3.24 (119%) |
| sse_aosoa         | 0.0186  (53%) | 0.0212  (89%) | 0.0245  (62%) | 0.0829 (54%) | 2.57  (94%) |
| sse_soa_stream    | 0.0183  (53%) | 0.0196  (83%) | 0.0252  (63%) | 0.147  (95%) | 2.21  (81%) |
| sse_aosoa_stream  | 0.0176  (51%) | 0.0305 (129%) | 0.0245  (62%) | 0.128  (83%) | 2.19  (81%) |

### In-place, 10 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0185 (100%) | 0.0240 (100%) | 0.0413 (100%) | 0.141 (100%) | 2.06 (100%) |
| unroll4           | 0.0260 (140%) | 0.0264 (110%) | 0.0368  (89%) | 0.131  (93%) | 1.97  (95%) |
| sse               | 0.0147  (80%) | 0.0232  (97%) | 0.0343  (83%) | 0.0723 (51%) | 1.88  (91%) |
| sse_soa           | 0.0153  (82%) | 0.0214  (89%) | 0.0332  (80%) | 0.0764 (54%) | 1.79  (87%) |
| sse_aosoa         | 0.0158  (85%) | 0.0204  (85%) | 0.0317  (77%) | 0.103  (73%) | 1.98  (96%) |
| sse_soa_stream    | 0.0176  (95%) | 0.0229  (95%) | 0.0456 (110%) | 0.289 (205%) | 2.44 (118%) |
| sse_aosoa_stream  | 0.0171  (93%) | 0.0225  (94%) | 0.0419 (101%) | 0.263 (186%) | 2.38 (115%) |

## Cold cache

128 MB scratch buffer is touched (1 byte per 64-byte line, increment) before every timed iteration to evict src/dst from L1/L2/L3 (i7-14700HX L3 = 33 MB). For in-place methods the source buffer is also reset from the pristine copy before each iter — both the reset and the thrash run *outside* the timed section. 20 iterations per cell. Effect: every iteration measures a true DRAM-fetch start state, so methods that minimise DRAM traffic (especially non-temporal stores, which skip write-allocate) pull further ahead at large N.

What to look at:

- **`sse_soa_stream` at N=10M (OOP, 1 thread): 52% of scalar cold vs 53% warm.** Stream stores were already memory-bound warm; cold doesn't change the ranking because the bottleneck was always the memory bus.
- **Small-N cold cells (N=1000, 10000) compress toward 100%.** Once the working set fits in cache, scalar and SSE both eat the cold-line-fill cost, which dominates the few hundred ns of actual compute. The SIMD speedup is hidden by the fixed cost of pulling lines from DRAM.
- **The in-place 1-thread NT-store penalty at small N disappears (`sse_soa_stream` N=1000: 182% warm → 77% cold).** When the buffer was cache-resident warm, NT-store evict-then-refill was net-negative. Cold, the lines weren't there anyway.

### Out-of-place, 1 thread (cold)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.00177 (100%) | 0.0105 (100%) | 0.104 (100%) | 0.926 (100%) | 8.38 (100%) |
| unroll4           | 0.00162  (92%) | 0.00918 (87%) | 0.0851 (82%) | 0.874  (94%) | 8.27  (99%) |
| sse               | 0.00155  (87%) | 0.00812 (77%) | 0.0834 (80%) | 0.744  (80%) | 7.35  (88%) |
| sse_soa           | 0.00152  (86%) | 0.00740 (70%) | 0.0653 (63%) | 0.622  (67%) | 5.97  (71%) |
| sse_aosoa         | 0.00153  (86%) | 0.00823 (78%) | 0.0793 (76%) | 0.742  (80%) | 6.86  (82%) |
| sse_soa_stream    | 0.00129  (73%) | 0.00604 (57%) | 0.0513 (49%) | 0.502  (54%) | 4.40  (52%) |
| sse_aosoa_stream  | 0.00166  (94%) | 0.00747 (71%) | 0.0664 (64%) | 0.594  (64%) | 5.59  (67%) |

### In-place, 1 thread (cold, rotation-only mat)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.00182 (100%) | 0.0110 (100%) | 0.0945 (100%) | 0.877 (100%) | 8.04 (100%) |
| unroll4           | 0.00192 (106%) | 0.00896 (81%) | 0.0874  (92%) | 0.813  (93%) | 7.88  (98%) |
| sse               | 0.00149  (82%) | 0.00705 (64%) | 0.0663  (70%) | 0.614  (70%) | 6.01  (75%) |
| sse_soa           | 0.00111  (61%) | 0.00472 (43%) | 0.0538  (57%) | 0.471  (54%) | 4.37  (54%) |
| sse_aosoa         | 0.00139  (76%) | 0.00693 (63%) | 0.0542  (57%) | 0.528  (60%) | 5.48  (68%) |
| sse_soa_stream    | 0.00141  (77%) | 0.00613 (56%) | 0.0541  (57%) | 0.524  (60%) | 5.05  (63%) |
| sse_aosoa_stream  | 0.00149  (82%) | 0.00723 (66%) | 0.0654  (69%) | 0.592  (67%) | 5.69  (71%) |

### Out-of-place, 4 threads (cold)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0934 (100%) | 0.0660 (100%) | 0.160 (100%) | 0.524 (100%) | 3.87 (100%) |
| unroll4           | 0.0973 (104%) | 0.0907 (137%) | 0.149  (93%) | 0.459  (88%) | 3.77  (97%) |
| sse               | 0.126  (135%) | 0.104  (157%) | 0.108  (67%) | 0.467  (89%) | 3.40  (88%) |
| sse_soa           | 0.101  (108%) | 0.108  (164%) | 0.135  (84%) | 0.452  (86%) | 3.88 (100%) |
| sse_aosoa         | 0.109  (117%) | 0.110  (166%) | 0.102  (64%) | 0.455  (87%) | 3.27  (84%) |
| sse_soa_stream    | 0.0929  (99%) | 0.0858 (130%) | 0.121  (76%) | 0.356  (68%) | 2.96  (77%) |
| sse_aosoa_stream  | 0.0918  (98%) | 0.0775 (117%) | 0.130  (81%) | 0.411  (78%) | 3.14  (81%) |

### In-place, 4 threads (cold)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0938 (100%) | 0.101 (100%) | 0.118 (100%) | 0.547 (100%) | 3.21 (100%) |
| unroll4           | 0.0890  (95%) | 0.117 (116%) | 0.121 (103%) | 0.518  (95%) | 3.15  (98%) |
| sse               | 0.0659  (70%) | 0.0926 (92%) | 0.128 (108%) | 0.404  (74%) | 2.94  (92%) |
| sse_soa           | 0.0865  (92%) | 0.0826 (82%) | 0.122 (103%) | 0.337  (62%) | 2.62  (82%) |
| sse_aosoa         | 0.110  (117%) | 0.107 (106%) | 0.109  (92%) | 0.366  (67%) | 2.79  (87%) |
| sse_soa_stream    | 0.0964 (103%) | 0.0970 (96%) | 0.127 (108%) | 0.393  (72%) | 2.63  (82%) |
| sse_aosoa_stream  | 0.0932  (99%) | 0.0807 (80%) | 0.145 (123%) | 0.428  (78%) | 2.77  (86%) |

### Out-of-place, 10 threads (cold)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.105 (100%) | 0.126 (100%) | 0.159 (100%) | 0.495 (100%) | 2.99 (100%) |
| unroll4           | 0.130 (124%) | 0.134 (106%) | 0.123  (77%) | 0.470  (95%) | 2.98 (100%) |
| sse               | 0.118 (112%) | 0.275 (218%) | 0.156  (98%) | 0.389  (79%) | 2.79  (93%) |
| sse_soa           | 0.104  (99%) | 0.142 (112%) | 0.147  (92%) | 0.417  (84%) | 3.42 (115%) |
| sse_aosoa         | 0.0847 (80%) | 0.117  (93%) | 0.131  (82%) | 0.401  (81%) | 2.93  (98%) |
| sse_soa_stream    | 0.124 (118%) | 0.135 (107%) | 0.130  (82%) | 0.382  (77%) | 2.54  (85%) |
| sse_aosoa_stream  | 0.119 (113%) | 0.0806 (64%) | 0.145  (91%) | 0.360  (73%) | 2.55  (85%) |

### In-place, 10 threads (cold)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0845 (100%) | 0.130 (100%) | 0.132 (100%) | 0.455 (100%) | 2.69 (100%) |
| unroll4           | 0.119  (140%) | 0.111  (85%) | 0.128  (97%) | 0.446  (98%) | 2.57  (95%) |
| sse               | 0.0995 (118%) | 0.122  (94%) | 0.105  (80%) | 0.392  (86%) | 2.44  (91%) |
| sse_soa           | 0.103  (122%) | 0.0903 (69%) | 0.128  (97%) | 0.332  (73%) | 2.34  (87%) |
| sse_aosoa         | 0.125  (148%) | 0.128  (98%) | 0.119  (90%) | 0.399  (88%) | 2.38  (88%) |
| sse_soa_stream    | 0.133  (158%) | 0.121  (93%) | 0.125  (95%) | 0.378  (83%) | 2.55  (95%) |
| sse_aosoa_stream  | 0.128  (151%) | 0.127  (97%) | 0.135 (103%) | 0.400  (88%) | 2.55  (95%) |

## Layout

- `transform2d.cpp` — benchmark driver and all method implementations
- `transform2d.sln` / `transform2d.vcxproj` — MSVC project
