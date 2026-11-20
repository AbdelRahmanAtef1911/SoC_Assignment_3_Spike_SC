#!/usr/bin/env python3
"""Task 6 -- inter-cache-miss time histograms from the SystemC + Spike miss log.

Input : miss_log.csv (time_ns,cache,type,addr), written by the [A3] code in riscv/cachesim.cc
Output: hist_dcache_all.png, hist_dcache_phases.png, hist_l2_all.png + statistics on stdout.
At 1 GHz one clock period is 1 ns, so an inter-miss time in ns is also a number of cycles.
"""
import argparse
import csv
import re
import subprocess
from collections import Counter
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# same order as the phase() calls in sw/mess/mess_spike.c (marker 6 = end)
PHASES = ["init: MESS walk_generator", "MESS pointer-chase", "traffic R100 P0",
          "traffic R100 P50", "traffic R70 P0", "traffic R70 P50"]
LINE = 64
BAR, FIT, INK, GRID = "#2563c9", "#d1452f", "#333333", "#dddddd"


def marker_range(elf, nm):
    for line in subprocess.check_output([nm, elf], text=True).splitlines():
        f = line.split()
        if len(f) == 3 and f[2] == "mess_marker":
            base = int(f[0], 16)
            return base, base + 16 * LINE
    raise SystemExit("symbol mess_marker not found in " + elf)


def read_log(path):
    rows = {}
    with open(path, newline="") as fh:
        for r in csv.DictReader(fh):
            rows.setdefault(r["cache"], []).append((float(r["time_ns"]), int(r["addr"], 16)))
    return rows


def describe(name, gaps):
    g = np.asarray(gaps, dtype=float)
    if g.size == 0:
        return f"{name}: no gaps"
    mean, sd = g.mean(), g.std()
    top = ", ".join(f"{int(v)}:{100 * c / g.size:.1f}%"
                    for v, c in Counter(g.astype(int).tolist()).most_common(5))
    return (f"{name:<28s} n={g.size:<7d} mean={mean:8.2f}  median={np.median(g):6.1f}  "
            f"CV={sd / mean:5.2f}  min={g.min():.0f}  max={g.max():.0f}\n"
            f"{'':28s} most common gaps (ns:share)  {top}")


def draw(ax, gaps, title, xmax, logy):
    g = np.asarray(gaps, dtype=float)
    bins = np.arange(-0.5, xmax + 1.5, 1.0)               # one bin per cycle
    ax.hist(g, bins=bins, weights=np.full(g.size, 1.0 / g.size),
            color=BAR, rwidth=0.85, zorder=2, label="measured")
    mean = g.mean()
    x = np.arange(0, xmax + 1)
    ax.plot(x, np.exp(-x / mean) / mean, color=FIT, lw=2, zorder=3,
            label=f"memoryless exponential (mean {mean:.1f} ns)")
    beyond = 100.0 * np.count_nonzero(g > xmax) / g.size
    ax.set_title(f"{title}\n(n = {g.size}, {beyond:.1f}% of gaps > {xmax} ns)", fontsize=10, color=INK)
    ax.set_xlim(-1, xmax + 1)
    ax.set_xlabel("inter-miss time (ns = cycles at 1 GHz)", color=INK)
    ax.set_ylabel("fraction of misses", color=INK)
    if logy:
        ax.set_yscale("log")
        ax.set_ylim(bottom=0.5 / g.size)          # no empty decades below one sample
    ax.grid(axis="y", color=GRID, lw=0.6, zorder=0)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.legend(frameon=False, fontsize=8)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--log", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--outdir", default="results")
    ap.add_argument("--program-output", help="stdout of the SystemC run (for the minstret check)")
    ap.add_argument("--nm", default="riscv64-unknown-elf-nm")
    ap.add_argument("--xmax", type=int, default=120)
    a = ap.parse_args()
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)

    lo, hi = marker_range(a.elf, a.nm)
    log = read_log(a.log)
    print("misses per cache: " + ", ".join(f"{c}={len(v)}" for c, v in sorted(log.items())))

    d = log.get("D$", [])
    marks = [t for t, addr in d if lo <= addr < hi]
    times = np.array([t for t, addr in d if not lo <= addr < hi])
    gaps = np.diff(times)
    print(describe("D$ all phases", gaps))

    phase_gaps = {}
    if len(marks) == len(PHASES) + 1:
        minstret = {}
        if a.program_output:
            for line in open(a.program_output):
                m = re.match(r"\[mess\] marker (\d+) minstret=(\d+)", line)
                if m:
                    minstret[int(m.group(1))] = int(m.group(2))
        print("\nphase markers (SystemC time of the marker miss vs. minstret read just before it):")
        for k, t in enumerate(marks):
            extra = f"  minstret={minstret[k]:<9d} time-minstret={t - minstret[k]:.0f}" if k in minstret else ""
            print(f"  marker {k}: t={t:<10.0f} ns{extra}")
        print()
        for k, name in enumerate(PHASES):
            sel = times[(times >= marks[k]) & (times < marks[k + 1])]
            phase_gaps[name] = np.diff(sel)
            print(describe(f"D$ {name}", phase_gaps[name]))
    else:
        print(f"warning: found {len(marks)} phase markers, expected {len(PHASES) + 1}; no per-phase stats")

    l2 = np.array([t for t, addr in log.get("L2$", []) if not lo <= addr < hi])
    l2_gaps = np.diff(l2)
    print()
    print(describe("L2$ all phases", l2_gaps))

    # Figure 1: all D$ misses, linear (as in the slides) and log scale
    fig, ax = plt.subplots(1, 2, figsize=(13, 4.5))
    draw(ax[0], gaps, "L1 D$ (32 KiB) inter-miss time, MESS @ 1 GHz", a.xmax, False)
    draw(ax[1], gaps, "same data, log scale", a.xmax, True)
    fig.tight_layout()
    fig.savefig(out / "hist_dcache_all.png", dpi=150)

    # Figure 2: one panel per MESS phase
    if phase_gaps:
        fig, axs = plt.subplots(2, 3, figsize=(16, 8))
        for axp, (name, g) in zip(axs.flat, phase_gaps.items()):
            draw(axp, g, f"D$ - {name}", a.xmax, True)
        fig.tight_layout()
        fig.savefig(out / "hist_dcache_phases.png", dpi=150)

    # Figure 3: L2
    if l2_gaps.size:
        fig, ax = plt.subplots(1, 2, figsize=(13, 4.5))
        draw(ax[0], l2_gaps, "L2$ (256 KiB) inter-miss time, MESS @ 1 GHz", a.xmax, False)
        draw(ax[1], l2_gaps, "same data, log scale", a.xmax, True)
        fig.tight_layout()
        fig.savefig(out / "hist_l2_all.png", dpi=150)

    print("\nfigures: hist_dcache_all.png hist_dcache_phases.png hist_l2_all.png")


main()
