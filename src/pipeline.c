/*
 * pipeline.c - EXPERIMENT 4: File and process handling using system calls.
 *
 * Parent process that drives the irrigation-advisory workflow:
 *
 *                       pipeline (parent)
 *        Stage 1 ┌──────────┴──────────┐  (concurrent)
 *          acquire sensors       acquire weather
 *        Stage 2 ┌───────┬───────┬─────┴─┬───────┐  (one child per village, concurrent)
 *          predict V1  predict V2  ...   predict Vn
 *        Stage 3         advise
 *
 * System calls used: fork, execv, wait4/waitpid (exit status + rusage),
 * open, read, write, close, mkdir, getpid, _exit.
 *
 * Each child's wall time and CPU time (user+sys from rusage) are logged to
 * <logs>/stage_times.csv; Experiment 6 uses them as measured burst times.
 * A failed stage (non-zero exit or signal) stops the workflow so later
 * stages never consume missing or corrupt intermediate files.
 *
 * usage: pipeline [-d data_dir] [-o out_dir] [-l log_dir] [-m model] [-b bin_dir] [-s mc_samples]
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TASKS 32

typedef struct {
    char   name[48];
    int    stage;
    pid_t  pid;
    double start_ms, end_ms, cpu_ms;
    int    status;           /* exit code, or 128+signal */
} Task;

static Task   tasks[MAX_TASKS];
static int    ntasks;
static double t_origin;
static int    log_fd = -1;

static void plog(const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    time_t t = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    printf("%s\n", msg);
    fflush(stdout);
    if (log_fd >= 0) fd_printf(log_fd, "%s [pipeline pid=%d] %s\n", ts, (int)getpid(), msg);
}

/* fork + execv one stage program; returns task index */
static int spawn(const char *name, int stage, char *const argv[])
{
    Task *t = &tasks[ntasks];
    snprintf(t->name, sizeof t->name, "%s", name);
    t->stage = stage;
    t->start_ms = now_ms() - t_origin;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {                       /* ---- child ---- */
        execv(argv[0], argv);
        fprintf(stderr, "execv %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    t->pid = pid;                         /* ---- parent ---- */
    plog("  fork -> pid %-6d exec %-28s (parent pid %d)", (int)pid, name, (int)getpid());
    return ntasks++;
}

/* Reap every child of the current stage with wait4(); returns #failures */
static int wait_stage(int stage)
{
    int pending = 0, failed = 0;
    for (int i = 0; i < ntasks; i++)
        if (tasks[i].stage == stage) pending++;

    while (pending > 0) {
        int status;
        struct rusage ru;
        pid_t pid = wait4(-1, &status, 0, &ru);
        if (pid < 0) {
            if (errno == EINTR) continue;
            perror("wait4");
            break;
        }
        for (int i = 0; i < ntasks; i++) {
            Task *t = &tasks[i];
            if (t->pid != pid) continue;
            t->end_ms = now_ms() - t_origin;
            t->cpu_ms = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0 +
                        ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
            if (WIFEXITED(status)) {
                t->status = WEXITSTATUS(status);
                plog("  wait <- pid %-6d %-28s exit=%d  wall=%7.2f ms  cpu=%7.2f ms",
                     (int)pid, t->name, t->status, t->end_ms - t->start_ms, t->cpu_ms);
            } else if (WIFSIGNALED(status)) {
                t->status = 128 + WTERMSIG(status);
                plog("  wait <- pid %-6d %-28s KILLED by signal %d", (int)pid, t->name, WTERMSIG(status));
            }
            if (t->status != 0) failed++;
            pending--;
        }
    }
    return failed;
}

static void write_stage_times(const char *logdir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/stage_times.csv", logdir);
    int fd = fd_create(path);
    if (fd < 0) return;
    fd_printf(fd, "task,stage,pid,start_ms,end_ms,wall_ms,cpu_ms,exit_code\n");
    for (int i = 0; i < ntasks; i++) {
        Task *t = &tasks[i];
        fd_printf(fd, "%s,%d,%d,%.3f,%.3f,%.3f,%.3f,%d\n", t->name, t->stage, (int)t->pid,
                  t->start_ms, t->end_ms, t->end_ms - t->start_ms, t->cpu_ms, t->status);
    }
    close(fd);
}

/* copy src -> dst with read()/write(); used to back up the previous advisory */
static int copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0440);   /* read-only backup */
    if (out < 0) { close(in); return -1; }
    char buf[8192];
    ssize_t r;
    long total = 0;
    while ((r = read(in, buf, sizeof buf)) > 0) {
        if (write(out, buf, (size_t)r) != r) { r = -1; break; }
        total += r;
    }
    close(in);
    close(out);
    return r < 0 ? -1 : (int)total;
}

static void ensure_dir(const char *p)
{
    if (mkdir(p, 0750) < 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", p, strerror(errno));
        exit(1);
    }
}

