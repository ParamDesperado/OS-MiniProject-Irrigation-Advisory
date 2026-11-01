/*
 * scheduler.c - EXPERIMENT 6: Shortest Job First and Round Robin scheduling.
 *
 * The processes are the real tasks of the irrigation-advisory workflow
 * (acquire_sensors, acquire_weather, predict_<village> ..., advise_report).
 * Burst times are MEASURED: CPU time (user+sys) of each child process,
 * recorded by the Exp-4 pipeline with wait4() in logs/stage_times.csv.
 * If that log is missing, documented estimates (data/config/task_estimates.csv)
 * are used and a warning is printed.
 *
 * Arrival modes:
 *   measured : arrival = fork offset measured by the pipeline
 *   zero     : all tasks of one advisory cycle are queued at t = 0 on a
 *              single-core field gateway (batch scenario)
 *
 * Algorithms: FCFS (reference), SJF non-preemptive, SRTF (preemptive SJF),
 * Round Robin for every quantum given with -q (default 2,5,10 ticks).
 *
 * usage: scheduler [-f stage_times.csv] [-e estimates.csv] [-a measured|zero]
 *                  [-t ms_per_tick] [-q q1,q2,...] [-o out_dir]
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAXP   32
#define MAXSEG 20000

typedef struct {
    char   name[48];
    int    at, bt;                /* arrival, burst (ticks) */
    double bt_ms;
    int    ct, tat, wt, rt;       /* completion, turnaround, waiting, response */
    int    rem, started;
} Proc;

typedef struct { int pid, start, end; } Seg;

typedef struct {
    char   alg[24];
    double avg_wt, avg_tat, avg_rt, throughput;
    int    makespan, switches;
} Result;

static Proc base[MAXP], p[MAXP];
static int  n;
static Seg  seg[MAXSEG];
static int  nseg;

static void add_seg(int pid, int s, int e)
{
    if (nseg > 0 && seg[nseg - 1].pid == pid && seg[nseg - 1].end == s) { seg[nseg - 1].end = e; return; }
    if (nseg < MAXSEG) seg[nseg++] = (Seg){pid, s, e};
}

static void reset(void)
{
    memcpy(p, base, sizeof(Proc) * n);
    for (int i = 0; i < n; i++) { p[i].rem = p[i].bt; p[i].started = 0; p[i].rt = -1; }
    nseg = 0;
}

static void run_fcfs(void)
{
    reset();
    int order[MAXP];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++)                      /* stable sort by arrival */
        for (int j = i + 1; j < n; j++)
            if (p[order[j]].at < p[order[i]].at) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    int t = 0;
    for (int k = 0; k < n; k++) {
        Proc *x = &p[order[k]];
        if (t < x->at) { add_seg(-1, t, x->at); t = x->at; }
        x->rt = t - x->at;
        add_seg(order[k], t, t + x->bt);
        t += x->bt;
        x->ct = t;
    }
}

static void run_sjf(void)
{
    reset();
    int done = 0, t = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++)
            if (p[i].rem > 0 && p[i].at <= t &&
                (best < 0 || p[i].bt < p[best].bt || (p[i].bt == p[best].bt && p[i].at < p[best].at)))
                best = i;
        if (best < 0) {                              /* CPU idle until next arrival */
            int next = INT_MAX;
            for (int i = 0; i < n; i++) if (p[i].rem > 0 && p[i].at < next) next = p[i].at;
            add_seg(-1, t, next);
            t = next;
            continue;
        }
        p[best].rt = t - p[best].at;
        add_seg(best, t, t + p[best].bt);
        t += p[best].bt;
        p[best].rem = 0;
        p[best].ct = t;
        done++;
    }
}

static void run_srtf(void)
{
    reset();
    int done = 0, t = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++)
            if (p[i].rem > 0 && p[i].at <= t && (best < 0 || p[i].rem < p[best].rem)) best = i;
        if (best < 0) { add_seg(-1, t, t + 1); t++; continue; }
        if (p[best].rt < 0) p[best].rt = t - p[best].at;
        add_seg(best, t, t + 1);
        t++;
        if (--p[best].rem == 0) { p[best].ct = t; done++; }
    }
}

static void run_rr(int q)
{
    reset();
    int queue[MAXP * 4096], head = 0, tail = 0, inq[MAXP] = {0};
    int done = 0, t = 0;
    int cap = (int)(sizeof queue / sizeof queue[0]);

    #define ENQ_ARRIVALS(upto) \
        for (int i = 0; i < n; i++) \
            if (!inq[i] && p[i].rem > 0 && p[i].at <= (upto)) { queue[tail++ % cap] = i; inq[i] = 1; }

    ENQ_ARRIVALS(t);
    while (done < n) {
        if (head == tail) {                           /* idle */
            int next = INT_MAX;
            for (int i = 0; i < n; i++) if (p[i].rem > 0 && p[i].at < next) next = p[i].at;
            add_seg(-1, t, next);
            t = next;
            ENQ_ARRIVALS(t);
            continue;
        }
        int i = queue[head++ % cap];
        if (p[i].rt < 0) p[i].rt = t - p[i].at;
        int run = p[i].rem < q ? p[i].rem : q;
        add_seg(i, t, t + run);
        t += run;
        p[i].rem -= run;
        ENQ_ARRIVALS(t);                              /* new arrivals go before the preempted task */
        if (p[i].rem > 0) queue[tail++ % cap] = i;
        else { p[i].ct = t; done++; }
    }
    #undef ENQ_ARRIVALS
}

