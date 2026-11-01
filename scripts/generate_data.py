#!/usr/bin/env python3
"""
Synthetic data generator for the Sugarcane Irrigation Advisory prototype
(Use case KJS-AGR-01).

ALL DATA PRODUCED HERE IS CLEARLY-LABELLED SYNTHETIC DATA. No real farmer,
plot or sensor record is used. Farmer IDs are random anonymised tokens.

Outputs (in data/raw/):
  plots.csv              - plot master records (geo-fenced plots)
  sensor_readings.csv    - 14 days of soil-moisture sensor readings every 2 h,
                           with injected faults (missing / out-of-range /
                           malformed rows) to exercise data validation
  weather_forecast.csv   - 3-day forecast per village
  training_history.csv   - historical labelled samples used to train the ML
                           models (labels come from a FAO-56 style water
                           balance simulation + noise, documented below)

Standard library only, fixed seed => fully reproducible.
Usage: python3 scripts/generate_data.py [--plots 600] [--seed 42] [--out data/raw]
"""
import argparse
import csv
import math
import os
import random

# Villages in the KIAAR / GBL command area (Bagalkot district, N. Karnataka).
# Plot counts are deliberately unequal so per-village jobs have different
# CPU bursts (used by Experiment 6).
VILLAGES = [
    ("Sameerwadi", 0.30, 16.38, 75.04),
    ("Mudhol",     0.23, 16.33, 75.28),
    ("Jamkhandi",  0.20, 16.50, 75.29),
    ("Rabkavi",    0.15, 16.47, 75.11),
    ("Terdal",     0.12, 16.49, 75.05),
]

# Soil hydraulic properties (volumetric %): field capacity, wilting point
SOILS = {
    "black": (40.0, 20.0),   # deep black cotton soil (Vertisol)
    "red":   (25.0, 12.0),
    "loam":  (32.0, 15.0),
}
SOIL_MIX = ["black"] * 6 + ["loam"] * 3 + ["red"] * 2

HOURS = 14 * 24          # sensor history length
STEP = 2                 # sensor sampling interval (hours)


def kc_for_age(age):
    """FAO-56 crop coefficient for sugarcane by crop age (days)."""
    if age <= 45:
        return 0.40      # germination
    if age <= 120:
        return 0.75      # tillering
    if age <= 270:
        return 1.25      # grand growth
    return 0.75          # maturity / ripening


def root_depth_mm(age):
    return 300.0 + 600.0 * min(1.0, age / 150.0)


def hargreaves_et0(tmax, tmin, ra=15.0):
    """Reference evapotranspiration (mm/day), Hargreaves equation."""
    tmean = (tmax + tmin) / 2.0
    return 0.0023 * (tmean + 17.8) * math.sqrt(max(tmax - tmin, 0.1)) * ra


def ground_truth(sm, fc, wp, age, tmax, tmin, rain3, rprob, rng):
    """
    Synthetic 'agronomist-validated' label generator.
    depth_mm : net irrigation depth needed over the next 3 days
    stress   : 1 if the crop is expected to enter water stress
    Effects included that the simple rule baseline ignores: rain probability,
    effective-rainfall fraction, heat stress above 36 C and a soil-type
    specific refill target.
    """
    kc = kc_for_age(age)
    root = root_depth_mm(age)
    et0 = hargreaves_et0(tmax, tmin)
    depl = max(0.0, (fc - sm) / 100.0 * root)
    etc3 = kc * et0 * 3.0
    eff_rain = rain3 * rprob * 0.8
    heat = max(0.0, tmax - 36.0) * kc * 3.0 * 0.8
    refill = 0.85 if fc >= 35 else 0.95           # black soil: under-refill
    need = refill * depl + etc3 + heat - eff_rain
    taw = (fc - wp) / 100.0 * root
    allowed = 0.35 * taw                           # management allowed depletion
    depth = max(0.0, need - allowed * 0.5) + rng.gauss(0, 2.0)
    depth = max(0.0, min(depth, 120.0))

    rel = (sm - wp) / (fc - wp)                    # relative available water
    z = -9.0 * (rel - 0.45) + 0.35 * max(0.0, tmax - 34.0) - 0.06 * eff_rain + 0.6 * (kc - 0.75)
    p = 1.0 / (1.0 + math.exp(-z))
    stress = 1 if rng.random() < p else 0
    return round(depth, 2), stress


def gen_plots(n, rng):
    plots = []
    weights = [v[1] for v in VILLAGES]
    pid = 1
    for vi, (vname, w, lat, lon) in enumerate(VILLAGES):
        count = round(n * w / sum(weights)) if vi < len(VILLAGES) - 1 else n - len(plots)
        for _ in range(count):
            soil = rng.choice(SOIL_MIX)
            plots.append({
                "plot_id": f"P{pid:04d}",
                "farmer_id": f"F{rng.randrange(16**6):06x}",       # anonymised token
                "village": vname,
                "soil_type": soil,
                "area_ha": round(rng.uniform(0.4, 3.5), 2),
                "crop_age_days": rng.randint(10, 360),
                "irrigation_method": "drip" if rng.random() < 0.35 else "furrow",
                "pump_kw": rng.choice([3.7, 5.5, 7.5]),
                "lat": round(lat + rng.uniform(-0.04, 0.04), 5),
                "lon": round(lon + rng.uniform(-0.04, 0.04), 5),
            })
            pid += 1
    return plots


