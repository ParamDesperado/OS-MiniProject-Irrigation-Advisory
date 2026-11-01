/*
 * advise.c - Stage 3 of the advisory workflow (exec'd by pipeline, Exp 4).
 *
 *   advise <predictions_dir> <out_dir> <village> [<village> ...]
 *
 * Merges the per-village prediction files and turns them into plot-wise,
 * farmer-friendly advisories (English + Hindi templates), e.g.
 *   "Irrigate tomorrow morning for 2.5 hours"
 *   "Rainfall is expected; irrigation is not required"
 *
 * Writes (all with write()):
 *   <out_dir>/advisories.csv       one row per plot
 *   <out_dir>/advisory_report.txt  human-readable summary for supervisors
 *   <out_dir>/pump_demand.csv      per-village pump / power / valve demand
 *                                  for the next 48 h  -> input to Exp 9 (Banker's)
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NO_IRRIGATION_MM 5.0
#define FLOW_PER_KW      10.0    /* m3/h delivered per kW of pump (canal lift / borewell) */
#define MAX_FURROW_MM    75.0    /* max net depth applied in one furrow irrigation */
#define MAX_DRIP_MM      40.0    /* max net depth applied in one drip cycle */
#define POWER_HOURS      8.0     /* 3-phase power supply hours per day for agriculture */
#define PLOTS_PER_PUMP   12      /* plots one shared pump set can serve in 48 h */
#define KW_PER_BLOCK     15.0    /* one 3-phase feeder power slot */
#define PLOTS_PER_VALVE  20

typedef struct {
    char   plot_id[12], farmer_id[12], village[NAME_LEN], method[12];
    double area, pump_kw, depth, rule, p10, p90, stress, rain3, rprob;
    int    priority;             /* 1 today, 2 tomorrow, 3 within 3 days, 4 none, 0 offline */
    double gross, volume, hours, need_m3;   /* need_m3: full 3-day requirement */
    char   action[48];
    char   msg_en[200], msg_hi[320];
} Advice;

static Advice adv[MAX_PLOTS];

static int cmp_priority(const void *a, const void *b)
{
    const Advice *x = a, *y = b;
    int px = x->priority == 0 ? 9 : x->priority, py = y->priority == 0 ? 9 : y->priority;
    if (px != py) return px - py;
    return (y->stress > x->stress) - (y->stress < x->stress);
}

static void build_advice(Advice *a)
{
    if (a->depth < 0) {
        a->priority = 0;
        strcpy(a->action, "SENSOR_OFFLINE");
        snprintf(a->msg_en, sizeof a->msg_en, "Sensor data unavailable for plot %s. Field visit required.", a->plot_id);
        snprintf(a->msg_hi, sizeof a->msg_hi, "सेंसर डेटा उपलब्ध नहीं है। खेत निरीक्षण आवश्यक है।");
        return;
    }
    int drip = strcmp(a->method, "drip") == 0;
    double eff = drip ? 0.90 : 0.60;
    double cap = drip ? MAX_DRIP_MM : MAX_FURROW_MM;
    double apply = a->depth > cap ? cap : a->depth;        /* one irrigation event */
    a->gross = apply / eff;
    a->volume = a->gross * a->area * 10.0;                 /* mm x ha x 10 = m3 */
    a->hours = a->volume / (a->pump_kw * FLOW_PER_KW);
    double days = a->hours / POWER_HOURS;
    a->need_m3 = a->depth < NO_IRRIGATION_MM ? 0 : a->depth / eff * a->area * 10.0;

    if (a->depth < NO_IRRIGATION_MM) {
        a->priority = 4;
        a->volume = a->hours = 0;
        if (a->rprob >= 0.5 && a->rain3 >= 5.0) {
            strcpy(a->action, "SKIP_RAIN_EXPECTED");
            snprintf(a->msg_en, sizeof a->msg_en, "Rainfall of about %.0f mm is expected; irrigation is not required.", a->rain3);
            snprintf(a->msg_hi, sizeof a->msg_hi, "लगभग %.0f मिमी बारिश की संभावना है; सिंचाई की आवश्यकता नहीं है।", a->rain3);
        } else {
            strcpy(a->action, "NO_IRRIGATION");
            snprintf(a->msg_en, sizeof a->msg_en, "Soil moisture is adequate. No irrigation needed for the next 3 days.");
            snprintf(a->msg_hi, sizeof a->msg_hi, "मिट्टी में पर्याप्त नमी है। अगले 3 दिन सिंचाई की आवश्यकता नहीं है।");
        }
        return;
    }
    if (a->stress >= 0.7) {
        a->priority = 1;
        strcpy(a->action, "IRRIGATE_TODAY");
        snprintf(a->msg_en, sizeof a->msg_en, "High water-stress risk. Start irrigation today: run pump %.1f h "
                 "(%.1f days of power supply), %.0f mm.", a->hours, days, a->gross);
        snprintf(a->msg_hi, sizeof a->msg_hi, "पानी की कमी का खतरा अधिक है। आज सिंचाई शुरू करें: पंप %.1f घंटे चलाएँ।", a->hours);
    } else if (a->stress >= 0.4) {
        a->priority = 2;
        strcpy(a->action, "IRRIGATE_TOMORROW");
        snprintf(a->msg_en, sizeof a->msg_en, "Irrigate tomorrow morning: run pump %.1f h (%.1f days of power supply), %.0f mm.",
                 a->hours, days, a->gross);
        snprintf(a->msg_hi, sizeof a->msg_hi, "कल सुबह सिंचाई करें: पंप %.1f घंटे चलाएँ।", a->hours);
    } else {
        a->priority = 3;
        strcpy(a->action, "IRRIGATE_WITHIN_3_DAYS");
        snprintf(a->msg_en, sizeof a->msg_en, "Irrigate within the next 3 days: run pump %.1f h, %.0f mm.", a->hours, a->gross);
        snprintf(a->msg_hi, sizeof a->msg_hi, "अगले 3 दिनों में सिंचाई करें: पंप %.1f घंटे चलाएँ।", a->hours);
    }
}

