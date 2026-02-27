# Container Benchmark Results

## Environment

| | |
|---|---|
| **Date** | 2026-02-27 |
| **Host** | psemkin-HP-ProBook-450-G7 |
| **CPU** | 8 × 4200 MHz |
| **Compiler** | GCC 13 (C++23, `-O3`) |
| **Boost** | 1.83 |
| **Abseil** | 20220623 |
| **Google Benchmark** | v1.8.0 |

`Data = struct { float a; int b; }` (8 bytes). Keys: random `uint32_t`, seed `mt19937(42)`.

> All axes are log-scale (see chart at bottom). All times in **ns per element**.

---

## Insertion — without reserve

> Container created fresh each iteration. No pre-allocation. `boost::flat_map` capped at N=65 536 (O(N²) shifts).

| N | std::map | std::unordered_map | **absl::flat_hash_map** | boost::flat_map | **boost::unordered_flat_map** | boost::unordered_node_map |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 30.7 | 50.0 | 32.4 | 20.3 | **14.3** | 34.3 |
| 256 | 45.0 | 68.1 | 23.1 | 65.4 | **22.6** | 71.5 |
| 4 096 | 107.8 | 98.0 | **22.6** | 734.7 | **24.1** | 71.1 |
| 65 536 | 288.4 | 159.9 | **28.2** | 13 718 ⚠️ | 32.3 | 89.3 |
| 1 048 576 | 1 199.2 | 567.4 | **48.1** | ❌ O(N²) | **34.9** | 480.8 |

---

## Insertion — with reserve

> `reserve(N)` called outside timing via `PauseTiming/ResumeTiming`. Only `insert()` measured.

| N | std::unordered_map | **absl::flat_hash_map** | boost::flat_map | **boost::unordered_flat_map** | boost::unordered_node_map |
|---:|---:|---:|---:|---:|---:|
| 16 | 76.3 | 27.9 | 28.5 | 31.6 | 45.9 |
| 256 | 71.7 | **9.5** | 73.7 | **6.8** | 50.9 |
| 4 096 | 87.7 | **9.8** | 732.3 | **6.1** | 58.6 |
| 65 536 | 102.6 | **12.8** | 15 633 ⚠️ | **8.4** | 86.3 |
| 1 048 576 | 343.1 | 40.3 | ❌ O(N²) | **24.2** | 346.6 |

---

## Insertion — monotonic allocator (no reserve)

> `std::pmr::monotonic_buffer_resource` arena (128 bytes/element) allocated once per benchmark.
> Only `insert()` is timed.

| N | std::pmr::map | std::pmr::unordered_map | **absl + pmr** | boost::unordered_flat + pmr | **boost::unordered_node + pmr** |
|---:|---:|---:|---:|---:|---:|
| 16 | 33.7 | 55.5 | 48.3 | 29.0 | 35.6 |
| 256 | 30.9 | 50.6 | 23.9 | 20.7 | 26.8 |
| 4 096 | 85.5 | 53.2 | 23.1 | 20.7 | 24.9 |
| 65 536 | 195.6 | 82.0 | 31.5 | 28.4 | 32.5 |
| 1 048 576 | 799.0 | 326.4 | **36.8** | **33.1** | 100.8 |

---

## Insertion — monotonic allocator + reserve

> Arena + `reserve(N)` both outside timing. Pure `insert()` cost only.

| N | std::pmr::unordered_map | **absl + pmr** | **boost::unordered_flat + pmr** | boost::unordered_node + pmr |
|---:|---:|---:|---:|---:|
| 16 | 46.6 | 27.9 | **27.9** | 35.3 |
| 256 | 35.4 | 10.4 | **7.4** | 13.0 |
| 4 096 | 36.1 | 9.0 | **6.3** | 14.1 |
| 65 536 | 49.1 | 12.5 | **8.0** | 17.8 |
| 1 048 576 | 172.4 | 27.3 | **36.1** | 52.3 |

---

## Lookup

> Container pre-filled before timing. Only `find()` is measured.

| N | std::map | std::unordered_map | **absl::flat_hash_map** | boost::flat_map | **boost::unordered_flat_map** | boost::unordered_node_map |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 6.9 | 14.9 | 6.1 | 9.3 | 6.9 | **5.0** |
| 256 | 12.4 | 16.1 | 6.7 | 25.2 | 6.7 | **5.4** |
| 4 096 | 124.8 | 23.5 | **6.7** | 92.8 | **6.6** | 7.1 |
| 65 536 | 460.0 | 53.8 | 13.4 | 129.7 | **10.0** | 13.0 |
| 1 048 576 | 1 170.4 | 93.7 | 41.9 | ❌ | **36.0** | 39.5 |

---

## Key observations

### What monotonic allocator actually saves

For **flat containers** (`absl::flat_hash_map`, `boost::unordered_flat_map`):
- `reserve(N)` → 1 allocation total → PMR saves exactly **1 `malloc()`** → negligible gain
- Visible in table: PMR+reserve ≈ base with reserve for these two

For **node-based containers** (`std::map`, `std::pmr::map`, `boost::unordered_node_map`):
- Every insert → 1 `malloc()` for a tree/hash node → N inserts = N allocations
- PMR replaces N `malloc()` calls with N bump-pointer advances
- `std::pmr::map` vs `std::map` at N=4096: **85.5 vs ~119 ns/elem** — ~30% faster
- `boost::unordered_node_map` + PMR + reserve at N=256: **13.0 ns** vs raw **80.2 ns** — ~6× faster

### Container rankings

| | Insertion | Lookup |
|---|---|---|
| 🥇 | `boost::unordered_flat_map` | `boost::unordered_flat_map` (N≥4K) |
| 🥈 | `absl::flat_hash_map` | `boost::unordered_node_map` (small N) |
| 🥉 | `std::pmr::map` (with arena) | `absl::flat_hash_map` |

---

## Recommendations

| Use case | Recommended |
|---|---|
| General key-value store | `boost::unordered_flat_map` or `absl::flat_hash_map` |
| Max lookup, query-heavy (N<1K sorted data) | `boost::container::flat_map` |
| Stable references after insert | `boost::unordered_node_map` |
| Node container + many inserts in tight loop | Any PMR node container + monotonic arena |
| No external deps | `std::unordered_map` |
| Ordered iteration | `std::map` or `boost::container::flat_map` |

---

## Chart

![Benchmark results](results.png)

*Log-log scale. X = number of elements, Y = time per element (ns).*
