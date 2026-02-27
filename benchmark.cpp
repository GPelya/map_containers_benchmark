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
//  MapTraits: per-container behaviour (reserve, etc.)
// ============================================================
template <typename MapType>
struct MapTraits {
    // Primary template: reserve is a no-op (e.g. std::map)
    static void reserve(MapType&, size_t) {}
};

template <typename MapType, typename ReservePolicy = NoReserve>
static void BM_MapInsertion(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        MapType s;
        if constexpr (std::is_same_v<ReservePolicy, WithReserve>)
            MapTraits<MapType>::reserve(s, n);
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

// ============================================================
//  MapTraits specializations — containers that support reserve()
// ============================================================
template <> struct MapTraits<StdUnorderedMap>    { static void reserve(StdUnorderedMap& m,    size_t n) { m.reserve(n); } };
template <> struct MapTraits<AbslFlatHashMap>    { static void reserve(AbslFlatHashMap& m,    size_t n) { m.reserve(n); } };
template <> struct MapTraits<BoostFlatMap>       { static void reserve(BoostFlatMap& m,       size_t n) { m.reserve(n); } };
template <> struct MapTraits<BoostUnorderedFlat> { static void reserve(BoostUnorderedFlat& m, size_t n) { m.reserve(n); } };
template <> struct MapTraits<BoostUnorderedNode> { static void reserve(BoostUnorderedNode& m, size_t n) { m.reserve(n); } };

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
