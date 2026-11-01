#!/usr/bin/env bash
# capture_screenshots.sh - run every part of the project on Linux and save a
# PNG screenshot of each terminal output (docs/screenshots/) plus the plain
# text of the same output (report/evidence/outputs/run_logs/).
# Needs: freeze  (go install github.com/charmbracelet/freeze@latest)
set -u
cd "$(dirname "$0")/.."
export PATH=$PATH:$(go env GOPATH 2>/dev/null)/bin
SHOT=docs/screenshots
LOG=report/evidence/outputs/run_logs
mkdir -p "$SHOT" "$LOG"

# shot <name> <log-file or -> <command...>
shot() {
    local name=$1 logf=$2 cmd=$3
    local s=/tmp/shot_$name.sh
    {
        echo "cd $PWD"
        printf 'printf "\\033[1;32mcodespace@ubuntu-24.04\\033[0m:\\033[1;34m~/OS-MiniProject\\033[0m$ %%s\\n" %q\n' "$cmd"
        if [ "$logf" = "-" ]; then echo "$cmd"; else echo "( $cmd ) 2>&1 | tee $LOG/$logf"; fi
    } > "$s"
    bash "$s" | freeze --language ansi --window --theme dracula --font.size 13 --line-height 1.25 \
           --padding 18 --border.radius 8 -o "$SHOT/$name.png" >/dev/null 2>&1
    echo "  saved $SHOT/$name.png"
}

make clean >/dev/null; rm -rf outputs logs backups data/processed
shot 01_build            build.txt          'grep PRETTY_NAME /etc/os-release; uname -sr; gcc --version | head -1; make'
shot 02_model_training   model_metrics.txt  'python3 scripts/train_model.py'
shot 03_exp4_pipeline    exp4_pipeline.txt  './bin/pipeline'
shot 04_exp4_process_tree ps_tree.txt      './bin/pipeline -o /tmp/o -l /tmp/l >/dev/null 2>&1 & sleep 0.1; ps -o pid,ppid,stat,args --ppid $! -p $! | cut -c1-80; wait'
shot 05_advisory_report  advisory_report.txt 'cat outputs/advisory_report.txt'
shot 06_exp6_sjf         exp6_batch.txt     './bin/scheduler -a zero -q 2,5,10 | sed -n "1,15p;/==== SJF/,/^ [0-9]/p"'
shot 07_exp6_comparison  exp6_measured.txt  './bin/scheduler -a measured -q 2,5,10 | sed -n "/COMPARISON/,\$p"'
shot 08_exp7_safe        exp7_safe.txt      './bin/sensor_sync'
shot 09_exp7_unsafe      exp7_unsafe.txt    './bin/sensor_sync --unsafe'
shot 10_exp9_banker      exp9_banker.txt    './bin/banker | head -42'
shot 11_exp9_decisions   -                  './bin/banker | grep -E "^REQUEST|^RELEASE|=>|ERROR|WAIT|DENIED"'
shot 12_tests            tests.txt          'bash tests/run_tests.sh'

# full logs used by the report (same binaries, same data)
./bin/scheduler -a zero -q 2,5,10 > $LOG/exp6_batch.txt
./bin/scheduler -a measured -q 2,5,10 > $LOG/exp6_measured.txt
./bin/banker > $LOG/exp9_banker.txt
cp logs/stage_times.csv outputs/pump_demand.csv $LOG/
rm -rf report/evidence/outputs/scheduling report/evidence/outputs/predictions
cp -r outputs/scheduling outputs/predictions outputs/advisories.csv outputs/advisory_report.txt \
      outputs/pump_demand.csv report/evidence/outputs/
echo "done"
