#!/usr/bin/env python3
"""
Train the AI component of the Irrigation Advisory prototype.

Model 1  Irrigation-depth regressor   : ridge linear regression (closed form)
Model 2  Water-stress classifier      : logistic regression (IRLS / Newton)
Baseline Rule-based FAO-56 water balance (no learning) - used for comparison
         and as the fallback when no trained model file is present.

Both models use the SAME engineered features that the C inference code
(src/common.c: compute_features) computes, so the exported weights are used
directly by the Linux processes in Experiments 4 and 7.

Standard library only. Output:
  models/irrigation_model.txt   weights + standardisation (read by C code)
  models/model_metrics.txt      test-set evaluation (ML vs rule baseline)
Usage: python3 scripts/train_model.py [--data data/raw/training_history.csv]
"""
import argparse
import csv
import math
import os
import random

FEATURES = ["depl_mm", "etc3_mm", "eff_rain_mm", "heat_idx", "taw_mm",
            "rel_water", "kc", "tmax_c", "rain_3d_mm", "depl_black"]


def kc_for_age(age):
    if age <= 45:
        return 0.40
    if age <= 120:
        return 0.75
    if age <= 270:
        return 1.25
    return 0.75


def features(fc, wp, age, sm, tmax, tmin, rain3, rprob):
    """Must stay identical to compute_features() in src/common.c."""
    kc = kc_for_age(age)
    root = 300.0 + 600.0 * min(1.0, age / 150.0)
    tmean = (tmax + tmin) / 2.0
    et0 = 0.0023 * (tmean + 17.8) * math.sqrt(max(tmax - tmin, 0.1)) * 15.0
    depl = max(0.0, (fc - sm) / 100.0 * root)
    return [
        depl,
        kc * et0 * 3.0,
        rain3 * rprob,
        max(0.0, tmax - 36.0) * kc,
        (fc - wp) / 100.0 * root,
        (sm - wp) / (fc - wp),
        kc,
        tmax,
        rain3,
        depl if fc >= 35.0 else 0.0,
    ]


def rule_depth(x):
    """Rule-based baseline: refill depletion + crop ET - forecast rain."""
    depl, etc3, rain3 = x[0], x[1], x[8]
    return max(0.0, depl + etc3 - rain3)


def rule_stress(x):
    return 1 if x[5] < 0.5 else 0


