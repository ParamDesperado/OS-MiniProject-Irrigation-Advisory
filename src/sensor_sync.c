/*
 * sensor_sync.c - EXPERIMENT 7: Producer-Consumer with semaphores + mutex.
 *
 * Producers : one thread per village gateway, streaming soil-moisture
 *             readings from data/raw/sensor_readings.csv
 * Buffer    : bounded circular buffer (default 8 slots) of sensor readings
 * Consumers : analytics worker threads; each validates a reading and updates
 *             the SHARED plot-water table (per-plot count / moisture sum and
 *             global totals) used for the advisory.
 *
 * Synchronisation
 *   sem empty  - free slots   (producer waits when buffer is FULL)
 *   sem full   - filled slots (consumer waits when buffer is EMPTY)
 *   buf_mutex  - protects buffer indices
 *   table_mutex- protects the shared plot-water table
 *
 * --unsafe removes table_mutex: concurrent read-modify-write updates are
 * lost, which the final verification detects (race condition evidence).
 *
 * usage: sensor_sync [-p producers] [-c consumers] [-b buffer] [-f sensor.csv] [--unsafe]
 */
#define _GNU_SOURCE
#include "irrigation.h"

#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int plot; double sm; int producer; } Reading;

/* ---- bounded buffer ---- */
static Reading *buf;
static int      bsize, in_idx, out_idx;
static sem_t    empty, full;
static pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---- shared plot-water table ---- */
static long   plot_count[MAX_PLOTS];
static double plot_sum[MAX_PLOTS];
static long   total_valid, total_rejected;
static pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;
static int    unsafe_mode;

/* ---- statistics (atomic counters) ---- */
static long produced, consumed, waits_full, waits_empty;

/* readings loaded from disk, partitioned among producers */
static Reading *all;
static long     nall;
static int      nprod, ncons;

