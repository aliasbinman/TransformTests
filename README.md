# TransformTests

Benchmark for 2D point transform implementations in C++. Compares out-of-place vs in-place, scalar vs SSE2, AoS vs SoA vs AoSoA layouts, single-threaded vs multi-threaded (4 and 10 threads via a fixed thread pool).

## Build

Visual Studio (MSVC, x64):

```
msbuild transform2d.sln /p:Configuration=Release /p:Platform=x64
```

Or open `transform2d.sln` in Visual Studio and build.

## Run

```
./transform2d.exe [--verbose]
```

Runs each method across a range of `N` sizes for a fixed iteration count and prints per-method timings. Speedup vs the 1-thread baseline is shown for the 4- and 10-thread runs.

## Output

CSV files written to the working directory:

| File | Contents |
|---|---|
| `results_oop_1t.csv` | Out-of-place, 1 thread |
| `results_ip_1t.csv`  | In-place, 1 thread (rotation-only matrix) |
| `results_oop_4t.csv` | Out-of-place, 4 threads |
| `results_ip_4t.csv`  | In-place, 4 threads |
| `results_oop_10t.csv`| Out-of-place, 10 threads |
| `results_ip_10t.csv` | In-place, 10 threads |
| `results_long.csv`   | Long-form combined results |

Rows: method. Columns: `N`. Values: mean time in ms.

## Results

CPU: **Intel(R) Core(TM) i7-14700HX**

All times are mean ms per iteration, truncated to 3 significant figures (2 digits after the first non-zero). `(NN%)` is time relative to the scalar baseline of the same table — 100% = same as scalar, lower = faster. Percent is integer-rounded.

### Method names

- `scalar` — plain scalar loop, one point at a time.
- `unroll4` — same scalar math, manually unrolled 4 points per iteration.
- `sse` — SSE2 SIMD, AoS layout (interleaved x,y).
- `sse_soa` — SSE2 SIMD, SoA layout (separate x[] and y[] arrays).
- `sse_aosoa` — SSE2 SIMD, AoSoA layout (blocks of 8 x's then 8 y's).
- `*_stream` — SoA / AoSoA variant using **non-temporal stores** (`_mm_stream_ps`). These writes bypass the cache: they skip the read-for-ownership / write-allocate that normal stores incur, so they avoid polluting cache with output the producer won't reread. Win on large buffers that overflow cache; cost is a stall if the data *is* immediately read again, and a small fixed overhead that makes them lose at small N (visible in the in-place 1-thread row at N=10000 where they hit ~200% of scalar). Require the destination to be 16-byte aligned.

### Out-of-place, 1 thread

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.000746 (100%) | 0.00641 (100%) | 0.0571 (100%) | 0.617 (100%) | 8.74 (100%) |
| unroll4           | 0.000668  (90%) | 0.00539  (84%) | 0.0515  (90%) | 0.572  (93%) | 7.80  (89%) |
| sse               | 0.000339  (45%) | 0.00241  (38%) | 0.0223  (39%) | 0.282  (46%) | 6.69  (77%) |
| sse_soa           | 0.000189  (25%) | 0.00379  (59%) | 0.0282  (49%) | 0.326  (53%) | 5.52  (63%) |
| sse_aosoa         | 0.000201  (27%) | 0.00139  (22%) | 0.0191  (34%) | 0.272  (44%) | 6.77  (77%) |
| sse_soa_stream    | 0.000197  (26%) | 0.00338  (53%) | 0.0205  (36%) | 0.222  (36%) | 4.22  (48%) |
| sse_aosoa_stream  | 0.000197  (26%) | 0.00266  (41%) | 0.0168  (29%) | 0.231  (37%) | 5.30  (61%) |

### In-place, 1 thread (rotation-only mat)

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.000681 (100%) | 0.00573 (100%) | 0.0568 (100%) | 0.568 (100%) | 7.36 (100%) |
| unroll4           | 0.000608  (89%) | 0.00524  (92%) | 0.0537  (95%) | 0.547  (96%) | 7.48 (102%) |
| sse               | 0.000253  (37%) | 0.00222  (39%) | 0.0221  (39%) | 0.236  (42%) | 5.46  (74%) |
| sse_soa           | 0.000156  (23%) | 0.00135  (24%) | 0.0131  (23%) | 0.173  (30%) | 4.10  (56%) |
| sse_aosoa         | 0.000153  (22%) | 0.00129  (23%) | 0.0137  (24%) | 0.176  (31%) | 5.49  (75%) |
| sse_soa_stream    | 0.00125  (184%) | 0.0111  (195%) | 0.0454  (80%) | 0.479  (84%) | 4.83  (66%) |
| sse_aosoa_stream  | 0.00127  (186%) | 0.0116  (203%) | 0.0513  (90%) | 0.542  (95%) | 5.69  (77%) |

