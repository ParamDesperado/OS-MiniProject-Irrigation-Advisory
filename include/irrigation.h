/*
 * irrigation.h - shared definitions for the Sugarcane Irrigation Advisory
 * prototype (Use case KJS-AGR-01).
 *
 * Used by every OS experiment program:
 *   Exp 4  pipeline / acquire / predict / advise  (fork, exec, wait, open/read/write/close)
 *   Exp 6  scheduler                               (SJF + RR on measured bursts)
 *   Exp 7  sensor_sync                             (producer-consumer, semaphores + mutex)
 *   Exp 9  banker                                  (Banker's algorithm on pump/power resources)
 */
#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <stddef.h>
#include <sys/types.h>

#define MAX_LINE      512
#define MAX_FIELDS    16
#define MAX_PLOTS     4096
#define MAX_VILLAGES  16
#define NAME_LEN      32
#define N_FEATURES    10

/* ---------- domain records ---------- */
typedef struct {
    char   plot_id[12];
    char   farmer_id[12];
    char   village[NAME_LEN];
    char   soil_type[12];
    double area_ha;
    int    crop_age_days;
    char   method[12];          /* drip | furrow */
    double pump_kw;
} Plot;

typedef struct {
    char   plot_id[12];
    double sm_now;              /* latest valid soil moisture (vol %) */
    double sm_avg24;            /* mean over last 24 h */
    double sm_trend24;          /* change over last 24 h (%-points) */
    int    n_valid;
    int    n_rejected;
} SensorFeature;

typedef struct {
    char   village[NAME_LEN];
    double tmax, tmin, rh;
    double et0;                 /* mean Hargreaves ET0, mm/day */
    double rain3;               /* total forecast rain, 3 days (mm) */
    double rain_prob;           /* max rain probability over 3 days */
} WeatherFeature;

typedef struct {
    int    n;
    double mean[N_FEATURES], std[N_FEATURES];
    double depth_w[N_FEATURES + 1];   /* [0] = bias */
    double stress_w[N_FEATURES + 1];
    int    loaded;              /* 0 => rule-based fallback */
} Model;

/* ---------- soil / crop helpers ---------- */
void   soil_params(const char *soil, double *fc, double *wp);
double kc_for_age(int age);
double root_depth_mm(int age);
double hargreaves_et0(double tmax, double tmin);

/* ---------- AI component ---------- */
void   compute_features(double fc, double wp, int age, double sm, double tmax,
                        double tmin, double rain3, double rprob, double x[N_FEATURES]);
int    model_load(Model *m, const char *path);           /* 0 ok, -1 => fallback */
double model_depth(const Model *m, const double x[N_FEATURES]);
double model_stress(const Model *m, const double x[N_FEATURES]);
double rule_depth(const double x[N_FEATURES]);

/* ---------- low-level file I/O on raw system calls (Exp 4) ---------- */
typedef struct {
    int    fd;
    char   buf[8192];
    size_t len, pos;
} LineReader;

int     lr_open(LineReader *lr, const char *path);       /* open(2)          */
int     lr_next(LineReader *lr, char *line, size_t cap); /* read(2) by lines */
void    lr_close(LineReader *lr);                        /* close(2)         */
int     fd_create(const char *path);                     /* open O_CREAT|O_TRUNC, 0640 */
ssize_t fd_printf(int fd, const char *fmt, ...);         /* formatted write(2) */

int     split_csv(char *line, char **fields, int max);
int     load_plots(const char *path, Plot *plots, int max);
double  now_ms(void);
void    log_msg(const char *who, const char *fmt, ...);

#endif
