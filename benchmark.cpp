#include <benchmark/benchmark.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <random>
#include <cstdint>
#include <algorithm>

// Boost containers
#include <boost/container/flat_map.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_node_map.hpp>

// Abseil
#include <absl/container/flat_hash_map.h>

// PMR (monotonic allocator)
#include <memory_resource>

//static constexpr int MAX_ELEMS              = 1 << 26; // 67 108 864 (~67M, closest power of 2 to 100M)
static constexpr int MAX_ELEMS              = 1 << 20;
// boost::container::flat_map insertion is O(N^2), limit to avoid hour-long runs
static constexpr int FLAT_MAP_INSERTION_MAX = 1 << 16; // 65 536

static std::vector<uint32_t> g_data;

struct Data { float a; int b; };

// ── Intrusive set ────────────────────────────────────────────────────────────
namespace bi = boost::intrusive;

struct IntrusiveData : public bi::set_base_hook<bi::optimize_size<true>> {
    uint32_t key;
    Data     payload;

    // Comparator that supports find(uint32_t) directly
    struct KeyCmp {
        bool operator()(uint32_t k,             const IntrusiveData& v) const { return k < v.key; }
        bool operator()(const IntrusiveData& v, uint32_t k)             const { return v.key < k; }
        bool operator()(const IntrusiveData& a, const IntrusiveData& b) const { return a.key < b.key; }
    };
};
using IntrusiveSet = bi::set<IntrusiveData, bi::compare<IntrusiveData::KeyCmp>>;

// Pre-allocated node pool — intrusive containers don't own elements
static std::vector<IntrusiveData> g_nodes;

static void prepare_data() {
    std::mt19937 gen(42);
    std::uniform_int_distribution<uint32_t> dis;
    g_data.resize(MAX_ELEMS);
    std::generate(g_data.begin(), g_data.end(), [&]{ return dis(gen); });
    g_nodes.resize(MAX_ELEMS);
    for (int i = 0; i < MAX_ELEMS; ++i)
        g_nodes[i].key = g_data[i];
}

// ============================================================
//  Reserve policy tags
// ============================================================
struct WithReserve {};  // call MapTraits<T>::reserve() before filling
struct NoReserve   {};  // skip reserve — measure raw insertion cost

// ============================================================
//  MapTraits primary template (specializations follow after using aliases)
// ============================================================
template <typename MapType>
struct MapTraits {
    static void reserve(MapType&, size_t) {}
    static MapType construct(std::pmr::memory_resource*) { return MapType{}; }
};

template <typename MapType, typename ReservePolicy = NoReserve>
static void BM_MapInsertion(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        MapType s;
        if constexpr (std::is_same_v<ReservePolicy, WithReserve>) {
            state.PauseTiming();
            MapTraits<MapType>::reserve(s, n);
            state.ResumeTiming();
        }
        for (int i = 0; i < n; ++i)
            s.insert({g_data[i], Data{}});
    }
    state.SetItemsProcessed(state.iterations() * n);
}

template <typename MapType>
static void BM_MapLookup(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    MapType s;
    MapTraits<MapType>::reserve(s, n);
    for (int i = 0; i < n; ++i) s.insert({g_data[i], Data{}});
    for (auto _ : state) {
        for (int i = 0; i < n; ++i) {
            auto it = s.find(g_data[i]);
            benchmark::DoNotOptimize(it);
        }
    }
    state.SetItemsProcessed(state.iterations() * n);
}

using StdMap              = std::map<uint32_t, Data>;
using StdUnorderedMap     = std::unordered_map<uint32_t, Data>;
using AbslFlatHashMap     = absl::flat_hash_map<uint32_t, Data>;
using BoostFlatMap        = boost::container::flat_map<uint32_t, Data>;
using BoostUnorderedFlat  = boost::unordered::unordered_flat_map<uint32_t, Data>;
using BoostUnorderedNode  = boost::unordered::unordered_node_map<uint32_t, Data>;