static void *producer(void *arg)
{
    int id = (int)(long)arg;
    for (long i = id; i < nall; i += nprod) {
        Reading r = all[i];
        r.producer = id;
        if (sem_trywait(&empty) != 0) {               /* buffer FULL -> block */
            __sync_fetch_and_add(&waits_full, 1);
            sem_wait(&empty);
        }
        pthread_mutex_lock(&buf_mutex);
        buf[in_idx] = r;
        in_idx = (in_idx + 1) % bsize;
        pthread_mutex_unlock(&buf_mutex);
        sem_post(&full);
        __sync_fetch_and_add(&produced, 1);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    for (;;) {
        if (sem_trywait(&full) != 0) {                /* buffer EMPTY -> block */
            __sync_fetch_and_add(&waits_empty, 1);
            sem_wait(&full);
        }
        pthread_mutex_lock(&buf_mutex);
        Reading r = buf[out_idx];
        out_idx = (out_idx + 1) % bsize;
        pthread_mutex_unlock(&buf_mutex);
        sem_post(&empty);

        if (r.plot < 0) break;                        /* poison pill: shutdown */
        __sync_fetch_and_add(&consumed, 1);

        int ok = r.sm > 0.0 && r.sm <= 60.0;
        if (!unsafe_mode) pthread_mutex_lock(&table_mutex);
        if (ok) {
            long c = plot_count[r.plot];              /* read-modify-write */
            double s = plot_sum[r.plot];
            long t = total_valid;
            if (unsafe_mode) sched_yield();           /* widen the race window */
            plot_count[r.plot] = c + 1;
            plot_sum[r.plot] = s + r.sm;
            total_valid = t + 1;
        } else {
            long t = total_rejected;
            if (unsafe_mode) sched_yield();
            total_rejected = t + 1;
        }
        if (!unsafe_mode) pthread_mutex_unlock(&table_mutex);
    }
    return NULL;
}

static long load_readings(const char *path)
{
    LineReader lr;
    char line[MAX_LINE];
    char *f[MAX_FIELDS];
    long cap = 1 << 17, n = 0;
    if (lr_open(&lr, path) < 0) return -1;
    all = malloc(sizeof(Reading) * (size_t)cap);
    lr_next(&lr, line, sizeof line);
    while (lr_next(&lr, line, sizeof line) >= 0) {
        if (split_csv(line, f, MAX_FIELDS) != 5 || f[2][0] != 'P') continue;   /* malformed */
        int id = atoi(f[2] + 1);
        if (id <= 0 || id >= MAX_PLOTS) continue;
        if (n == cap) { cap *= 2; all = realloc(all, sizeof(Reading) * (size_t)cap); }
        all[n].plot = id;
        all[n].sm = f[3][0] ? atof(f[3]) : -1.0;  /* missing value -> rejected by consumer */
        n++;
    }
    lr_close(&lr);
    return n;
}

int main(int argc, char **argv)
{
    const char *file = "data/raw/sensor_readings.csv";
    nprod = 5; ncons = 3; bsize = 8;
    static struct option lo[] = {{"unsafe", no_argument, 0, 'u'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, "p:c:b:f:uh", lo, NULL)) != -1) {
        switch (opt) {
        case 'p': nprod = atoi(optarg); break;
        case 'c': ncons = atoi(optarg); break;
        case 'b': bsize = atoi(optarg); break;
        case 'f': file = optarg; break;
        case 'u': unsafe_mode = 1; break;
        default:
            fprintf(stderr, "usage: %s [-p producers] [-c consumers] [-b buffer_size] [-f sensor.csv] [--unsafe]\n", argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    if (nprod < 1 || ncons < 1 || bsize < 1 || nprod > 64 || ncons > 64) {
        fprintf(stderr, "ERROR: producers/consumers must be 1..64 and buffer size >= 1\n");
        return 1;
    }

    nall = load_readings(file);
    if (nall < 0) { fprintf(stderr, "ERROR: cannot open sensor file '%s'\n", file); return 2; }
    if (nall == 0) { fprintf(stderr, "ERROR: no readings in '%s'\n", file); return 3; }

    /* sequential reference result (ground truth for verification) */
    long ref_valid = 0, ref_rej = 0;
    double ref_sum = 0;
    for (long i = 0; i < nall; i++) {
        if (all[i].sm > 0.0 && all[i].sm <= 60.0) { ref_valid++; ref_sum += all[i].sm; }
        else ref_rej++;
    }

    printf("EXPERIMENT 7 - Producer-Consumer on the sensor buffer (KJS-AGR-01)\n");
    printf("mode=%s  producers=%d (village gateways)  consumers=%d (analytics workers)  buffer=%d slots\n",
           unsafe_mode ? "UNSAFE (no table mutex)" : "SAFE (semaphores + mutex)", nprod, ncons, bsize);
    printf("readings to stream: %ld\n", nall);

    buf = malloc(sizeof(Reading) * (size_t)bsize);
    sem_init(&empty, 0, (unsigned)bsize);
    sem_init(&full, 0, 0);

    double t0 = now_ms();
    pthread_t pt[64], ct[64];
    for (int i = 0; i < ncons; i++) pthread_create(&ct[i], NULL, consumer, NULL);
    for (int i = 0; i < nprod; i++) pthread_create(&pt[i], NULL, producer, (void *)(long)i);
    for (int i = 0; i < nprod; i++) pthread_join(pt[i], NULL);
    for (int i = 0; i < ncons; i++) {                 /* one poison pill per consumer */
        sem_wait(&empty);
        pthread_mutex_lock(&buf_mutex);
        buf[in_idx] = (Reading){-1, 0, -1};
        in_idx = (in_idx + 1) % bsize;
        pthread_mutex_unlock(&buf_mutex);
        sem_post(&full);
    }
    for (int i = 0; i < ncons; i++) pthread_join(ct[i], NULL);
    double ms = now_ms() - t0;

    long table_count = 0;
    double table_sum = 0;
    for (int i = 0; i < MAX_PLOTS; i++) { table_count += plot_count[i]; table_sum += plot_sum[i]; }

    printf("\n%-34s %12s %12s %s\n", "check", "expected", "actual", "result");
    int ok = 1;
#define CHECK(label, exp, act) do { int good = (exp) == (act); ok &= good; \
        printf("%-34s %12ld %12ld %s\n", label, (long)(exp), (long)(act), good ? "OK" : "MISMATCH  <-- race condition"); } while (0)
    CHECK("readings produced = consumed", produced, consumed);
    CHECK("valid readings counted", ref_valid, total_valid);
    CHECK("rejected readings counted", ref_rej, total_rejected);
    CHECK("per-plot table entries", ref_valid, table_count);
    long d_ref = (long)(ref_sum * 100 + 0.5), d_act = (long)(table_sum * 100 + 0.5);
    CHECK("moisture sum (x100)", d_ref, d_act);

    printf("\nbuffer FULL events (producer blocked) : %ld\n", waits_full);
    printf("buffer EMPTY events (consumer blocked): %ld\n", waits_empty);
    printf("elapsed %.1f ms, throughput %.0f readings/s\n", ms, nall / (ms / 1000.0));
    if (ok)
        printf("\nRESULT: shared plot table is CONSISTENT - no lost updates, no race condition.\n");
    else
        printf("\nRESULT: %ld updates LOST - unsynchronised read-modify-write caused a RACE CONDITION.\n",
               (ref_valid + ref_rej) - (total_valid + total_rejected));

    sem_destroy(&empty);
    sem_destroy(&full);
    free(buf);
    free(all);
    return ok ? 0 : 4;
}