def gen_weather(rng):
    rows = []
    # Each village gets a distinct forecast so advisories differ by village.
    profiles = {
        "Sameerwadi": (33.5, 0.10, 0.0),
        "Mudhol":     (35.5, 0.05, 0.0),
        "Jamkhandi":  (31.0, 0.75, 9.0),   # rain expected
        "Rabkavi":    (37.2, 0.05, 0.0),   # heat
        "Terdal":     (32.0, 0.40, 3.0),
    }
    for vname, _, _, _ in VILLAGES:
        tbase, pbase, rbase = profiles[vname]
        for day in range(1, 4):
            tmax = round(tbase + rng.uniform(-1.0, 1.0), 1)
            tmin = round(tmax - rng.uniform(11.0, 14.0), 1)
            prob = round(min(0.98, max(0.0, pbase + rng.uniform(-0.05, 0.1))), 2)
            rain = round(max(0.0, rbase + rng.uniform(-2.0, 4.0)) if prob > 0.3 else 0.0, 1)
            rows.append({
                "village": vname, "day": day,
                "date": f"2026-11-0{day + 1}",
                "tmax_c": tmax, "tmin_c": tmin,
                "rh_pct": round(rng.uniform(45, 80), 0),
                "wind_ms": round(rng.uniform(1.0, 4.0), 1),
                "rain_mm": rain, "rain_prob": prob,
            })
    return rows


def gen_sensors(plots, rng):
    """Soil moisture drying curve with irrigation events, diurnal soil temp,
    and ~1.5 % injected faults."""
    rows = []
    for p in plots:
        fc, wp = SOILS[p["soil_type"]]
        sm = rng.uniform(wp + 2, fc)
        kc = kc_for_age(p["crop_age_days"])
        dry_rate = (0.08 + 0.10 * kc) * (40.0 / fc) * STEP / 2.0
        for h in range(0, HOURS, STEP):
            sm -= dry_rate * rng.uniform(0.6, 1.4)
            if sm < wp + 1.5 and rng.random() < 0.15:     # farmer irrigates
                sm = fc - rng.uniform(0, 2)
            sm = max(wp - 1.0, sm)
            day = h // 24
            hour = h % 24
            ts = f"2026-10-{18 + day:02d}T{hour:02d}:00"
            temp = 24 + 6 * math.sin((hour - 9) / 24 * 2 * math.pi) + rng.gauss(0, 0.6)
            val = round(sm + rng.gauss(0, 0.4), 2)
            r = rng.random()
            if r < 0.005:
                rows.append(f"{ts},{h},{p['plot_id']},,{temp:.1f}")            # missing value
            elif r < 0.010:
                rows.append(f"{ts},{h},{p['plot_id']},{rng.choice([-5.0, 150.0, 999.0])},{temp:.1f}")  # sensor glitch
            elif r < 0.015:
                rows.append(f"{ts},{h},{p['plot_id']}#CORRUPT")                # malformed line
            else:
                rows.append(f"{ts},{h},{p['plot_id']},{val},{temp:.1f}")
    return rows


def gen_training(n, rng):
    rows = []
    for _ in range(n):
        soil = rng.choice(SOIL_MIX)
        fc, wp = SOILS[soil]
        age = rng.randint(10, 360)
        sm = rng.uniform(wp - 1, fc + 1)
        tmax = rng.uniform(28, 41)
        tmin = tmax - rng.uniform(10, 15)
        rprob = rng.choice([0.0, 0.05, 0.1, 0.3, 0.5, 0.7, 0.9])
        rain3 = rng.uniform(0, 30) if rprob >= 0.3 else rng.uniform(0, 3)
        depth, stress = ground_truth(sm, fc, wp, age, tmax, tmin, rain3, rprob, rng)
        rows.append({
            "soil_type": soil, "fc": fc, "wp": wp, "crop_age_days": age,
            "sm_now": round(sm, 2), "tmax_c": round(tmax, 1), "tmin_c": round(tmin, 1),
            "rain_3d_mm": round(rain3, 1), "rain_prob": rprob,
            "label_depth_mm": depth, "label_stress": stress,
        })
    return rows


def write_csv(path, rows, header):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=header)
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plots", type=int, default=600)
    ap.add_argument("--train", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", default="data/raw")
    a = ap.parse_args()

    rng = random.Random(a.seed)
    os.makedirs(a.out, exist_ok=True)

    plots = gen_plots(a.plots, rng)
    write_csv(os.path.join(a.out, "plots.csv"), plots, list(plots[0].keys()))

    weather = gen_weather(rng)
    write_csv(os.path.join(a.out, "weather_forecast.csv"), weather, list(weather[0].keys()))

    sensors = gen_sensors(plots, rng)
    with open(os.path.join(a.out, "sensor_readings.csv"), "w", newline="") as f:
        f.write("timestamp,hour,plot_id,soil_moisture_pct,soil_temp_c\n")
        f.write("\n".join(sensors) + "\n")

    train = gen_training(a.train, rng)
    write_csv(os.path.join(a.out, "training_history.csv"), train, list(train[0].keys()))

    print(f"[generate_data] SYNTHETIC data written to {a.out}/")
    print(f"  plots.csv            {len(plots):6d} plots in {len(VILLAGES)} villages")
    print(f"  sensor_readings.csv  {len(sensors):6d} readings (14 days x every {STEP} h, ~1.5% injected faults)")
    print(f"  weather_forecast.csv {len(weather):6d} rows (3-day forecast)")
    print(f"  training_history.csv {len(train):6d} labelled samples")


if __name__ == "__main__":
    main()