### Out-of-place, 4 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0200 (100%) | 0.0207 (100%) | 0.0526 (100%) | 0.197 (100%) | 3.48 (100%) |
| unroll4           | 0.0161  (80%) | 0.0197  (95%) | 0.0347  (66%) | 0.174  (88%) | 3.55 (102%) |
| sse               | 0.0145  (73%) | 0.0161  (78%) | 0.0261  (50%) | 0.0992 (50%) | 3.18  (91%) |
| sse_soa           | 0.0174  (87%) | 0.0183  (88%) | 0.0292  (56%) | 0.149  (76%) | 3.62 (104%) |
| sse_aosoa         | 0.0167  (84%) | 0.0167  (81%) | 0.0243  (46%) | 0.0944 (48%) | 3.02  (87%) |
| sse_soa_stream    | 0.0162  (81%) | 0.0177  (85%) | 0.0277  (53%) | 0.137  (70%) | 2.28  (66%) |
| sse_aosoa_stream  | 0.0179  (90%) | 0.0186  (90%) | 0.0251  (48%) | 0.135  (69%) | 2.50  (72%) |

### In-place, 4 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0178 (100%) | 0.0229 (100%) | 0.0361 (100%) | 0.191 (100%) | 2.65 (100%) |
| unroll4           | 0.0203 (114%) | 0.0249 (109%) | 0.0329  (91%) | 0.181  (95%) | 2.70 (102%) |
| sse               | 0.0191 (107%) | 0.0208  (91%) | 0.0269  (74%) | 0.0992 (52%) | 2.27  (86%) |
| sse_soa           | 0.0202 (113%) | 0.0205  (89%) | 0.0237  (66%) | 0.0674 (35%) | 2.03  (77%) |
| sse_aosoa         | 0.0209 (117%) | 0.0200  (87%) | 0.0243  (67%) | 0.0750 (39%) | 2.26  (85%) |
| sse_soa_stream    | 0.0198 (111%) | 0.0220  (96%) | 0.0459 (127%) | 0.277 (145%) | 2.53  (95%) |
| sse_aosoa_stream  | 0.0191 (107%) | 0.0213  (93%) | 0.0423 (117%) | 0.282 (147%) | 2.60  (98%) |

### Out-of-place, 10 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0282 (100%) | 0.0251 (100%) | 0.0349 (100%) | 0.161 (100%) | 2.56 (100%) |
| unroll4           | 0.0218  (77%) | 0.0265 (105%) | 0.0326  (93%) | 0.138  (85%) | 2.68 (105%) |
| sse               | 0.0202  (72%) | 0.0230  (92%) | 0.0325  (93%) | 0.0813 (50%) | 2.43  (95%) |
| sse_soa           | 0.0217  (77%) | 0.0239  (95%) | 0.0335  (96%) | 0.0957 (59%) | 3.05 (119%) |
| sse_aosoa         | 0.0224  (79%) | 0.0245  (97%) | 0.0337  (96%) | 0.0997 (62%) | 2.47  (96%) |
| sse_soa_stream    | 0.0221  (78%) | 0.0228  (91%) | 0.0338  (97%) | 0.147  (91%) | 2.09  (82%) |
| sse_aosoa_stream  | 0.0224  (79%) | 0.0249  (99%) | 0.0313  (90%) | 0.141  (88%) | 2.13  (83%) |

### In-place, 10 threads

| method            |             1000 |            10000 |           100000 |          1000000 |         10000000 |
|-------------------|-----------------:|-----------------:|-----------------:|-----------------:|-----------------:|
| scalar            | 0.0331 (100%) | 0.0345 (100%) | 0.0462 (100%) | 0.150 (100%) | 1.93 (100%) |
| unroll4           | 0.0331 (100%) | 0.0335  (97%) | 0.0452  (98%) | 0.146  (97%) | 1.95 (101%) |
| sse               | 0.0300  (91%) | 0.0341  (99%) | 0.0387  (84%) | 0.110  (73%) | 1.74  (90%) |
| sse_soa           | 0.0326  (98%) | 0.0328  (95%) | 0.0378  (82%) | 0.110  (73%) | 1.77  (92%) |
| sse_aosoa         | 0.0310  (94%) | 0.0323  (94%) | 0.0390  (84%) | 0.106  (71%) | 1.88  (98%) |
| sse_soa_stream    | 0.0341 (103%) | 0.0350 (101%) | 0.0521 (113%) | 0.268 (178%) | 2.31 (120%) |
| sse_aosoa_stream  | 0.0307  (93%) | 0.0324  (94%) | 0.0510 (110%) | 0.262 (174%) | 2.39 (124%) |

## Layout

- `transform2d.cpp` — benchmark driver and all method implementations
- `transform2d.sln` / `transform2d.vcxproj` — MSVC project
