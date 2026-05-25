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

All times are mean ms per iteration. Lower is better.

### Out-of-place, 1 thread

| method            |    10000 |   100000 |  1000000 | 10000000 |
|-------------------|---------:|---------:|---------:|---------:|
| scalar            | 0.020906 | 0.064900 | 0.695361 | 8.738957 |
| unroll4           | 0.005705 | 0.056912 | 0.712218 | 8.155486 |
| sse               | 0.002433 | 0.024538 | 0.477430 | 7.139704 |
| sse_soa           | 0.004169 | 0.027517 | 0.387839 | 5.833594 |
| sse_aosoa         | 0.001571 | 0.015827 | 0.429494 | 7.483938 |
| sse_soa_stream    | 0.002109 | 0.021431 | 0.248584 | 4.241549 |
| sse_aosoa_stream  | 0.001775 | 0.018262 | 0.222189 | 5.441975 |

### In-place, 1 thread (rotation-only mat)

| method            |    10000 |   100000 |  1000000 | 10000000 |
|-------------------|---------:|---------:|---------:|---------:|
| scalar            | 0.007088 | 0.061080 | 0.591226 | 7.707195 |
| unroll4           | 0.005381 | 0.054865 | 0.576547 | 7.599540 |
| sse               | 0.003087 | 0.023226 | 0.255571 | 5.504468 |
| sse_soa           | 0.001567 | 0.014476 | 0.173936 | 3.984765 |
| sse_aosoa         | 0.001450 | 0.014552 | 0.197189 | 5.202200 |
| sse_soa_stream    | 0.014998 | 0.046707 | 0.476616 | 4.985594 |
| sse_aosoa_stream  | 0.014036 | 0.053074 | 0.608411 | 5.891704 |

### Out-of-place, 4 threads

| method            |    10000 |   100000 |  1000000 | 10000000 |
|-------------------|---------:|---------:|---------:|---------:|
| scalar            | 0.034567 | 0.044439 | 0.186432 | 3.388888 |
| unroll4           | 0.021096 | 0.040477 | 0.169815 | 3.367522 |
| sse               | 0.014780 | 0.022101 | 0.087526 | 3.056883 |
| sse_soa           | 0.017536 | 0.033403 | 0.099376 | 3.486429 |
| sse_aosoa         | 0.016105 | 0.018621 | 0.106797 | 2.917099 |
| sse_soa_stream    | 0.012372 | 0.020627 | 0.130900 | 2.245459 |
| sse_aosoa_stream  | 0.012497 | 0.024197 | 0.120155 | 2.403116 |

### In-place, 4 threads

| method            |    10000 |   100000 |  1000000 | 10000000 |
|-------------------|---------:|---------:|---------:|---------:|
| scalar            | 0.014046 | 0.028398 | 0.173902 | 2.912799 |
| unroll4           | 0.015557 | 0.030472 | 0.165384 | 2.687157 |
| sse               | 0.005149 | 0.018711 | 0.088046 | 2.239477 |
| sse_soa           | 0.008990 | 0.017174 | 0.070176 | 2.060998 |
| sse_aosoa         | 0.007771 | 0.017181 | 0.073334 | 2.151821 |
| sse_soa_stream    | 0.013807 | 0.037698 | 0.283076 | 2.518155 |
| sse_aosoa_stream  | 0.015666 | 0.037221 | 0.284567 | 2.595931 |

### Out-of-place, 10 threads

| method            |    10000 |   100000 |  1000000 | 10000000 |
|-------------------|---------:|---------:|---------:|---------:|
| scalar            | 0.022589 | 0.044898 | 0.141205 | 2.664504 |
| unroll4           | 0.020658 | 0.026968 | 0.139545 | 2.565613 |
| sse               | 0.020708 | 0.025110 | 0.071954 | 2.473974 |
| sse_soa           | 0.017811 | 0.025449 | 0.101485 | 3.203995 |
| sse_aosoa         | 0.018805 | 0.029702 | 0.102752 | 2.607835 |
| sse_soa_stream    | 0.016594 | 0.031280 | 0.164421 | 2.209606 |
| sse_aosoa_stream  | 0.022017 | 0.027574 | 0.151497 | 2.172419 |

### In-place, 10 threads

| method            |    10000 |   100000 |  1000000 | 10000000 |
|-------------------|---------:|---------:|---------:|---------:|
| scalar            | 0.025322 | 0.034922 | 0.136023 | 1.972241 |
| unroll4           | 0.025698 | 0.038118 | 0.137051 | 1.951510 |
| sse               | 0.021421 | 0.028065 | 0.094335 | 1.797413 |
| sse_soa           | 0.021080 | 0.030004 | 0.094257 | 1.774623 |
| sse_aosoa         | 0.024175 | 0.029468 | 0.090550 | 1.847181 |
| sse_soa_stream    | 0.023231 | 0.044965 | 0.258720 | 2.361543 |
| sse_aosoa_stream  | 0.027774 | 0.045094 | 0.254618 | 2.399228 |

## Layout

- `transform2d.cpp` — benchmark driver and all method implementations
- `transform2d.sln` / `transform2d.vcxproj` — MSVC project
