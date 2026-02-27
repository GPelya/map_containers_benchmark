#!/usr/bin/env python3
"""
Reads results.json produced by hello_benchmark and generates results.png
with three side-by-side log-scale charts:
  1. Insertion (no reserve)
  2. Insertion with reserve
  3. Lookup

Usage:
    ./build/hello_benchmark --benchmark_out=results.json --benchmark_out_format=json
    python3 plot_results.py [results.json]
"""

import json
import sys
import re
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# ── config ──────────────────────────────────────────────────────────────────

INPUT_FILE  = sys.argv[1] if len(sys.argv) > 1 else "results.json"
OUTPUT_FILE = "results.png"

# Human-readable names and colors for each map type
MAP_STYLE = {
    "StdMap":             dict(label="std::map",                    color="#e74c3c", ls="-",  marker="o"),
    "StdUnorderedMap":    dict(label="std::unordered_map",          color="#e67e22", ls="-",  marker="s"),
    "AbslFlatHashMap":    dict(label="absl::flat_hash_map",         color="#2ecc71", ls="-",  marker="^"),
    "BoostFlatMap":       dict(label="boost::container::flat_map",  color="#9b59b6", ls="--", marker="D"),
    "BoostUnorderedFlat": dict(label="boost::unordered_flat_map",   color="#3498db", ls="-",  marker="P"),
    "BoostUnorderedNode": dict(label="boost::unordered_node_map",   color="#1abc9c", ls="-",  marker="X"),
    "IntrusiveSet":       dict(label="boost::intrusive::set",       color="#f39c12", ls="--", marker="v"),
}

# ── parse JSON ───────────────────────────────────────────────────────────────

with open(INPUT_FILE) as f:
    data = json.load(f)

# Benchmark name formats:
#   BM_MapInsertion<StdMap>/256
#   BM_MapInsertion<StdUnorderedMap, WithReserve>/256
#   BM_IntrusiveSetInsertion/256
RE      = re.compile(r"BM_Map(Insertion|Lookup)<(\w+)(?:,\s*(\w+))?>/(\d+)")
RE_INTR = re.compile(r"BM_IntrusiveSet(Insertion|Lookup)/(\d+)")

insertion     = defaultdict(list)  # no-reserve insertion
ins_reserve   = defaultdict(list)  # with-reserve insertion
lookup        = defaultdict(list)

for bm in data["benchmarks"]:
    cpu_ns = bm["cpu_time"]

    m = RE.match(bm["name"])
    if m:
        category  = m.group(1)
        map_type  = m.group(2)
        policy    = m.group(3)   # "WithReserve" or None
        n         = int(m.group(4))
        ns        = cpu_ns / n
        if category == "Lookup":
            lookup[map_type].append((n, ns))
        elif policy == "WithReserve":
            ins_reserve[map_type].append((n, ns))
        else:
            insertion[map_type].append((n, ns))
        continue

    m = RE_INTR.match(bm["name"])
    if m:
        category, n = m.group(1), int(m.group(2))
        (insertion if category == "Insertion" else lookup)["IntrusiveSet"].append((n, cpu_ns / n))

# Sort each series by n
for d in (insertion, ins_reserve, lookup):
    for k in d:
        d[k].sort()

# ── plot ─────────────────────────────────────────────────────────────────────

fig, axes = plt.subplots(1, 3, figsize=(24, 7))
fig.patch.set_facecolor("#1a1a2e")

datasets = [insertion,   ins_reserve,              lookup]
titles   = ["Insertion — ns/elem (no reserve)",
            "Insertion with reserve — ns/elem",
            "Lookup — ns/elem"]

def style_ax(ax, dataset, title):
    ax.set_facecolor("#16213e")
    ax.set_title(title, color="white", fontsize=13, pad=12)
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("Number of elements (N)", color="#aaaacc", fontsize=10)
    ax.set_ylabel("Time per element (ns)",  color="#aaaacc", fontsize=10)
    ax.tick_params(colors="#aaaacc", which="both")
    for spine in ax.spines.values():
        spine.set_edgecolor("#444466")
    ax.grid(True, which="major", color="#2a2a4a", linewidth=0.8)
    ax.grid(True, which="minor", color="#222240", linewidth=0.4)

    for map_type, points in sorted(dataset.items()):
        if not points:
            continue
        style = MAP_STYLE.get(map_type, dict(label=map_type, color="grey", ls="-", marker="o"))
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        ax.plot(xs, ys,
                label=style["label"],
                color=style["color"],
                linestyle=style["ls"],
                marker=style["marker"],
                markersize=6,
                linewidth=2,
                alpha=0.9)

    all_ns = sorted({p[0] for pts in dataset.values() for p in pts})
    if all_ns:
        ax.set_xticks(all_ns)
        ax.xaxis.set_major_formatter(ticker.FuncFormatter(
            lambda v, _: f"{int(v):,}" if v < 1024 else
                         (f"{int(v)//1024}K" if v < 1_048_576 else f"{int(v)//1_048_576}M")))
    ax.tick_params(axis="x", labelrotation=45, labelsize=8, colors="#aaaacc")
    ax.legend(facecolor="#0f3460", edgecolor="#444466",
              labelcolor="white", fontsize=9, loc="upper left")

for ax, dataset, title in zip(axes, datasets, titles):
    style_ax(ax, dataset, title)

plt.suptitle("Containers Benchmark",
             color="white", fontsize=16, fontweight="bold", y=1.01)
plt.tight_layout()
plt.savefig(OUTPUT_FILE, dpi=150, bbox_inches="tight",
            facecolor=fig.get_facecolor())
print(f"Saved → {OUTPUT_FILE}")
