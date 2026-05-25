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

## Layout

- `transform2d.cpp` — benchmark driver and all method implementations
- `transform2d.sln` / `transform2d.vcxproj` — MSVC project
