#!/usr/bin/env python3
"""
Render report charts from the real run outputs (needs matplotlib).
usage: python3 scripts/make_charts.py [outputs_dir] [figure_dir]
"""
import csv
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = sys.argv[1] if len(sys.argv) > 1 else "outputs"
FIG = sys.argv[2] if len(sys.argv) > 2 else "report/figures"
os.makedirs(FIG, exist_ok=True)

INK, MUTED, GRID = "#1f2937", "#6b7280", "#e5e7eb"
SERIES = ["#2563eb", "#16a34a", "#f59e0b", "#dc2626", "#7c3aed", "#0891b2", "#db2777", "#65a30d"]
plt.rcParams.update({"font.size": 10, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
                     "xtick.color": INK, "ytick.color": INK, "axes.spines.top": False,
                     "axes.spines.right": False})


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, name), dpi=160)
    plt.close(fig)


def gantt(alg, title):
    path = os.path.join(OUT, "scheduling", f"gantt_{alg}.csv")
    rows = list(csv.DictReader(open(path)))
    tasks = sorted({r["task"] for r in rows if r["task"] != "idle"},
                   key=lambda t: int(next(r["pid"] for r in rows if r["task"] == t)[1:]))
    fig, ax = plt.subplots(figsize=(9, 3.2))
    for r in rows:
        if r["task"] == "idle":
            continue
        y = tasks.index(r["task"])
        s, e = int(r["start_tick"]), int(r["end_tick"])
        ax.barh(y, e - s, left=s, color=SERIES[y % len(SERIES)], edgecolor="white", linewidth=0.3)
    ax.set_yticks(range(len(tasks)))
    ax.set_yticklabels([f"P{i+1} {t}" for i, t in enumerate(tasks)])
    ax.invert_yaxis()
    ax.set_xlabel("time (ms)")
    ax.set_title(title, loc="left", color=INK)
    ax.grid(axis="x", color=GRID)
    save(fig, f"gantt_{alg}.png")


def sched_compare():
    rows = list(csv.DictReader(open(os.path.join(OUT, "scheduling", "summary_measured.csv"))))
    algs = [r["algorithm"] for r in rows]
    wt = [float(r["avg_wt_ms"]) for r in rows]
    tat = [float(r["avg_tat_ms"]) for r in rows]
    rt = [float(r["avg_rt_ms"]) for r in rows]
    x = range(len(algs))
    fig, ax = plt.subplots(figsize=(9, 3.6))
    w = 0.27
    for k, (vals, lbl, c) in enumerate([(wt, "Avg waiting", SERIES[0]), (tat, "Avg turnaround", SERIES[2]),
                                         (rt, "Avg response", SERIES[1])]):
        bars = ax.bar([i + (k - 1) * w for i in x], vals, w, label=lbl, color=c)
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v + 4, f"{v:.0f}", ha="center", fontsize=7, color=MUTED)
    ax.set_xticks(list(x))
    ax.set_xticklabels(algs)
    ax.set_ylabel("ms")
    ax.set_title("Scheduling comparison on measured bursts (lower is better)", loc="left", color=INK)
    ax.legend(frameon=False, ncol=3)
    ax.grid(axis="y", color=GRID)
    save(fig, "sched_compare.png")


def race_chart():
    def parse(p):
        vals = {}
        for line in open(p):
            m = re.match(r"(valid readings counted|per-plot table entries)\s+(\d+)\s+(\d+)", line)
            if m:
                vals[m.group(1)] = (int(m.group(2)), int(m.group(3)))
        return vals
    safe = parse(os.path.join(OUT, "run_logs", "exp7_safe.txt"))
    unsafe = parse(os.path.join(OUT, "run_logs", "exp7_unsafe.txt"))
    labels = ["Expected\n(sequential)", "SAFE\n(semaphore+mutex)", "UNSAFE\n(no mutex)"]
    vals = [safe["valid readings counted"][0], safe["valid readings counted"][1],
            unsafe["valid readings counted"][1]]
    fig, ax = plt.subplots(figsize=(6, 3.2))
    bars = ax.bar(labels, vals, color=[MUTED, SERIES[1], SERIES[3]], width=0.55)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + 1500, f"{v:,}", ha="center", color=INK)
    ax.set_ylabel("valid readings in shared table")
    ax.set_title("Exp 7: lost updates without mutual exclusion", loc="left", color=INK)
    ax.grid(axis="y", color=GRID)
    save(fig, "exp7_race.png")


def model_chart():
    txt = open(os.path.join(OUT, "run_logs", "model_metrics.txt")).read()
    ml = re.search(r"ML ridge regression\s+([\d.]+)\s+([\d.]+)", txt)
    rule = re.search(r"Rule baseline \(FAO-56\)\s+([\d.]+)\s+([\d.]+)", txt)
    fig, ax = plt.subplots(figsize=(6, 3))
    names = ["MAE", "RMSE"]
    x = range(2)
    a = [float(ml.group(1)), float(ml.group(2))]
    b = [float(rule.group(1)), float(rule.group(2))]
    ax.bar([i - 0.18 for i in x], a, 0.36, label="ML ridge regression", color=SERIES[0])
    ax.bar([i + 0.18 for i in x], b, 0.36, label="Rule baseline (FAO-56)", color=SERIES[2])
    for i in x:
        ax.text(i - 0.18, a[i] + 0.6, f"{a[i]:.1f}", ha="center", fontsize=8)
        ax.text(i + 0.18, b[i] + 0.6, f"{b[i]:.1f}", ha="center", fontsize=8)
    ax.set_xticks(list(x))
    ax.set_xticklabels(names)
    ax.set_ylabel("error (mm)")
    ax.set_title("Irrigation-depth prediction error (test set)", loc="left", color=INK)
    ax.legend(frameon=False)
    ax.grid(axis="y", color=GRID)
    save(fig, "model_error.png")


def advisory_chart():
    rows = list(csv.DictReader(open(os.path.join(OUT, "pump_demand.csv"))))
    adv = list(csv.DictReader(open(os.path.join(OUT, "advisories.csv"), encoding="utf-8")))
    villages = [r["village"] for r in rows]
    cats = [("1", "Irrigate today", SERIES[3]), ("2", "Tomorrow", SERIES[2]),
            ("3", "Within 3 days", SERIES[0]), ("4", "No irrigation", SERIES[1])]
    fig, ax = plt.subplots(figsize=(8, 3.2))
    left = [0] * len(villages)
    for code, lbl, c in cats:
        vals = [sum(1 for a in adv if a["village"] == v and a["priority"] == code) for v in villages]
        ax.barh(villages, vals, left=left, label=lbl, color=c)
        left = [l + v for l, v in zip(left, vals)]
    ax.invert_yaxis()
    ax.set_xlabel("number of plots")
    ax.set_title("Advisory output per village (600 plots)", loc="left", color=INK)
    ax.legend(frameon=False, ncol=4, loc="lower center", bbox_to_anchor=(0.5, -0.45))
    save(fig, "advisory_mix.png")


if __name__ == "__main__":
    gantt("SJF", "SJF (non-preemptive), batch cycle - all tasks arrive at t=0")
    gantt("RR_q5", "Round Robin q = 5 ms, batch cycle")
    sched_compare()
    race_chart()
    model_chart()
    advisory_chart()
    print(f"charts written to {FIG}/")