int main(int argc, char **argv)
{
    const char *data = "data", *out = "outputs", *logs = "logs", *bin = "bin";
    const char *model = "models/irrigation_model.txt", *samples = "4000";
    int opt;
    while ((opt = getopt(argc, argv, "d:o:l:m:b:s:h")) != -1) {
        switch (opt) {
        case 'd': data = optarg; break;
        case 'o': out = optarg; break;
        case 'l': logs = optarg; break;
        case 'm': model = optarg; break;
        case 'b': bin = optarg; break;
        case 's': samples = optarg; break;
        default:
            fprintf(stderr, "usage: %s [-d data_dir] [-o out_dir] [-l log_dir] [-m model] [-b bin_dir] [-s mc_samples]\n", argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    char raw[512], proc[512], preds[512], path[512], path2[512], prog[512];
    snprintf(raw, sizeof raw, "%s/raw", data);
    snprintf(proc, sizeof proc, "%s/processed", data);
    snprintf(preds, sizeof preds, "%s/predictions", out);
    ensure_dir(proc);
    ensure_dir(out);
    ensure_dir(preds);
    ensure_dir(logs);
    ensure_dir("backups");

    snprintf(path, sizeof path, "%s/pipeline.log", logs);
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0640);
    t_origin = now_ms();

    plog("=== Irrigation advisory pipeline (KJS-AGR-01)  parent pid=%d ===", (int)getpid());

    /* back up previous advisory output before it is overwritten */
    snprintf(path, sizeof path, "%s/advisories.csv", out);
    snprintf(path2, sizeof path2, "backups/advisories_%ld.csv", (long)time(NULL));
    int copied = copy_file(path, path2);
    if (copied >= 0) plog("backup: %s -> %s (%d bytes, mode 0440)", path, path2, copied);

    /* discover villages from plots.csv (read with open/read) */
    Plot *plots = malloc(sizeof(Plot) * MAX_PLOTS);
    snprintf(path, sizeof path, "%s/plots.csv", raw);
    int np = load_plots(path, plots, MAX_PLOTS);
    if (np <= 0) {
        plog("FATAL: cannot read plot master file %s (%s)", path, np < 0 ? strerror(errno) : "empty");
        return 2;
    }
    char villages[MAX_VILLAGES][NAME_LEN];
    int nv = 0;
    for (int i = 0; i < np; i++) {
        int k;
        for (k = 0; k < nv && strcmp(villages[k], plots[i].village) != 0; k++)
            ;
        if (k == nv && nv < MAX_VILLAGES) snprintf(villages[nv++], NAME_LEN, "%s", plots[i].village);
    }
    free(plots);
    plog("plots.csv: %d plots in %d villages", np, nv);

    /* -------- Stage 1: data acquisition (2 concurrent children) -------- */
    plog("\n[Stage 1] data acquisition");
    snprintf(prog, sizeof prog, "%s/acquire", bin);
    char s_in[512], s_out[512], w_in[512], w_out[512];
    snprintf(s_in, sizeof s_in, "%s/sensor_readings.csv", raw);
    snprintf(s_out, sizeof s_out, "%s/sensor_features.csv", proc);
    snprintf(w_in, sizeof w_in, "%s/weather_forecast.csv", raw);
    snprintf(w_out, sizeof w_out, "%s/weather_features.csv", proc);
    char *a1[] = {prog, "sensors", s_in, s_out, NULL};
    char *a2[] = {prog, "weather", w_in, w_out, NULL};
    spawn("acquire_sensors", 1, a1);
    spawn("acquire_weather", 1, a2);
    if (wait_stage(1)) {
        plog("Stage 1 FAILED - aborting workflow (prediction would use missing/corrupt data)");
        write_stage_times(logs);
        return 1;
    }

    /* -------- Stage 2: prediction (one child per village) -------- */
    plog("\n[Stage 2] AI prediction - %d village processes in parallel", nv);
    char pprog[512], plots_csv[512];
    snprintf(pprog, sizeof pprog, "%s/predict", bin);
    snprintf(plots_csv, sizeof plots_csv, "%s/plots.csv", raw);
    static char outs[MAX_VILLAGES][512];
    for (int v = 0; v < nv; v++) {
        snprintf(outs[v], sizeof outs[v], "%s/%s.csv", preds, villages[v]);
        char *av[] = {pprog, villages[v], plots_csv, s_out, w_out, (char *)model, outs[v], (char *)samples, NULL};
        char name[48];
        snprintf(name, sizeof name, "predict_%s", villages[v]);
        spawn(name, 2, av);
    }
    if (wait_stage(2)) {
        plog("Stage 2 FAILED - aborting workflow");
        write_stage_times(logs);
        return 1;
    }

    /* -------- Stage 3: advisory / report generation -------- */
    plog("\n[Stage 3] advisory and report generation");
    char aprog[512];
    snprintf(aprog, sizeof aprog, "%s/advise", bin);
    char *av3[4 + MAX_VILLAGES];
    int k = 0;
    av3[k++] = aprog;
    av3[k++] = preds;
    av3[k++] = (char *)out;
    for (int v = 0; v < nv; v++) av3[k++] = villages[v];
    av3[k] = NULL;
    spawn("advise_report", 3, av3);
    int f3 = wait_stage(3);

    write_stage_times(logs);
    double total = now_ms() - t_origin;
    double cpu = 0;
    for (int i = 0; i < ntasks; i++) cpu += tasks[i].cpu_ms;
    plog("\n%s: %d child processes, total wall %.2f ms, total child CPU %.2f ms",
         f3 ? "PIPELINE FAILED" : "PIPELINE COMPLETE", ntasks, total, cpu);
    plog("outputs: %s/advisories.csv, %s/advisory_report.txt, %s/pump_demand.csv; timings: %s/stage_times.csv",
         out, out, out, logs);
    if (log_fd >= 0) close(log_fd);
    return f3 ? 1 : 0;
}
