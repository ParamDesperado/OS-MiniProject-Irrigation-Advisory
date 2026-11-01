#!/usr/bin/env bash
# demo.sh - guided, step-by-step demonstration of the project.
# Each step shows the command, runs it, and waits for Enter.
#   bash scripts/demo.sh          (interactive)
#   bash scripts/demo.sh --auto   (no pauses)
cd "$(dirname "$0")/.."
AUTO=0; [ "${1:-}" = "--auto" ] && AUTO=1

G='\033[1;32m'; B='\033[1;34m'; Y='\033[1;33m'; C='\033[1;36m'; N='\033[0m'

title() {
    [ $AUTO -eq 0 ] && clear
    printf "${C}══════════════════════════════════════════════════════════════════════${N}\n"
    printf "${C}  %s${N}\n" "$1"
    [ -n "${2:-}" ] && printf "  %s\n" "$2"
    printf "${C}══════════════════════════════════════════════════════════════════════${N}\n\n"
}
run() {
    printf "${G}\$ %s${N}\n" "$1"
    bash -c "$1"
    echo
}
pause() {
    [ $AUTO -eq 1 ] && return
    printf "${Y}── press Enter for the next step ──${N}"
    read -r _
}

title "AI Irrigation Advisory for Sugarcane  (Use case KJS-AGR-01)" \
      "OS Mini Project · Experiments 4, 6, 7, 9 · running on Linux"
run 'grep PRETTY_NAME /etc/os-release; uname -sr; gcc --version | head -1'
run 'make 2>&1 | tail -3'
pause

title "INPUT DATA  (synthetic, 600 plots in 5 villages)"
run 'head -4 data/raw/plots.csv'
run 'head -4 data/raw/sensor_readings.csv; wc -l data/raw/sensor_readings.csv'
run 'head -4 data/raw/weather_forecast.csv'
pause

title "EXPERIMENT 4 · fork / exec / wait + file system calls" \
      "The parent process runs every stage of the workflow as a child process"
run './bin/pipeline'
pause

title "EXPERIMENT 4 · process tree while the pipeline runs" \
      "All predict processes have the pipeline as their parent (PPID)"
run './bin/pipeline -o /tmp/o -l /tmp/l >/dev/null 2>&1 & sleep 0.1; ps -o pid,ppid,stat,args --ppid $! -p $! | cut -c1-80; wait'
pause

title "OUTPUT · plot-wise irrigation advisory"
run 'sed -n "5,14p" outputs/advisory_report.txt'
run 'sed -n "/Sample farmer/,\$p" outputs/advisory_report.txt | grep -v "HI:"'
pause

title "EXPERIMENT 6 · SJF and Round Robin" \
      "Burst times = CPU time of each pipeline process, measured with wait4()"
run './bin/scheduler -a zero -q 2,5,10 | sed -n "6,15p;/==== SJF/,/^ [0-9]/p"'
pause
title "EXPERIMENT 6 · comparison of all scheduling policies"
run './bin/scheduler -a zero -q 2,5,10 | sed -n "/COMPARISON/,\$p"'
pause

title "EXPERIMENT 7 · Producer-Consumer (semaphores + mutex)" \
      "5 sensor-gateway threads -> bounded buffer -> 3 analytics threads"
run './bin/sensor_sync'
pause

title "EXPERIMENT 7 · same program WITHOUT the mutex  (--unsafe)"
run './bin/sensor_sync --unsafe'
pause

title "EXPERIMENT 9 · Banker's algorithm" \
      "Villages share pumps, power slots, canal valves and fertigation units"
run './bin/banker | sed -n "4,21p"'
pause
title "EXPERIMENT 9 · handling resource requests" \
      "GRANTED if the state stays safe · DENIED if it could lead to deadlock"
run './bin/banker | grep -E "^REQUEST|^RELEASE|=>|ERROR|WAIT"'
pause

title "TESTING · normal and failure scenarios"
run 'bash tests/run_tests.sh'
printf "${B}Demo complete. Code, report and screenshots: github.com/ParamDesperado/OS-MiniProject-Irrigation-Advisory${N}\n"
