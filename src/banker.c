/*
 * banker.c - EXPERIMENT 9: Deadlock avoidance with the Banker's algorithm.
 *
 * Processes : irrigation jobs of each village for the next 48 h
 * Resources : shared pump sets, 3-phase power slots (15 kW feeder blocks),
 *             canal valve gates, fertigation units
 * Max claim : per-village demand produced by the advisory stage
 *             (outputs/pump_demand.csv); a scenario file fixes the values
 *             so the demonstration is reproducible.
 *
 * The program runs the safety algorithm on the initial state and then
 * processes every resource request in the scenario with the
 * resource-request algorithm: GRANTED (state stays safe), DENIED (would make
 * the state unsafe), WAIT (not enough available) or ERROR (exceeds the
 * declared maximum claim).
 *
 * usage: banker [scenario_file]     (default data/config/banker_scenario.txt)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXP 16
#define MAXR 8

static int  np, nr;
static char rname[MAXR][24], pname[MAXP][24];
static int  avail[MAXR], maxc[MAXP][MAXR], alloc[MAXP][MAXR], need[MAXP][MAXR], total[MAXR];
static int  released[MAXP];          /* job finished and returned its resources */

static void print_vec(const int *v)
{
    printf("[");
    for (int j = 0; j < nr; j++) printf("%s%d", j ? " " : "", v[j]);
    printf("]");
}

static void print_state(void)
{
    printf("\n%-12s %-14s %-14s %-14s\n", "Job", "Allocation", "Max", "Need");
    for (int i = 0; i < np; i++) {
        printf("%-12s ", pname[i]);
        print_vec(alloc[i]); printf("%*s", 14 - 2 * nr - 1, "");
        print_vec(maxc[i]);  printf("%*s", 14 - 2 * nr - 1, "");
        print_vec(need[i]);  printf("\n");
    }
    printf("Available    "); print_vec(avail); printf("\n");
}

/* Safety algorithm; prints the trace when verbose. Returns 1 if safe. */
static int is_safe(int verbose)
{
    int work[MAXR], finish[MAXP] = {0}, seq[MAXP], k = 0, active = 0;
    memcpy(work, avail, sizeof work);
    for (int i = 0; i < np; i++) { finish[i] = released[i]; active += !released[i]; }
    for (int round = 0; round < np; round++) {
        int progressed = 0;
        for (int i = 0; i < np; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < nr; j++) if (need[i][j] > work[j]) { ok = 0; break; }
            if (!ok) continue;
            if (verbose) {
                printf("  %-12s Need ", pname[i]); print_vec(need[i]);
                printf(" <= Work "); print_vec(work);
            }
            for (int j = 0; j < nr; j++) work[j] += alloc[i][j];
            if (verbose) { printf("  -> finishes, Work = "); print_vec(work); printf("\n"); }
            finish[i] = 1;
            seq[k++] = i;
            progressed = 1;
        }
        if (!progressed) break;
    }
    if (k == active) {
        if (verbose) {
            printf("  SAFE. Safe sequence: < ");
            for (int i = 0; i < k; i++) printf("%s%s", pname[seq[i]], i < k - 1 ? ", " : " >\n");
        }
        return 1;
    }
    if (verbose) {
        printf("  UNSAFE. Jobs that cannot be guaranteed to finish: ");
        for (int i = 0; i < np; i++) if (!finish[i]) printf("%s ", pname[i]);
        printf("\n");
    }
    return 0;
}

static void request(int i, const int *req)
{
    printf("\nREQUEST %s ", pname[i]); print_vec(req); printf("\n");
    for (int j = 0; j < nr; j++)
        if (req[j] > need[i][j]) {
            printf("  ERROR: request exceeds declared maximum claim for %s (need %d) -> REJECTED\n", rname[j], need[i][j]);
            return;
        }
    for (int j = 0; j < nr; j++)
        if (req[j] > avail[j]) {
            printf("  only %d %s available -> job must WAIT (not granted)\n", avail[j], rname[j]);
            return;
        }
    for (int j = 0; j < nr; j++) { avail[j] -= req[j]; alloc[i][j] += req[j]; need[i][j] -= req[j]; }
    printf("  pretend allocation, run safety algorithm:\n");
    if (is_safe(1)) {
        printf("  => GRANTED. Available now "); print_vec(avail); printf("\n");
    } else {
        for (int j = 0; j < nr; j++) { avail[j] += req[j]; alloc[i][j] -= req[j]; need[i][j] += req[j]; }
        printf("  => DENIED: granting would lead to an UNSAFE state (possible deadlock). Rolled back.\n");
    }
}

