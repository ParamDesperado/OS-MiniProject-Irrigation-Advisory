#!/usr/bin/env bash
# run_all.sh - one command that runs the whole project.
# Builds, checks inputs, runs the app (Exp 4) and Experiments 6, 7, 9,
# and saves every console output under outputs/run_logs/.
set -u
cd "$(dirname "$0")/.."

LOGDIR=outputs/run_logs
mkdir -p "$LOGDIR" logs

banner() { printf '\n\033[1;32m############ %s ############\033[0m\n' "$1"; }

banner "Environment"
echo "Host     : $(hostname)   User: $(whoami)"
echo "OS       : $(grep PRETTY_NAME /etc/os-release 2>/dev/null | cut -d= -f2 | tr -d '\"')"
echo "Kernel   : $(uname -r)    CPUs: $(nproc)"
echo "gcc      : $(gcc --version | head -1)"
echo "python3  : $(python3 --version 2>&1)"

banner "Build"
make all || { echo "Build failed"; exit 1; }

banner "Input check"
for f in data/raw/plots.csv data/raw/sensor_readings.csv data/raw/weather_forecast.csv; do
    if [ ! -s "$f" ]; then
        echo "missing $f -> generating synthetic data"
        python3 scripts/generate_data.py
        break
    fi
done
[ -s models/irrigation_model.txt ] || python3 scripts/train_model.py
wc -l data/raw/*.csv
cat models/model_metrics.txt

banner "EXP 4 - fork/exec/wait pipeline (the application)"
./bin/pipeline | tee "$LOGDIR/exp4_pipeline.txt"
cat outputs/advisory_report.txt | tee "$LOGDIR/advisory_report.txt"

banner "EXP 6 - SJF and Round Robin on measured bursts"
./bin/scheduler -a measured -t 1 -q 2,5,10 | tee "$LOGDIR/exp6_measured.txt"
./bin/scheduler -a zero -t 1 -q 2,5,10     | tee "$LOGDIR/exp6_batch.txt"

banner "EXP 7 - Producer-Consumer (safe)"
./bin/sensor_sync | tee "$LOGDIR/exp7_safe.txt"
banner "EXP 7 - Producer-Consumer (UNSAFE: race condition demo)"
./bin/sensor_sync --unsafe | tee "$LOGDIR/exp7_unsafe.txt"

banner "EXP 9 - Banker's algorithm"
./bin/banker | tee "$LOGDIR/exp9_banker.txt"

banner "Done"
echo "All console outputs saved in $LOGDIR/"
