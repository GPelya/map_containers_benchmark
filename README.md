# C++ Container Benchmarks

Benchmarking insertion and lookup performance of various C++ associative containers
over element counts from 2 to 1 048 576.

## Containers

### Standard / open-addressing / tree

| Container | Library | Structure |
|---|---|---|
| `std::map` | STL | Red-black tree |
| `std::unordered_map` | STL | Chaining hash table |
| `absl::flat_hash_map` | [Abseil](https://abseil.io/) | Open-addressing, SSE2 metadata |
| `boost::container::flat_map` | [Boost.Container](https://www.boost.org/doc/libs/release/libs/container/) | Sorted contiguous array |
| `boost::unordered::unordered_flat_map` | [Boost.Unordered](https://www.boost.org/doc/libs/release/libs/unordered/) | Open-addressing, SIMD probing |
| `boost::unordered::unordered_node_map` | [Boost.Unordered](https://www.boost.org/doc/libs/release/libs/unordered/) | Node-based open-addressing |
| `boost::intrusive::set` | [Boost.Intrusive](https://www.boost.org/doc/libs/release/libs/intrusive/) | Intrusive red-black tree (zero allocations) |

### With monotonic allocator (`std::pmr::monotonic_buffer_resource`)

Same containers backed by a pre-allocated arena — eliminates `malloc()` per node/rehash.

| Container | Notes |
|---|---|
| `std::pmr::map` | Node-based; all N nodes → N bump allocations |
| `std::pmr::unordered_map` | Bucket array + per-node bump |
| `absl::flat_hash_map` + pmr | One big flat allocation; minimal PMR benefit |
| `boost::unordered_flat_map` + pmr | Same — one flat allocation |
| `boost::unordered_node_map` + pmr | Node-based; significant PMR benefit |

`Data = struct { float a; int b; }` (8 bytes). Keys are random `uint32_t` (seed `mt19937(42)`).

## Benchmark categories

| Name | What is measured |
|---|---|
| **Insertion** | Build map from scratch, no `reserve()` |
| **Insertion with reserve** | `reserve(N)` excluded from timing, then insert |
| **Insertion + Monotonic** | Insert into PMR-backed container (arena pre-allocated once) |
| **Insertion + Monotonic + reserve** | Arena + `reserve(N)`, only `insert()` timed |
| **Lookup** | `find()` on a pre-filled container |

## Project structure

```
.
├── benchmark.cpp       # All benchmarks (Google Benchmark)
├── CMakeLists.txt      # Build (Release, FetchContent for gbench)
├── plot_results.py     # Generates results.png from results.json
├── run_benchmarks.sh   # One-shot: run → progress bar → save JSON → plot
├── RESULTS.md          # Detailed results and analysis
└── .gitignore
```

## Dependencies

- **CMake** ≥ 3.22
- **GCC / Clang** with C++23
- **Boost** ≥ 1.81 — `sudo apt install libboost-all-dev`
- **Abseil** — `sudo apt install libabsl-dev`
- **Google Benchmark** v1.8 (auto-downloaded via FetchContent)
- **Python 3** + matplotlib — `pip3 install matplotlib`

## Build & run

```bash
# Configure + build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run everything (progress indicator + auto-generate chart)
./run_benchmarks.sh

# Subset of benchmarks
./run_benchmarks.sh --benchmark_filter="BM_MapInsertionPMR"

# Manual run
./build/hello_benchmark --benchmark_out=results.json --benchmark_out_format=json
python3 plot_results.py
```

## How it works

### `BM_MapInsertion` / `BM_MapLookup`

Template functions parameterised by `MapType` and an optional `ReservePolicy` tag:

```cpp
struct WithReserve {};   // reserve(N) called outside timing
struct NoReserve   {};   // no pre-allocation (default)

template <typename MapType, typename ReservePolicy = NoReserve>
static void BM_MapInsertion(benchmark::State& state);

template <typename MapType>
static void BM_MapLookup(benchmark::State& state);
```

`BENCHMARK_TEMPLATE` registers each combination. Adding a new container:

```cpp
using MyMap = my::map<uint32_t, Data>;
template <> struct MapTraits<MyMap> { static void reserve(MyMap& m, size_t n) { m.reserve(n); } };
BENCHMARK_TEMPLATE(BM_MapInsertion, MyMap)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapLookup,    MyMap)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
```

### `BM_MapInsertionPMR`

Arena buffer allocated **once** outside the timing loop and reused across iterations.
Only the `insert()` calls are timed:

```cpp
std::vector<std::byte> buf(n * 128);       // allocated once per benchmark
for (auto _ : state) {
    state.PauseTiming();
    std::pmr::monotonic_buffer_resource pool(buf.data(), buf.size());
    auto s = MapTraits<PmrMapType>::construct(&pool);  // factory per container type
    if constexpr (WithReserve) MapTraits<PmrMapType>::reserve(s, n);
    state.ResumeTiming();
    for (int i = 0; i < n; ++i) s.insert({g_data[i], Data{}});
}
```

### `boost::intrusive::set`

Uses a dedicated function and a global node pool (`g_nodes`).
The container holds references to pre-existing objects — zero heap allocations during insert.

## Chart

![Benchmark chart](results.png)

*Log-log scale. X = number of elements N, Y = time per element (ns).*

Detailed tables — [RESULTS.md](RESULTS.md).