static int find_proc(const char *n)
{
    for (int i = 0; i < np; i++) if (strcmp(pname[i], n) == 0) return i;
    return -1;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "data/config/banker_scenario.txt";
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "ERROR: cannot open scenario '%s'\n", path); return 2; }

    printf("EXPERIMENT 9 - Banker's algorithm for irrigation resources (KJS-AGR-01)\n");
    printf("scenario: %s\n", path);

    char line[512];
    int lineno = 0, checked = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *tok = strtok(line, " \t\r\n");
        if (!tok || tok[0] == '#') continue;

        if (strcmp(tok, "resources") == 0) {
            while ((tok = strtok(NULL, " \t\r\n")) && nr < MAXR) snprintf(rname[nr++], 24, "%s", tok);
        } else if (strcmp(tok, "total") == 0) {
            for (int j = 0; j < nr; j++) { tok = strtok(NULL, " \t\r\n"); total[j] = tok ? atoi(tok) : 0; }
        } else if (strcmp(tok, "job") == 0) {          /* job <name> max a b c d alloc a b c d */
            if (np == MAXP) continue;
            snprintf(pname[np], 24, "%s", strtok(NULL, " \t\r\n"));
            strtok(NULL, " \t\r\n");                     /* "max" */
            for (int j = 0; j < nr; j++) maxc[np][j] = atoi(strtok(NULL, " \t\r\n"));
            strtok(NULL, " \t\r\n");                     /* "alloc" */
            for (int j = 0; j < nr; j++) alloc[np][j] = atoi(strtok(NULL, " \t\r\n"));
            for (int j = 0; j < nr; j++) {
                need[np][j] = maxc[np][j] - alloc[np][j];
                if (need[np][j] < 0) { fprintf(stderr, "ERROR line %d: allocation exceeds max\n", lineno); return 3; }
            }
            np++;
        } else if (strcmp(tok, "request") == 0 || strcmp(tok, "check") == 0 || strcmp(tok, "release") == 0) {
            if (!checked) {                              /* first time: compute Available, initial safety */
                printf("\nResources: ");
                for (int j = 0; j < nr; j++) printf("%s%s", rname[j], j < nr - 1 ? ", " : "\n");
                printf("Total    : "); print_vec(total); printf("\n");
                for (int j = 0; j < nr; j++) {
                    avail[j] = total[j];
                    for (int i = 0; i < np; i++) avail[j] -= alloc[i][j];
                    if (avail[j] < 0) { fprintf(stderr, "ERROR: over-allocated %s\n", rname[j]); return 3; }
                }
                print_state();
                printf("\nINITIAL STATE - safety algorithm:\n");
                is_safe(1);
                checked = 1;
            }
            if (tok[0] == 'c') continue;
            if (strcmp(tok, "release") == 0) {           /* job finished irrigation: return everything */
                const char *who = strtok(NULL, " \t\r\n");
                int i = who ? find_proc(who) : -1;
                if (i < 0) continue;
                printf("\nRELEASE %s: irrigation complete, returns ", pname[i]); print_vec(alloc[i]);
                for (int j = 0; j < nr; j++) { avail[j] += alloc[i][j]; alloc[i][j] = 0; need[i][j] = 0; maxc[i][j] = 0; }
                released[i] = 1;
                printf("  -> Available "); print_vec(avail); printf("\n");
                continue;
            }
            const char *who = strtok(NULL, " \t\r\n");
            int i = who ? find_proc(who) : -1;
            if (i < 0) { printf("\nREQUEST from unknown job '%s' -> ignored\n", who ? who : "?"); continue; }
            int req[MAXR];
            for (int j = 0; j < nr; j++) { tok = strtok(NULL, " \t\r\n"); req[j] = tok ? atoi(tok) : 0; }
            request(i, req);
        }
    }
    fclose(f);
    if (np == 0 || nr == 0) { fprintf(stderr, "ERROR: scenario defines no jobs/resources\n"); return 3; }
    printf("\nFINAL STATE:");
    print_state();
    return 0;
}