static int load_predictions(const char *path, int start)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS + 4];
    int n = start;
    if (lr_open(&lr, path) < 0) return -1;
    lr_next(&lr, line, sizeof line);
    while (n < MAX_PLOTS && lr_next(&lr, line, sizeof line) >= 0) {
        if (split_csv(line, f, MAX_FIELDS + 4) < 18) continue;
        Advice *a = &adv[n++];
        memset(a, 0, sizeof *a);
        snprintf(a->plot_id, sizeof a->plot_id, "%s", f[0]);
        snprintf(a->farmer_id, sizeof a->farmer_id, "%s", f[1]);
        snprintf(a->village, sizeof a->village, "%s", f[2]);
        a->area = atof(f[4]);
        snprintf(a->method, sizeof a->method, "%s", f[6]);
        a->pump_kw = atof(f[7]);
        a->depth = atof(f[10]);
        a->rule = atof(f[11]);
        a->p10 = atof(f[12]);
        a->p90 = atof(f[13]);
        a->stress = atof(f[14]);
        a->rain3 = atof(f[15]);
        a->rprob = atof(f[16]);
        build_advice(a);
    }
    lr_close(&lr);
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <predictions_dir> <out_dir> <village> [<village> ...]\n", argv[0]);
        return 1;
    }
    char path[512];
    int n = 0;
    for (int i = 3; i < argc; i++) {
        snprintf(path, sizeof path, "%s/%s.csv", argv[1], argv[i]);
        int r = load_predictions(path, n);
        if (r < 0) { log_msg("advise", "ERROR: missing predictions '%s': %s", path, strerror(errno)); return 2; }
        n = r;
    }
    if (n == 0) { log_msg("advise", "ERROR: no predictions to advise on"); return 3; }

    /* ---- advisories.csv ---- */
    snprintf(path, sizeof path, "%s/advisories.csv", argv[2]);
    int fd = fd_create(path);
    if (fd < 0) { log_msg("advise", "ERROR: cannot create '%s': %s", path, strerror(errno)); return 4; }
    fd_printf(fd, "plot_id,farmer_id,village,priority,action,net_depth_mm,gross_depth_mm,volume_m3,"
                  "pump_hours,stress_prob,depth_p10,depth_p90,message_en,message_hi\n");
    for (int i = 0; i < n; i++) {
        Advice *a = &adv[i];
        fd_printf(fd, "%s,%s,%s,%d,%s,%.1f,%.1f,%.1f,%.2f,%.3f,%.1f,%.1f,\"%s\",\"%s\"\n",
                  a->plot_id, a->farmer_id, a->village, a->priority, a->action, a->depth, a->gross,
                  a->volume, a->hours, a->stress, a->p10, a->p90, a->msg_en, a->msg_hi);
    }
    close(fd);

    /* ---- per-village summary + pump_demand.csv (input to Exp 9) ---- */
    snprintf(path, sizeof path, "%s/pump_demand.csv", argv[2]);
    int fdd = fd_create(path);
    if (fdd < 0) { log_msg("advise", "ERROR: cannot create '%s'", path); return 4; }
    fd_printf(fdd, "village,plots_today,plots_tomorrow,plots_48h,pumps,power_blocks,valves,fertigation_units,water_m3\n");

    snprintf(path, sizeof path, "%s/advisory_report.txt", argv[2]);
    int fr = fd_create(path);
    if (fr < 0) { log_msg("advise", "ERROR: cannot create '%s'", path); return 4; }
    fd_printf(fr, "=====================================================================================\n");
    fd_printf(fr, " SUGARCANE IRRIGATION ADVISORY REPORT  (KJS-AGR-01 prototype, SYNTHETIC DATA)\n");
    fd_printf(fr, " Decision support only - final irrigation decision rests with farmer / field officer\n");
    fd_printf(fr, "=====================================================================================\n\n");
    fd_printf(fr, "%-11s %5s %6s %6s %6s %6s %7s %10s %10s\n", "Village", "Plots", "Today", "Tmrw",
              "3-day", "None", "Offline", "AI(m3)", "Rule(m3)");

    double tot_ml = 0, tot_rule = 0;
    int cnt[5] = {0};
    for (int i = 3; i < argc; i++) {
        int c[5] = {0}, drip48 = 0, plots = 0;
        double water = 0, rulew = 0, kw48 = 0;
        for (int k = 0; k < n; k++) {
            Advice *a = &adv[k];
            if (strcmp(a->village, argv[i]) != 0) continue;
            plots++;
            c[a->priority]++;
            water += a->need_m3;
            if (a->priority != 0) {
                double eff = strcmp(a->method, "drip") == 0 ? 0.90 : 0.60;
                double r = a->rule < NO_IRRIGATION_MM ? 0 : a->rule;
                rulew += r / eff * a->area * 10.0;
            }
            if (a->priority == 1 || a->priority == 2) {
                kw48 += a->pump_kw;
                if (strcmp(a->method, "drip") == 0) drip48++;
            }
        }
        int n48 = c[1] + c[2];
        int pumps = (n48 + PLOTS_PER_PUMP - 1) / PLOTS_PER_PUMP;
        double avg_kw = n48 ? kw48 / n48 : 0;
        int power = (int)ceil(pumps * avg_kw / KW_PER_BLOCK);
        int valves = (n48 + PLOTS_PER_VALVE - 1) / PLOTS_PER_VALVE;
        int fert = (drip48 + 9) / 10;
        fd_printf(fdd, "%s,%d,%d,%d,%d,%d,%d,%d,%.0f\n", argv[i], c[1], c[2], n48, pumps, power, valves, fert, water);
        fd_printf(fr, "%-11s %5d %6d %6d %6d %6d %7d %10.0f %10.0f\n", argv[i], plots, c[1], c[2], c[3],
                  c[4], c[0], water, rulew);
        for (int k = 0; k < 5; k++) cnt[k] += c[k];
        tot_ml += water;
        tot_rule += rulew;
    }
    close(fdd);

    fd_printf(fr, "%-11s %5d %6d %6d %6d %6d %7d %10.0f %10.0f\n\n", "TOTAL", n, cnt[1], cnt[2], cnt[3],
              cnt[4], cnt[0], tot_ml, tot_rule);
    if (tot_rule > 0)
        fd_printf(fr, "3-day water requirement, AI advisory vs rule-based schedule: %.0f m3 vs %.0f m3 (%.1f%% less)\n\n",
                  tot_ml, tot_rule, 100.0 * (tot_rule - tot_ml) / tot_rule);

    qsort(adv, (size_t)n, sizeof(Advice), cmp_priority);
    fd_printf(fr, "Top 15 most urgent plots\n");
    fd_printf(fr, "%-6s %-11s %-7s %6s %9s %9s %8s %-22s\n", "Plot", "Village", "Method", "Stress", "NetNeed",
              "GrossApp", "PumpHrs", "Action");
    for (int i = 0; i < n && i < 15; i++)
        fd_printf(fr, "%-6s %-11s %-7s %6.2f %6.1f mm %6.1f mm %8.1f %-22s\n", adv[i].plot_id, adv[i].village,
                  adv[i].method, adv[i].stress, adv[i].depth, adv[i].gross, adv[i].hours, adv[i].action);

    fd_printf(fr, "\nSample farmer messages (one per action type)\n");
    const char *seen[8];
    int ns = 0;
    for (int i = 0; i < n; i++) {
        int dup = 0;
        for (int k = 0; k < ns; k++) if (strcmp(seen[k], adv[i].action) == 0) dup = 1;
        if (dup || ns == 8) continue;
        seen[ns++] = adv[i].action;
        fd_printf(fr, "  [%s] %s (%s)\n    EN: %s\n    HI: %s\n", adv[i].action, adv[i].plot_id,
                  adv[i].village, adv[i].msg_en, adv[i].msg_hi);
    }
    close(fr);

    log_msg("advise", "%d advisories: %d today, %d tomorrow, %d within 3 days, %d none, %d sensor-offline",
            n, cnt[1], cnt[2], cnt[3], cnt[4], cnt[0]);
    return 0;
}
