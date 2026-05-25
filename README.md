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

All times are mean ms per iteration. `(NN%)` is time relative to the scalar baseline of the same table — 100% = same as scalar, lower = faster. Percent is integer-rounded.

### Method names

- `scalar` — plain scalar loop, one point at a time.
- `unroll4` — same scalar math, manually unrolled 4 points per iteration.
- `sse` — SSE2 SIMD, AoS layout (interleaved x,y).
- `sse_soa` — SSE2 SIMD, SoA layout (separate x[] and y[] arrays).
- `sse_aosoa` — SSE2 SIMD, AoSoA layout (blocks of 8 x's then 8 y's).
- `*_stream` — SoA / AoSoA variant using **non-temporal stores** (`_mm_stream_ps`). These writes bypass the cache: they skip the read-for-ownership / write-allocate that normal stores incur, so they avoid polluting cache with output the producer won't reread. Win on large buffers that overflow cache; cost is a stall if the data *is* immediately read again, and a small fixed overhead that makes them lose at small N (visible in the in-place 1-thread row at N=10000 where they hit ~200% of scalar). Require the destination to be 16-byte aligned.

### Out-of-place, 1 thread

| method            |          10000 |         100000 |        1000000 |       10000000 |
|-------------------|---------------:|---------------:|---------------:|---------------:|
| scalar            | 0.020906 (100%) | 0.064900 (100%) | 0.695361 (100%) | 8.738957 (100%) |
| unroll4           | 0.005705  (27%) | 0.056912  (88%) | 0.712218 (102%) | 8.155486  (93%) |
| sse               | 0.002433  (12%) | 0.024538  (38%) | 0.477430  (69%) | 7.139704  (82%) |
| sse_soa           | 0.004169  (20%) | 0.027517  (42%) | 0.387839  (56%) | 5.833594  (67%) |
| sse_aosoa         | 0.001571   (8%) | 0.015827  (24%) | 0.429494  (62%) | 7.483938  (86%) |
| sse_soa_stream    | 0.002109  (10%) | 0.021431  (33%) | 0.248584  (36%) | 4.241549  (49%) |
| sse_aosoa_stream  | 0.001775   (8%) | 0.018262  (28%) | 0.222189  (32%) | 5.441975  (62%) |

### In-place, 1 thread (rotation-only mat)

| method            |          10000 |         100000 |        1000000 |       10000000 |
|-------------------|---------------:|---------------:|---------------:|---------------:|
| scalar            | 0.007088 (100%) | 0.061080 (100%) | 0.591226 (100%) | 7.707195 (100%) |
| unroll4           | 0.005381  (76%) | 0.054865  (90%) | 0.576547  (98%) | 7.599540  (99%) |
| sse               | 0.003087  (44%) | 0.023226  (38%) | 0.255571  (43%) | 5.504468  (71%) |
| sse_soa           | 0.001567  (22%) | 0.014476  (24%) | 0.173936  (29%) | 3.984765  (52%) |
| sse_aosoa         | 0.001450  (20%) | 0.014552  (24%) | 0.197189  (33%) | 5.202200  (68%) |
| sse_soa_stream    | 0.014998 (212%) | 0.046707  (76%) | 0.476616  (81%) | 4.985594  (65%) |
| sse_aosoa_stream  | 0.014036 (198%) | 0.053074  (87%) | 0.608411 (103%) | 5.891704  (76%) |

### Out-of-place, 4 threads

| method            |          10000 |         100000 |        1000000 |       10000000 |
|-------------------|---------------:|---------------:|---------------:|---------------:|
| scalar            | 0.034567 (100%) | 0.044439 (100%) | 0.186432 (100%) | 3.388888 (100%) |
| unroll4           | 0.021096  (61%) | 0.040477  (91%) | 0.169815  (91%) | 3.367522  (99%) |
| sse               | 0.014780  (43%) | 0.022101  (50%) | 0.087526  (47%) | 3.056883  (90%) |
| sse_soa           | 0.017536  (51%) | 0.033403  (75%) | 0.099376  (53%) | 3.486429 (103%) |
| sse_aosoa         | 0.016105  (47%) | 0.018621  (42%) | 0.106797  (57%) | 2.917099  (86%) |
| sse_soa_stream    | 0.012372  (36%) | 0.020627  (46%) | 0.130900  (70%) | 2.245459  (66%) |
| sse_aosoa_stream  | 0.012497  (36%) | 0.024197  (54%) | 0.120155  (64%) | 2.403116  (71%) |

