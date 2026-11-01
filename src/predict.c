/*
 * predict.c - Stage 2 of the advisory workflow (exec'd by pipeline, Exp 4).
 * One instance runs per village, in parallel with the others.
 *
 *   predict <village> <plots.csv> <sensor_features.csv> <weather_features.csv>
 *           <model.txt> <out.csv> [mc_samples]
 *
 * For every plot of the village:
 *   1. builds the engineered feature vector (same as training),
 *   2. ML inference: irrigation depth (ridge regression) and water-stress
 *      probability (logistic regression); rule baseline kept for comparison,
 *   3. Monte-Carlo ensemble (default 4000 draws) over forecast uncertainty
 *      (rain occurrence/amount, Tmax, sensor noise) -> P10 / P90 depth.
 *
 * Exit codes: 0 ok, 1 usage, 2 input missing, 3 village not found.
 * If the model file is missing the rule-based baseline is used (warning).
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Plot          plots[MAX_PLOTS];
static SensorFeature sens[MAX_PLOTS];

static double urand(unsigned *s) { return (rand_r(s) + 0.5) / ((double)RAND_MAX + 1.0); }

static double gauss(unsigned *s)
{
    double u1 = urand(s), u2 = urand(s);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int load_sensors(const char *path)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS];
    int n = 0;
    if (lr_open(&lr, path) < 0) return -1;
    lr_next(&lr, line, sizeof line);
    while (n < MAX_PLOTS && lr_next(&lr, line, sizeof line) >= 0) {
        if (split_csv(line, f, MAX_FIELDS) < 6) continue;
        SensorFeature *s = &sens[n++];
        snprintf(s->plot_id, sizeof s->plot_id, "%s", f[0]);
        s->sm_now = atof(f[1]);
        s->sm_avg24 = atof(f[2]);
        s->sm_trend24 = atof(f[3]);
        s->n_valid = atoi(f[4]);
        s->n_rejected = atoi(f[5]);
    }
    lr_close(&lr);
    return n;
}

static int load_weather(const char *path, const char *village, WeatherFeature *w)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS];
    int found = 0;
    if (lr_open(&lr, path) < 0) return -1;
    lr_next(&lr, line, sizeof line);
    while (lr_next(&lr, line, sizeof line) >= 0) {
        if (split_csv(line, f, MAX_FIELDS) < 7 || strcmp(f[0], village) != 0) continue;
        snprintf(w->village, NAME_LEN, "%s", f[0]);
        w->tmax = atof(f[1]);
        w->tmin = atof(f[2]);
        w->rh = atof(f[3]);
        w->et0 = atof(f[4]);
        w->rain3 = atof(f[5]);
        w->rain_prob = atof(f[6]);
        found = 1;
    }
    lr_close(&lr);
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: %s <village> <plots.csv> <sensor_features.csv> "
                        "<weather_features.csv> <model.txt> <out.csv> [mc_samples]\n", argv[0]);
        return 1;
    }
    const char *village = argv[1];
    int samples = argc > 7 ? atoi(argv[7]) : 4000;
    if (samples < 10) samples = 10;

    int np = load_plots(argv[2], plots, MAX_PLOTS);
    if (np < 0) { log_msg("predict", "ERROR: cannot open plots '%s': %s", argv[2], strerror(errno)); return 2; }
    int ns = load_sensors(argv[3]);
    if (ns < 0) { log_msg("predict", "ERROR: cannot open sensor features '%s': %s", argv[3], strerror(errno)); return 2; }
    WeatherFeature w;
    int wf = load_weather(argv[4], village, &w);
    if (wf < 0) { log_msg("predict", "ERROR: cannot open weather features '%s': %s", argv[4], strerror(errno)); return 2; }
    if (wf == 0) { log_msg("predict", "ERROR: no forecast for village '%s'", village); return 3; }

    Model m;
    if (model_load(&m, argv[5]) < 0)
        log_msg("predict", "WARNING: model '%s' unavailable - using rule-based baseline", argv[5]);

    int fd = fd_create(argv[6]);
    if (fd < 0) { log_msg("predict", "ERROR: cannot create '%s': %s", argv[6], strerror(errno)); return 2; }
    fd_printf(fd, "plot_id,farmer_id,village,soil_type,area_ha,crop_age_days,method,pump_kw,"
                  "sm_now,rel_water,depth_ml,depth_rule,depth_p10,depth_p90,stress_prob,"
                  "rain_3d_mm,rain_prob,tmax_c\n");

    double *draws = malloc(sizeof(double) * (size_t)samples);
    int done = 0, nodata = 0;
    double t0 = now_ms();

    for (int i = 0; i < np; i++) {
        Plot *p = &plots[i];
        if (strcmp(p->village, village) != 0) continue;

        SensorFeature *s = NULL;
        for (int k = 0; k < ns; k++)
            if (strcmp(sens[k].plot_id, p->plot_id) == 0) { s = &sens[k]; break; }

        if (!s || s->n_valid == 0) {      /* sensor offline: flag for field visit */
            fd_printf(fd, "%s,%s,%s,%s,%.2f,%d,%s,%.1f,NA,NA,-1,-1,-1,-1,-1,%.1f,%.2f,%.1f\n",
                      p->plot_id, p->farmer_id, p->village, p->soil_type, p->area_ha,
                      p->crop_age_days, p->method, p->pump_kw, w.rain3, w.rain_prob, w.tmax);
            nodata++;
            continue;
        }

        double fc, wp, x[N_FEATURES];
        soil_params(p->soil_type, &fc, &wp);
        compute_features(fc, wp, p->crop_age_days, s->sm_now, w.tmax, w.tmin, w.rain3, w.rain_prob, x);
        double depth = model_depth(&m, x);
        double rule = rule_depth(x);
        double stress = model_stress(&m, x);

        /* Monte-Carlo ensemble over forecast and sensor uncertainty */
        unsigned seed = 2026u;
        for (const char *c = p->plot_id; *c; c++) seed = seed * 31u + (unsigned)*c;
        double xs[N_FEATURES];
        for (int k = 0; k < samples; k++) {
            double sm = s->sm_now + 0.5 * gauss(&seed);
            double tmax = w.tmax + 1.2 * gauss(&seed);
            double rain = urand(&seed) < w.rain_prob ? w.rain3 * (0.5 + urand(&seed)) : 0.0;
            compute_features(fc, wp, p->crop_age_days, sm, tmax, w.tmin, rain, 1.0, xs);
            draws[k] = model_depth(&m, xs);
        }
        qsort(draws, (size_t)samples, sizeof(double), cmp_double);

        fd_printf(fd, "%s,%s,%s,%s,%.2f,%d,%s,%.1f,%.2f,%.3f,%.2f,%.2f,%.2f,%.2f,%.3f,%.1f,%.2f,%.1f\n",
                  p->plot_id, p->farmer_id, p->village, p->soil_type, p->area_ha, p->crop_age_days,
                  p->method, p->pump_kw, s->sm_now, x[5], depth, rule,
                  draws[samples / 10], draws[samples * 9 / 10], stress, w.rain3, w.rain_prob, w.tmax);
        done++;
    }
    free(draws);
    close(fd);

    log_msg("predict", "%s: %d plots predicted, %d sensor-offline, %d MC draws/plot, %.1f ms (%s model)",
            village, done, nodata, samples, now_ms() - t0, m.loaded ? "ML" : "rule");
    return 0;
}
