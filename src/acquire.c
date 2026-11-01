/*
 * acquire.c - Stage 1 of the advisory workflow (exec'd by pipeline, Exp 4).
 *
 *   acquire sensors <sensor_readings.csv> <out sensor_features.csv>
 *   acquire weather <weather_forecast.csv> <out weather_features.csv>
 *
 * Reads raw data with open/read/close, validates every record (missing,
 * out-of-range and malformed rows are rejected and counted), aggregates
 * features and writes them with write().
 *
 * Exit codes: 0 ok, 1 usage, 2 input file missing/unreadable,
 *             3 input present but no valid records (corrupt file),
 *             4 cannot create output.
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WINDOW 12           /* 12 readings x 2 h = last 24 h */

typedef struct {
    int    used;
    char   id[12];
    int    hours[WINDOW];
    double sm[WINDOW];
    int    count;           /* readings stored in ring (<= WINDOW) */
    int    head;
    int    valid, rejected;
} PlotAgg;

static PlotAgg agg[MAX_PLOTS];

static int plot_index(const char *id)
{
    if (id[0] != 'P') return -1;
    char *end;
    long n = strtol(id + 1, &end, 10);
    if (*end != '\0' || n <= 0 || n >= MAX_PLOTS) return -1;
    return (int)n;
}

static int parse_double(const char *s, double *out)
{
    if (!s || !*s) return -1;
    char *end;
    errno = 0;
    *out = strtod(s, &end);
    return (errno || *end) ? -1 : 0;
}

static int do_sensors(const char *in, const char *out)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS];
    long total = 0, ok = 0, missing = 0, range = 0, malformed = 0;

    if (lr_open(&lr, in) < 0) {
        log_msg("acquire", "ERROR: cannot open sensor file '%s': %s", in, strerror(errno));
        return 2;
    }
    lr_next(&lr, line, sizeof line);                         /* header */
    while (lr_next(&lr, line, sizeof line) >= 0) {
        if (line[0] == '\0') continue;
        total++;
        int nf = split_csv(line, f, MAX_FIELDS);
        int idx = nf >= 3 ? plot_index(f[2]) : -1;
        if (nf != 5 || idx < 0) { malformed++; continue; }

        PlotAgg *a = &agg[idx];
        if (!a->used) { a->used = 1; snprintf(a->id, sizeof a->id, "%s", f[2]); }

        double sm;
        if (f[3][0] == '\0')             { missing++; a->rejected++; continue; }
        if (parse_double(f[3], &sm) < 0) { malformed++; a->rejected++; continue; }
        if (sm <= 0.0 || sm > 60.0)      { range++; a->rejected++; continue; }

        a->hours[a->head] = atoi(f[1]);
        a->sm[a->head] = sm;
        a->head = (a->head + 1) % WINDOW;
        if (a->count < WINDOW) a->count++;
        a->valid++;
        ok++;
    }
    lr_close(&lr);

    if (ok == 0) {
        log_msg("acquire", "ERROR: '%s' contains no valid sensor records (corrupt?)", in);
        return 3;
    }

    int fd = fd_create(out);
    if (fd < 0) { log_msg("acquire", "ERROR: cannot create '%s': %s", out, strerror(errno)); return 4; }
    fd_printf(fd, "plot_id,sm_now,sm_avg24,sm_trend24,n_valid,n_rejected\n");

    int plots = 0;
    for (int i = 0; i < MAX_PLOTS; i++) {
        PlotAgg *a = &agg[i];
        if (!a->used || a->count == 0) continue;
        int newest = (a->head - 1 + WINDOW) % WINDOW;
        int oldest = a->count < WINDOW ? 0 : a->head;
        double sum = 0;
        for (int k = 0; k < a->count; k++) sum += a->sm[k];
        fd_printf(fd, "%s,%.2f,%.2f,%.2f,%d,%d\n", a->id, a->sm[newest], sum / a->count,
                  a->sm[newest] - a->sm[oldest], a->valid, a->rejected);
        plots++;
    }
    close(fd);

    log_msg("acquire", "sensors: %ld rows read, %ld valid, rejected: %ld missing, %ld out-of-range, %ld malformed -> %d plots",
            total, ok, missing, range, malformed, plots);
    return 0;
}

typedef struct {
    char   village[NAME_LEN];
    int    days;
    double tmax, tmin, rh, et0, rain, prob;
} VillAgg;

static int do_weather(const char *in, const char *out)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS];
    VillAgg v[MAX_VILLAGES];
    int nv = 0;
    long bad = 0;

    if (lr_open(&lr, in) < 0) {
        log_msg("acquire", "ERROR: cannot open weather file '%s': %s", in, strerror(errno));
        return 2;
    }
    lr_next(&lr, line, sizeof line);
    while (lr_next(&lr, line, sizeof line) >= 0) {
        if (line[0] == '\0') continue;
        double tmax, tmin, rh, rain, prob;
        if (split_csv(line, f, MAX_FIELDS) != 9 ||
            parse_double(f[3], &tmax) || parse_double(f[4], &tmin) || parse_double(f[5], &rh) ||
            parse_double(f[7], &rain) || parse_double(f[8], &prob) ||
            tmax < -10 || tmax > 55 || tmin > tmax || prob < 0 || prob > 1 || rain < 0) {
            bad++;
            continue;
        }
        int i;
        for (i = 0; i < nv && strcmp(v[i].village, f[0]) != 0; i++)
            ;
        if (i == nv) {
            if (nv == MAX_VILLAGES) continue;
            memset(&v[nv], 0, sizeof v[nv]);
            snprintf(v[nv].village, NAME_LEN, "%s", f[0]);
            nv++;
        }
        v[i].days++;
        v[i].tmax += tmax;
        v[i].tmin += tmin;
        v[i].rh += rh;
        v[i].et0 += hargreaves_et0(tmax, tmin);
        v[i].rain += rain;
        if (prob > v[i].prob) v[i].prob = prob;
    }
    lr_close(&lr);

    if (nv == 0) {
        log_msg("acquire", "ERROR: '%s' contains no valid forecast rows", in);
        return 3;
    }

    int fd = fd_create(out);
    if (fd < 0) { log_msg("acquire", "ERROR: cannot create '%s': %s", out, strerror(errno)); return 4; }
    fd_printf(fd, "village,tmax_c,tmin_c,rh_pct,et0_mm,rain_3d_mm,rain_prob\n");
    for (int i = 0; i < nv; i++) {
        double d = v[i].days;
        fd_printf(fd, "%s,%.2f,%.2f,%.1f,%.3f,%.1f,%.2f\n", v[i].village, v[i].tmax / d,
                  v[i].tmin / d, v[i].rh / d, v[i].et0 / d, v[i].rain, v[i].prob);
    }
    close(fd);
    log_msg("acquire", "weather: %d villages aggregated, %ld invalid rows rejected", nv, bad);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s sensors|weather <input.csv> <output.csv>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "sensors") == 0) return do_sensors(argv[2], argv[3]);
    if (strcmp(argv[1], "weather") == 0) return do_weather(argv[2], argv[3]);
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    return 1;
}