// PMR aliases — backed by std::pmr::monotonic_buffer_resource
// Note: boost::container::pmr uses its own memory_resource hierarchy
// (incompatible with std::pmr), so we use std::pmr types directly.
using PmrStdMap          = std::pmr::map<uint32_t, Data>;
using PmrStdUnorderedMap = std::pmr::unordered_map<uint32_t, Data>;
// boost::unordered 1.83: pass std::pmr::polymorphic_allocator explicitly
using PmrBoostUnorderedFlat = boost::unordered::unordered_flat_map<
    uint32_t, Data,
    boost::hash<uint32_t>, std::equal_to<uint32_t>,
    std::pmr::polymorphic_allocator<std::pair<const uint32_t, Data>>>;
using PmrBoostUnorderedNode = boost::unordered::unordered_node_map<
    uint32_t, Data,
    boost::hash<uint32_t>, std::equal_to<uint32_t>,
    std::pmr::polymorphic_allocator<std::pair<const uint32_t, Data>>>;

// ============================================================
//  MapTraits: per-container reserve() + PMR construction
// ============================================================
// Primary: no reserve, construct ignores pool (non-PMR containers)

// Non-PMR containers with reserve()
template <> struct MapTraits<StdUnorderedMap>    { static void reserve(StdUnorderedMap& m,    size_t n) { m.reserve(n); } };
template <> struct MapTraits<AbslFlatHashMap>       { static void reserve(AbslFlatHashMap& m,       size_t n) { m.reserve(n); } };
template <> struct MapTraits<BoostFlatMap>          { static void reserve(BoostFlatMap& m,          size_t n) { m.reserve(n); } };
template <> struct MapTraits<BoostUnorderedFlat>    { static void reserve(BoostUnorderedFlat& m,    size_t n) { m.reserve(n); } };
template <> struct MapTraits<BoostUnorderedNode>    { static void reserve(BoostUnorderedNode& m,    size_t n) { m.reserve(n); } };

// PMR specializations — provide construct() + reserve()
template <> struct MapTraits<PmrStdMap> {
    static void reserve(PmrStdMap&, size_t) {}
    static PmrStdMap construct(std::pmr::memory_resource* p) { return PmrStdMap{p}; }
};
template <> struct MapTraits<PmrStdUnorderedMap> {
    static void reserve(PmrStdUnorderedMap& m, size_t n) { m.reserve(n); }
    static PmrStdUnorderedMap construct(std::pmr::memory_resource* p) { return PmrStdUnorderedMap{p}; }
};
template <> struct MapTraits<PmrBoostUnorderedFlat> {
    using Alloc = std::pmr::polymorphic_allocator<std::pair<const uint32_t, Data>>;
    static void reserve(PmrBoostUnorderedFlat& m, size_t n) { m.reserve(n); }
    static PmrBoostUnorderedFlat construct(std::pmr::memory_resource* p) {
        return PmrBoostUnorderedFlat{Alloc{p}};
    }
};
template <> struct MapTraits<PmrBoostUnorderedNode> {
    using Alloc = std::pmr::polymorphic_allocator<std::pair<const uint32_t, Data>>;
    static void reserve(PmrBoostUnorderedNode& m, size_t n) { m.reserve(n); }
    static PmrBoostUnorderedNode construct(std::pmr::memory_resource* p) {
        return PmrBoostUnorderedNode{Alloc{p}};
    }
};

// ============================================================
//  INSERTION benchmarks
// ============================================================
BENCHMARK_TEMPLATE(BM_MapInsertion, StdMap)             ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertion, StdUnorderedMap)    ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertion, AbslFlatHashMap)    ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
// boost::flat_map insertion is O(N^2) due to array shifts — capped at 64K
BENCHMARK_TEMPLATE(BM_MapInsertion, BoostFlatMap)       ->RangeMultiplier(2)->Range(2, FLAT_MAP_INSERTION_MAX);
BENCHMARK_TEMPLATE(BM_MapInsertion, BoostUnorderedFlat) ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertion, BoostUnorderedNode) ->RangeMultiplier(2)->Range(2, MAX_ELEMS);