def solve(a, b):
    """Gaussian elimination with partial pivoting (a: n x n, b: n)."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        for r in range(n):
            if r != c and m[r][c] != 0.0:
                f = m[r][c] / m[c][c]
                for k in range(c, n + 1):
                    m[r][k] -= f * m[c][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def standardise(X):
    n, d = len(X), len(X[0])
    mean = [sum(r[j] for r in X) / n for j in range(d)]
    std = [math.sqrt(sum((r[j] - mean[j]) ** 2 for r in X) / n) or 1.0 for j in range(d)]
    Z = [[(r[j] - mean[j]) / std[j] for j in range(d)] for r in X]
    return Z, mean, std


def ridge(Z, y, lam=1e-2):
    d = len(Z[0]) + 1
    A = [[0.0] * d for _ in range(d)]
    b = [0.0] * d
    for z, t in zip(Z, y):
        v = [1.0] + z
        for i in range(d):
            b[i] += v[i] * t
            for j in range(d):
                A[i][j] += v[i] * v[j]
    for i in range(1, d):
        A[i][i] += lam * len(Z)
    return solve(A, b)                      # [bias, w1..wd]


def logistic_irls(Z, y, lam=1e-2, iters=25):
    d = len(Z[0]) + 1
    w = [0.0] * d
    for _ in range(iters):
        H = [[0.0] * d for _ in range(d)]
        g = [0.0] * d
        for z, t in zip(Z, y):
            v = [1.0] + z
            s = sum(wi * vi for wi, vi in zip(w, v))
            p = 1.0 / (1.0 + math.exp(-max(-30.0, min(30.0, s))))
            r = p * (1 - p)
            for i in range(d):
                g[i] += (p - t) * v[i]
                for j in range(d):
                    H[i][j] += r * v[i] * v[j]
        for i in range(1, d):
            H[i][i] += lam * len(Z)
            g[i] += lam * len(Z) * w[i]
        step = solve(H, g)
        w = [wi - si for wi, si in zip(w, step)]
        if max(abs(s) for s in step) < 1e-8:
            break
    return w


def predict_lin(w, z):
    return w[0] + sum(a * b for a, b in zip(w[1:], z))


def regression_metrics(y, p):
    n = len(y)
    mae = sum(abs(a - b) for a, b in zip(y, p)) / n
    rmse = math.sqrt(sum((a - b) ** 2 for a, b in zip(y, p)) / n)
    my = sum(y) / n
    ss_tot = sum((a - my) ** 2 for a in y)
    r2 = 1 - sum((a - b) ** 2 for a, b in zip(y, p)) / ss_tot
    return mae, rmse, r2


def class_metrics(y, p):
    tp = sum(1 for a, b in zip(y, p) if a == 1 and b == 1)
    tn = sum(1 for a, b in zip(y, p) if a == 0 and b == 0)
    fp = sum(1 for a, b in zip(y, p) if a == 0 and b == 1)
    fn = sum(1 for a, b in zip(y, p) if a == 1 and b == 0)
    acc = (tp + tn) / len(y)
    prec = tp / (tp + fp) if tp + fp else 0.0
    rec = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
    return acc, prec, rec, f1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data/raw/training_history.csv")
    ap.add_argument("--out", default="models")
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()

    X, yd, ys = [], [], []
    with open(a.data) as f:
        for r in csv.DictReader(f):
            X.append(features(float(r["fc"]), float(r["wp"]), int(r["crop_age_days"]),
                              float(r["sm_now"]), float(r["tmax_c"]), float(r["tmin_c"]),
                              float(r["rain_3d_mm"]), float(r["rain_prob"])))
            yd.append(float(r["label_depth_mm"]))
            ys.append(int(r["label_stress"]))

    idx = list(range(len(X)))
    random.Random(a.seed).shuffle(idx)
    cut = int(0.8 * len(idx))
    tr, te = idx[:cut], idx[cut:]

    Ztr, mean, std = standardise([X[i] for i in tr])
    Zte = [[(X[i][j] - mean[j]) / std[j] for j in range(len(FEATURES))] for i in te]

    wreg = ridge(Ztr, [yd[i] for i in tr])
    wlog = logistic_irls(Ztr, [ys[i] for i in tr])

    yte_d = [yd[i] for i in te]
    yte_s = [ys[i] for i in te]
    p_ml = [max(0.0, predict_lin(wreg, z)) for z in Zte]
    p_rule = [rule_depth(X[i]) for i in te]
    s_ml = [1 if 1 / (1 + math.exp(-predict_lin(wlog, z))) >= 0.5 else 0 for z in Zte]
    s_rule = [rule_stress(X[i]) for i in te]

    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "irrigation_model.txt"), "w") as f:
        f.write("# Irrigation advisory model (KJS-AGR-01) - trained by scripts/train_model.py\n")
        f.write("# Synthetic training data. z = (x - mean) / std ; depth = b + w.z ; P(stress) = sigmoid(b + w.z)\n")
        f.write(f"features {len(FEATURES)}\n")
        f.write("names " + " ".join(FEATURES) + "\n")
        f.write("mean " + " ".join(f"{v:.8g}" for v in mean) + "\n")
        f.write("std " + " ".join(f"{v:.8g}" for v in std) + "\n")
        f.write("depth " + " ".join(f"{v:.8g}" for v in wreg) + "\n")
        f.write("stress " + " ".join(f"{v:.8g}" for v in wlog) + "\n")

    mae_m, rmse_m, r2_m = regression_metrics(yte_d, p_ml)
    mae_r, rmse_r, r2_r = regression_metrics(yte_d, p_rule)
    acc_m, pr_m, rc_m, f1_m = class_metrics(yte_s, s_ml)
    acc_r, pr_r, rc_r, f1_r = class_metrics(yte_s, s_rule)
    lines = [
        "Model evaluation on held-out test set (20%, synthetic data)",
        f"train samples = {len(tr)}, test samples = {len(te)}",
        "",
        "Irrigation depth (mm)        MAE     RMSE    R^2",
        f"  ML ridge regression     {mae_m:7.2f}  {rmse_m:7.2f}  {r2_m:6.3f}",
        f"  Rule baseline (FAO-56)  {mae_r:7.2f}  {rmse_r:7.2f}  {r2_r:6.3f}",
        "",
        "Water-stress classifier     Acc    Prec   Recall  F1",
        f"  ML logistic regression  {acc_m:6.3f} {pr_m:6.3f} {rc_m:6.3f} {f1_m:6.3f}",
        f"  Rule (RAW < 50%)        {acc_r:6.3f} {pr_r:6.3f} {rc_r:6.3f} {f1_r:6.3f}",
    ]
    with open(os.path.join(a.out, "model_metrics.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\n[train_model] weights written to {a.out}/irrigation_model.txt")


if __name__ == "__main__":
    main()
