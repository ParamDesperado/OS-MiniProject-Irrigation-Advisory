/*
 * common.c - shared helpers: soil/crop agronomy, model inference and
 * line-oriented file I/O built directly on open/read/write/close.
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* ---------------- agronomy ---------------- */

void soil_params(const char *soil, double *fc, double *wp)
{
    if (strcmp(soil, "black") == 0)      { *fc = 40.0; *wp = 20.0; }
    else if (strcmp(soil, "red") == 0)   { *fc = 25.0; *wp = 12.0; }
    else                                 { *fc = 32.0; *wp = 15.0; }   /* loam */
}

double kc_for_age(int age)
{
    if (age <= 45)  return 0.40;   /* germination   */
    if (age <= 120) return 0.75;   /* tillering     */
    if (age <= 270) return 1.25;   /* grand growth  */
    return 0.75;                   /* maturity      */
}

double root_depth_mm(int age)
{
    double f = age / 150.0;
    return 300.0 + 600.0 * (f > 1.0 ? 1.0 : f);
}

double hargreaves_et0(double tmax, double tmin)
{
    double d = tmax - tmin;
    if (d < 0.1) d = 0.1;
    return 0.0023 * ((tmax + tmin) / 2.0 + 17.8) * sqrt(d) * 15.0;
}

/* ---------------- AI component ---------------- */

/* Must stay identical to features() in scripts/train_model.py */
void compute_features(double fc, double wp, int age, double sm, double tmax,
                      double tmin, double rain3, double rprob, double x[N_FEATURES])
{
    double kc   = kc_for_age(age);
    double root = root_depth_mm(age);
    double et0  = hargreaves_et0(tmax, tmin);
    double depl = (fc - sm) / 100.0 * root;
    if (depl < 0) depl = 0;

    x[0] = depl;
    x[1] = kc * et0 * 3.0;
    x[2] = rain3 * rprob;
    x[3] = (tmax > 36.0 ? tmax - 36.0 : 0.0) * kc;
    x[4] = (fc - wp) / 100.0 * root;
    x[5] = (sm - wp) / (fc - wp);
    x[6] = kc;
    x[7] = tmax;
    x[8] = rain3;
    x[9] = fc >= 35.0 ? depl : 0.0;
}

int model_load(Model *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[2048];
    int got = 0;
    while (fgets(line, sizeof line, f)) {
        char *tok = strtok(line, " \t\n");
        if (!tok || tok[0] == '#') continue;
        double *dst = NULL;
        int count = N_FEATURES;
        if (strcmp(tok, "features") == 0) { m->n = atoi(strtok(NULL, " \t\n")); continue; }
        else if (strcmp(tok, "mean") == 0)   dst = m->mean;
        else if (strcmp(tok, "std") == 0)    dst = m->std;
        else if (strcmp(tok, "depth") == 0)  { dst = m->depth_w;  count = N_FEATURES + 1; }
        else if (strcmp(tok, "stress") == 0) { dst = m->stress_w; count = N_FEATURES + 1; }
        if (!dst) continue;
        for (int i = 0; i < count; i++) {
            char *v = strtok(NULL, " \t\n");
            if (!v) { fclose(f); return -1; }
            dst[i] = atof(v);
        }
        got++;
    }
    fclose(f);
    if (m->n != N_FEATURES || got != 4) return -1;
    m->loaded = 1;
    return 0;
}

static double linear(const Model *m, const double *w, const double x[N_FEATURES])
{
    double s = w[0];
    for (int i = 0; i < N_FEATURES; i++)
        s += w[i + 1] * (x[i] - m->mean[i]) / (m->std[i] != 0 ? m->std[i] : 1.0);
    return s;
}

double rule_depth(const double x[N_FEATURES])
{
    double d = x[0] + x[1] - x[8];         /* depletion + 3-day ETc - forecast rain */
    return d > 0 ? d : 0;
}

double model_depth(const Model *m, const double x[N_FEATURES])
{
    if (!m->loaded) return rule_depth(x);
    double d = linear(m, m->depth_w, x);
    return d > 0 ? d : 0;
}

double model_stress(const Model *m, const double x[N_FEATURES])
{
    if (!m->loaded) return x[5] < 0.5 ? 0.9 : 0.1;
    double z = linear(m, m->stress_w, x);
    if (z > 30) z = 30;
    if (z < -30) z = -30;
    return 1.0 / (1.0 + exp(-z));
}

/* ---------------- system-call file I/O ---------------- */

int lr_open(LineReader *lr, const char *path)
{
    lr->len = lr->pos = 0;
    lr->fd = open(path, O_RDONLY);
    return lr->fd < 0 ? -1 : 0;
}

/* Returns line length (>=0), or -1 at EOF. Lines longer than cap are truncated. */
int lr_next(LineReader *lr, char *line, size_t cap)
{
    size_t n = 0;
    int any = 0;
    for (;;) {
        if (lr->pos == lr->len) {
            ssize_t r;
            do { r = read(lr->fd, lr->buf, sizeof lr->buf); } while (r < 0 && errno == EINTR);
            if (r <= 0) break;
            lr->len = (size_t)r;
            lr->pos = 0;
        }
        char c = lr->buf[lr->pos++];
        any = 1;
        if (c == '\n') break;
        if (c != '\r' && n + 1 < cap) line[n++] = c;
    }
    line[n] = '\0';
    return any ? (int)n : -1;
}

void lr_close(LineReader *lr)
{
    if (lr->fd >= 0) close(lr->fd);
    lr->fd = -1;
}

int fd_create(const char *path)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
}

ssize_t fd_printf(int fd, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof buf) n = sizeof buf - 1;

    ssize_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, (size_t)(n - done));
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        done += w;
    }
    return done;
}

int split_csv(char *line, char **fields, int max)
{
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (*p && n < max) {
        if (*p == ',') { *p = '\0'; fields[n++] = p + 1; }
        p++;
    }
    return n;
}

int load_plots(const char *path, Plot *plots, int max)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS];
    int n = 0;

    if (lr_open(&lr, path) < 0) return -1;
    lr_next(&lr, line, sizeof line);                    /* header */
    while (n < max && lr_next(&lr, line, sizeof line) >= 0) {
        if (split_csv(line, f, MAX_FIELDS) < 8) continue;
        Plot *p = &plots[n++];
        snprintf(p->plot_id, sizeof p->plot_id, "%s", f[0]);
        snprintf(p->farmer_id, sizeof p->farmer_id, "%s", f[1]);
        snprintf(p->village, sizeof p->village, "%s", f[2]);
        snprintf(p->soil_type, sizeof p->soil_type, "%s", f[3]);
        p->area_ha = atof(f[4]);
        p->crop_age_days = atoi(f[5]);
        snprintf(p->method, sizeof p->method, "%s", f[6]);
        p->pump_kw = atof(f[7]);
    }
    lr_close(&lr);
    return n;
}

double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

void log_msg(const char *who, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[%-9s pid=%d] %s\n", who, (int)getpid(), buf);
}