// ============================================================
//  INSERTION WITH RESERVE benchmarks
//  (reserve(N) called before filling — amortises rehash cost)
//  StdMap has no reserve(), so it is omitted here.
// ============================================================
BENCHMARK_TEMPLATE(BM_MapInsertion, StdUnorderedMap,    WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertion, AbslFlatHashMap,    WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertion, BoostFlatMap,       WithReserve)->RangeMultiplier(2)->Range(2, FLAT_MAP_INSERTION_MAX);
BENCHMARK_TEMPLATE(BM_MapInsertion, BoostUnorderedFlat, WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertion, BoostUnorderedNode, WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);

// ============================================================
//  LOOKUP benchmarks  (container pre-filled outside timing loop)
// ============================================================
BENCHMARK_TEMPLATE(BM_MapLookup, StdMap)             ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapLookup, StdUnorderedMap)    ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapLookup, AbslFlatHashMap)    ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
// boost::flat_map pre-fill is also O(N^2) — capped at 64K
BENCHMARK_TEMPLATE(BM_MapLookup, BoostFlatMap)       ->RangeMultiplier(2)->Range(2, FLAT_MAP_INSERTION_MAX);
BENCHMARK_TEMPLATE(BM_MapLookup, BoostUnorderedFlat) ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapLookup, BoostUnorderedNode) ->RangeMultiplier(2)->Range(2, MAX_ELEMS);

// ============================================================
//  INSERTION WITH MONOTONIC ALLOCATOR benchmarks
//  Buffer is allocated outside timing loop; only insert() is measured.
// ============================================================
template <typename PmrMapType, typename ReservePolicy = NoReserve>
static void BM_MapInsertionPMR(benchmark::State& state) {
    const int    n         = static_cast<int>(state.range(0));
    const size_t buf_bytes = static_cast<size_t>(n) * 128;

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::byte> buf(buf_bytes);
        std::pmr::monotonic_buffer_resource pool(buf.data(), buf_bytes);
        auto s = MapTraits<PmrMapType>::construct(&pool);
        if constexpr (std::is_same_v<ReservePolicy, WithReserve>)
            MapTraits<PmrMapType>::reserve(s, n);
        state.ResumeTiming();

        for (int i = 0; i < n; ++i)
            s.insert({g_data[i], Data{}});
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// INSERTION WITH MONOTONIC ALLOCATOR
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrStdMap)             ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrStdUnorderedMap)    ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrBoostUnorderedFlat) ->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrBoostUnorderedNode) ->RangeMultiplier(2)->Range(2, MAX_ELEMS);

// INSERTION WITH MONOTONIC ALLOCATOR + RESERVE
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrStdUnorderedMap,    WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrBoostUnorderedFlat, WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);
BENCHMARK_TEMPLATE(BM_MapInsertionPMR, PmrBoostUnorderedNode, WithReserve)->RangeMultiplier(2)->Range(2, MAX_ELEMS);

// ============================================================
//  boost::intrusive::set  (separate functions — different API)
// ============================================================

static void BM_IntrusiveSetInsertion(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        IntrusiveSet s;
        for (int i = 0; i < n; ++i)
            s.insert(g_nodes[i]);
        s.clear();  // mandatory: release hooks before next iteration
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_IntrusiveSetInsertion)->RangeMultiplier(2)->Range(2, MAX_ELEMS);

static void BM_IntrusiveSetLookup(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    IntrusiveSet s;
    for (int i = 0; i < n; ++i) s.insert(g_nodes[i]);
    for (auto _ : state) {
        for (int i = 0; i < n; ++i) {
            auto it = s.find(g_data[i], IntrusiveData::KeyCmp{});
            benchmark::DoNotOptimize(it);
        }
    }
    state.SetItemsProcessed(state.iterations() * n);
    s.clear();
}
BENCHMARK(BM_IntrusiveSetLookup)->RangeMultiplier(2)->Range(2, MAX_ELEMS);


int main(int argc, char** argv) {
    prepare_data();

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