### In-place, 4 threads

| method            |          10000 |         100000 |        1000000 |       10000000 |
|-------------------|---------------:|---------------:|---------------:|---------------:|
| scalar            | 0.014046 (100%) | 0.028398 (100%) | 0.173902 (100%) | 2.912799 (100%) |
| unroll4           | 0.015557 (111%) | 0.030472 (107%) | 0.165384  (95%) | 2.687157  (92%) |
| sse               | 0.005149  (37%) | 0.018711  (66%) | 0.088046  (51%) | 2.239477  (77%) |
| sse_soa           | 0.008990  (64%) | 0.017174  (60%) | 0.070176  (40%) | 2.060998  (71%) |
| sse_aosoa         | 0.007771  (55%) | 0.017181  (60%) | 0.073334  (42%) | 2.151821  (74%) |
| sse_soa_stream    | 0.013807  (98%) | 0.037698 (133%) | 0.283076 (163%) | 2.518155  (86%) |
| sse_aosoa_stream  | 0.015666 (112%) | 0.037221 (131%) | 0.284567 (164%) | 2.595931  (89%) |

### Out-of-place, 10 threads

| method            |          10000 |         100000 |        1000000 |       10000000 |
|-------------------|---------------:|---------------:|---------------:|---------------:|
| scalar            | 0.022589 (100%) | 0.044898 (100%) | 0.141205 (100%) | 2.664504 (100%) |
| unroll4           | 0.020658  (91%) | 0.026968  (60%) | 0.139545  (99%) | 2.565613  (96%) |
| sse               | 0.020708  (92%) | 0.025110  (56%) | 0.071954  (51%) | 2.473974  (93%) |
| sse_soa           | 0.017811  (79%) | 0.025449  (57%) | 0.101485  (72%) | 3.203995 (120%) |
| sse_aosoa         | 0.018805  (83%) | 0.029702  (66%) | 0.102752  (73%) | 2.607835  (98%) |
| sse_soa_stream    | 0.016594  (73%) | 0.031280  (70%) | 0.164421 (116%) | 2.209606  (83%) |
| sse_aosoa_stream  | 0.022017  (97%) | 0.027574  (61%) | 0.151497 (107%) | 2.172419  (82%) |

### In-place, 10 threads

| method            |          10000 |         100000 |        1000000 |       10000000 |
|-------------------|---------------:|---------------:|---------------:|---------------:|
| scalar            | 0.025322 (100%) | 0.034922 (100%) | 0.136023 (100%) | 1.972241 (100%) |
| unroll4           | 0.025698 (101%) | 0.038118 (109%) | 0.137051 (101%) | 1.951510  (99%) |
| sse               | 0.021421  (85%) | 0.028065  (80%) | 0.094335  (69%) | 1.797413  (91%) |
| sse_soa           | 0.021080  (83%) | 0.030004  (86%) | 0.094257  (69%) | 1.774623  (90%) |
| sse_aosoa         | 0.024175  (95%) | 0.029468  (84%) | 0.090550  (67%) | 1.847181  (94%) |
| sse_soa_stream    | 0.023231  (92%) | 0.044965 (129%) | 0.258720 (190%) | 2.361543 (120%) |
| sse_aosoa_stream  | 0.027774 (110%) | 0.045094 (129%) | 0.254618 (187%) | 2.399228 (122%) |

## Layout

- `transform2d.cpp` — benchmark driver and all method implementations
- `transform2d.sln` / `transform2d.vcxproj` — MSVC project