static Result report(const char *alg, double tick, const char *outdir)
{
    Result r;
    memset(&r, 0, sizeof r);
    snprintf(r.alg, sizeof r.alg, "%s", alg);
    double sw = 0, st = 0, sr = 0;
    int first = INT_MAX;
    for (int i = 0; i < n; i++) {
        p[i].tat = p[i].ct - p[i].at;
        p[i].wt = p[i].tat - p[i].bt;
        sw += p[i].wt; st += p[i].tat; sr += p[i].rt;
        if (p[i].ct > r.makespan) r.makespan = p[i].ct;
        if (p[i].at < first) first = p[i].at;
    }
    for (int k = 1; k < nseg; k++)
        if (seg[k].pid != seg[k - 1].pid && seg[k].pid >= 0 && seg[k - 1].pid >= 0) r.switches++;
    r.avg_wt = sw / n; r.avg_tat = st / n; r.avg_rt = sr / n;
    r.throughput = n / ((r.makespan - first) * tick / 1000.0);     /* tasks per second */

    printf("\n==================== %s ====================\n", alg);
    printf("%-4s %-22s %6s %6s %6s %6s %6s %6s\n", "PID", "Task", "AT", "BT", "CT", "TAT", "WT", "RT");
    for (int i = 0; i < n; i++)
        printf("P%-3d %-22s %6d %6d %6d %6d %6d %6d\n", i + 1, p[i].name, p[i].at, p[i].bt,
               p[i].ct, p[i].tat, p[i].wt, p[i].rt);
    printf("Average WT = %.2f ticks (%.2f ms)   Average TAT = %.2f ticks (%.2f ms)   Avg RT = %.2f ticks\n",
           r.avg_wt, r.avg_wt * tick, r.avg_tat, r.avg_tat * tick, r.avg_rt);
    printf("Context switches = %d   Throughput = %.1f tasks/s\n", r.switches, r.throughput);

    /* ASCII Gantt chart, wrapped; long RR charts show first and last 12 slices */
    printf("Gantt chart (1 tick = %.2f ms):\n", tick);
    int per_line = 12;
    for (int s0 = 0; s0 < nseg; s0 += per_line) {
        if (nseg > 3 * per_line && s0 == per_line) {
            printf("   ... %d more slices (full chart in gantt_%s.csv) ...\n", nseg - 2 * per_line, alg);
            s0 = nseg - per_line;
        }
        int s1 = s0 + per_line < nseg ? s0 + per_line : nseg;
        printf(" ");
        for (int k = s0; k < s1; k++) {
            if (seg[k].pid < 0) printf("|%-5s", "idle");
            else printf("|P%-4d", seg[k].pid + 1);
        }
        printf("|\n ");
        for (int k = s0; k < s1; k++) printf("%-6d", seg[k].start);
        printf("%d\n", seg[s1 - 1].end);
    }

    char path[512];
    snprintf(path, sizeof path, "%s/gantt_%s.csv", outdir, alg);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "pid,task,start_tick,end_tick\n");
        for (int k = 0; k < nseg; k++)
            fprintf(f, "%s%d,%s,%d,%d\n", seg[k].pid < 0 ? "" : "P", seg[k].pid < 0 ? 0 : seg[k].pid + 1,
                    seg[k].pid < 0 ? "idle" : p[seg[k].pid].name, seg[k].start, seg[k].end);
        fclose(f);
    }
    return r;
}

