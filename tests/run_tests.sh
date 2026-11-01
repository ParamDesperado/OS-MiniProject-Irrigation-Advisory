#!/usr/bin/env bash
# run_tests.sh - normal and failure-scenario tests for all four experiments.
# Each test states the expected behaviour and checks the exit code / output.
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0; fail=0

check() {   # check "<description>" <expected_exit> <command...>
    local desc=$1 want=$2; shift 2
    out=$("$@" 2>&1); got=$?
    if [ "$got" -eq "$want" ]; then
        printf '  PASS  %-62s (exit %d)\n' "$desc" "$got"; pass=$((pass+1))
    else
        printf '  FAIL  %-62s (exit %d, expected %d)\n' "$desc" "$got" "$want"; fail=$((fail+1))
        echo "$out" | tail -5 | sed 's/^/        /'
    fi
}
expect_text() {  # expect_text "<description>" "<pattern>" <command...>
    local desc=$1 pat=$2; shift 2
    if "$@" 2>&1 | grep -q -- "$pat"; then
        printf '  PASS  %-62s\n' "$desc"; pass=$((pass+1))
    else
        printf '  FAIL  %-62s (pattern "%s" not found)\n' "$desc" "$pat"; fail=$((fail+1))
    fi
}

echo "== Exp 4: pipeline (fork/exec/wait, file system calls) =="
check "normal run on synthetic data" 0 ./bin/pipeline -l "$T/logs"
mkdir -p "$T/nosensor/raw"
cp data/raw/plots.csv data/raw/weather_forecast.csv "$T/nosensor/raw/"
check "missing sensor file -> stage 1 fails, workflow aborts" 1 \
      ./bin/pipeline -d "$T/nosensor" -o "$T/out1" -l "$T/logs1"
mkdir -p "$T/corrupt/raw"
cp data/raw/plots.csv data/raw/weather_forecast.csv "$T/corrupt/raw/"
printf 'timestamp,hour,plot_id,soil_moisture_pct,soil_temp_c\n###garbage###\n,,,\nP0001,abc\n' > "$T/corrupt/raw/sensor_readings.csv"
check "corrupt sensor file -> acquire exits 3" 3 \
      ./bin/acquire sensors "$T/corrupt/raw/sensor_readings.csv" "$T/x.csv"
expect_text "missing model -> rule-based fallback warning" "rule-based baseline" \
      ./bin/pipeline -m "$T/no_model.txt" -o "$T/out2" -l "$T/logs2"
check "unknown village -> predict exits 3" 3 \
      ./bin/predict Atlantis data/raw/plots.csv data/processed/sensor_features.csv \
      data/processed/weather_features.csv models/irrigation_model.txt "$T/p.csv"

echo "== Exp 6: scheduler =="
check "measured bursts, quanta 2,5,10" 0 ./bin/scheduler -o "$T/s1"
expect_text "missing stage log -> falls back to documented estimates" "DOCUMENTED ESTIMATES" \
      ./bin/scheduler -f "$T/none.csv" -o "$T/s2"
check "invalid quantum 0 rejected" 1 ./bin/scheduler -q 0 -o "$T/s3"

echo "== Exp 7: producer-consumer =="
check "safe mode, buffer 8 -> consistent table" 0 ./bin/sensor_sync
check "safe mode, buffer 1 (always full/empty) -> consistent" 0 ./bin/sensor_sync -b 1 -p 8 -c 8
check "unsafe mode -> race detected (exit 4)" 4 ./bin/sensor_sync --unsafe -p 8 -c 8
check "invalid buffer size 0 rejected" 1 ./bin/sensor_sync -b 0

echo "== Exp 9: Banker's algorithm =="
expect_text "initial state is safe" "SAFE. Safe sequence" ./bin/banker
expect_text "unsafe request is denied" "DENIED" ./bin/banker
expect_text "request above max claim is rejected" "exceeds declared maximum" ./bin/banker
check "missing scenario file" 2 ./bin/banker "$T/none.txt"

echo
echo "Tests passed: $pass   failed: $fail"
[ "$fail" -eq 0 ]