static int load_tasks(const char *path, int estimates, double tick, int zero_arrival)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[MAX_LINE];
    char *fl[MAX_FIELDS];
    n = 0;
    if (!fgets(line, sizeof line, f)) { fclose(f); return 0; }
    while (n < MAXP && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        int nf = split_csv(line, fl, MAX_FIELDS);
        double at_ms, bt_ms;
        if (estimates) {                              /* name,arrival_ms,burst_ms */
            if (nf < 3) continue;
            at_ms = atof(fl[1]); bt_ms = atof(fl[2]);
        } else {                                      /* task,stage,pid,start,end,wall,cpu,exit */
            if (nf < 8 || atoi(fl[7]) != 0) continue;
            at_ms = atof(fl[3]); bt_ms = atof(fl[6]);
        }
        if (bt_ms <= 0) bt_ms = tick;                 /* CPU time below timer resolution */
        Proc *x = &base[n++];
        memset(x, 0, sizeof *x);
        snprintf(x->name, sizeof x->name, "%s", fl[0]);
        x->bt_ms = bt_ms;
        x->bt = (int)(bt_ms / tick + 0.5);
        if (x->bt < 1) x->bt = 1;
        x->at = zero_arrival ? 0 : (int)(at_ms / tick + 0.5);
    }
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    const char *file = "logs/stage_times.csv", *est = "data/config/task_estimates.csv";
    const char *outdir = "outputs/scheduling", *amode = "measured";
    char qlist[128] = "2,5,10";
    double tick = 1.0;
    int opt;
    while ((opt = getopt(argc, argv, "f:e:a:t:q:o:h")) != -1) {
        switch (opt) {
        case 'f': file = optarg; break;
        case 'e': est = optarg; break;
        case 'a': amode = optarg; break;
        case 't': tick = atof(optarg); break;
        case 'q': snprintf(qlist, sizeof qlist, "%s", optarg); break;
        case 'o': outdir = optarg; break;
        default:
            fprintf(stderr, "usage: %s [-f stage_times.csv] [-e estimates.csv] [-a measured|zero] "
                            "[-t ms_per_tick] [-q q1,q2,..] [-o out_dir]\n", argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    if (tick <= 0) { fprintf(stderr, "ERROR: tick must be > 0 ms\n"); return 1; }
    int zero = strcmp(amode, "zero") == 0;
    if (!zero && strcmp(amode, "measured") != 0) { fprintf(stderr, "ERROR: -a must be measured or zero\n"); return 1; }

    int quanta[8], nq = 0;
    for (char *tok = strtok(qlist, ","); tok && nq < 8; tok = strtok(NULL, ",")) {
        int q = atoi(tok);
        if (q <= 0) { fprintf(stderr, "ERROR: invalid time quantum '%s' (must be a positive integer)\n", tok); return 1; }
        quanta[nq++] = q;
    }

    const char *source = "MEASURED (wait4 rusage CPU time from Exp-4 pipeline)";
    if (load_tasks(file, 0, tick, zero) <= 0) {
        fprintf(stderr, "WARNING: '%s' missing or empty - run ./bin/pipeline first. Falling back to estimates.\n", file);
        if (load_tasks(est, 1, tick, zero) <= 0) {
            fprintf(stderr, "ERROR: estimates file '%s' also unavailable: %s\n", est, strerror(errno));
            return 2;
        }
        file = est;
        source = "DOCUMENTED ESTIMATES";
    }
    mkdir("outputs", 0750);
    mkdir(outdir, 0750);

    printf("EXPERIMENT 6 - CPU scheduling of irrigation-advisory tasks (KJS-AGR-01)\n");
    printf("Source of burst times : %s\n", source);
    printf("Input file            : %s\n", file);
    printf("Arrival mode          : %s   |   1 tick = %.2f ms\n", zero ? "zero (batch cycle)" : "measured fork offsets", tick);
    printf("\n%-4s %-22s %10s %12s %8s %8s\n", "PID", "Task", "arrival", "CPU burst", "AT", "BT");
    for (int i = 0; i < n; i++)
        printf("P%-3d %-22s %8.2fms %10.3fms %8d %8d\n", i + 1, base[i].name, base[i].at * tick,
               base[i].bt_ms, base[i].at, base[i].bt);

    Result res[16];
    int nr = 0;
    run_fcfs(); res[nr++] = report("FCFS", tick, outdir);
    run_sjf();  res[nr++] = report("SJF", tick, outdir);
    run_srtf(); res[nr++] = report("SRTF", tick, outdir);
    for (int k = 0; k < nq; k++) {
        char name[24];
        snprintf(name, sizeof name, "RR_q%d", quanta[k]);
        run_rr(quanta[k]);
        res[nr++] = report(name, tick, outdir);
    }

    char path[512];
    snprintf(path, sizeof path, "%s/summary_%s.csv", outdir, zero ? "zero" : "measured");
    FILE *f = fopen(path, "w");
    printf("\n==================== COMPARISON ====================\n");
    printf("%-10s %12s %12s %12s %9s %10s\n", "Algorithm", "Avg WT(ms)", "Avg TAT(ms)", "Avg RT(ms)", "Switches", "Tasks/s");
    if (f) fprintf(f, "algorithm,avg_wt_ms,avg_tat_ms,avg_rt_ms,context_switches,throughput\n");
    int best_wt = 0, best_rt = 0;
    for (int k = 0; k < nr; k++) {
        printf("%-10s %12.2f %12.2f %12.2f %9d %10.1f\n", res[k].alg, res[k].avg_wt * tick,
               res[k].avg_tat * tick, res[k].avg_rt * tick, res[k].switches, res[k].throughput);
        if (f) fprintf(f, "%s,%.3f,%.3f,%.3f,%d,%.2f\n", res[k].alg, res[k].avg_wt * tick,
                       res[k].avg_tat * tick, res[k].avg_rt * tick, res[k].switches, res[k].throughput);
        if (res[k].avg_wt < res[best_wt].avg_wt) best_wt = k;
        if (res[k].avg_rt < res[best_rt].avg_rt) best_rt = k;
    }
    if (f) fclose(f);
    printf("\nLowest average waiting time : %s\n", res[best_wt].alg);
    printf("Lowest average response time: %s\n", res[best_rt].alg);
    printf("Gantt segments written to %s/gantt_<alg>.csv\n", outdir);
    return 0;
}
