/*
 * role3_scheduler.c  —  Resource Scheduler
 *
 * Responsibilities (40% implementation):
 *   1. CPU cgroup setup (cpu.shares, cpu.max)
 *   2. Weighted Round-Robin scheduler in user-space
 *   3. CPU utilisation reading from cpu.stat
 *   4. Fairness verification
 *
 * OS Concepts covered: Unit III — Process Scheduling
 *   FCFS, Round Robin, Priority (shares = priority weights),
 *   scheduling criteria (fairness, throughput, CPU utilisation)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>

#include "../include/container.h"

#define CGROUP_BASE      "/sys/fs/cgroup"
#define MAX_SCHED_CONTS  16
#define DEFAULT_SHARES   1024   /* Linux default CPU share weight */
#define DEFAULT_PERIOD   100000 /* 100ms scheduling period        */

/* ─── Run-queue entry for our user-space scheduler ─────────── */
typedef struct {
    char     id[CONTAINER_ID_LEN];
    long     shares;          /* CPU weight (higher = more time)  */
    uint64_t cpu_used_us;     /* CPU time used so far (µs)        */
    uint64_t last_read_us;    /* Timestamp of last cpu.stat read  */
    int      active;          /* 1 = in run queue                 */
} sched_entry_t;

/* Global run queue */
static sched_entry_t run_queue[MAX_SCHED_CONTS];
static int           rq_count = 0;

/* ═══════════════════════════════════════════════════════════
 * HELPER: Write to cgroup file
 * ═══════════════════════════════════════════════════════════ */
static int cg_write(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "[cg_write] %s: %s\n", path, strerror(errno));
        return -1;
    }
    write(fd, val, strlen(val));
    close(fd);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * HELPER: Get current time in microseconds
 * ═══════════════════════════════════════════════════════════ */
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL +
           (uint64_t)(ts.tv_nsec / 1000);
}

/* ═══════════════════════════════════════════════════════════
 * setup_cpu_cgroup()
 *
 * Two levers for CPU control in cgroups v2:
 *
 * 1. cpu.weight (shares) — SOFT limit / proportional sharing
 *    - Only matters when CPU is contested (busy system)
 *    - Range: 1–10000. Default is 100.
 *    - Container with weight 200 gets 2× time vs weight 100
 *    - If only one container is running, it gets 100% regardless
 *
 * 2. cpu.max — HARD limit / absolute ceiling
 *    - Format: "quota period" both in microseconds
 *    - "50000 100000" = 50ms per 100ms = max 50% of 1 core
 *    - Even if CPU is idle, container is throttled to this
 *    - "max 100000" = unlimited (default)
 * ═══════════════════════════════════════════════════════════ */
int setup_cpu_cgroup(const char *id,
                     long shares,
                     long quota_us,
                     long period_us) {

    char cgroup_path[512];
    char value_buf[128];
    char ctrl_path[600];

    snprintf(cgroup_path, sizeof(cgroup_path),
             "%s/container-%s", CGROUP_BASE, id);

    printf("[cpu_cgroup] Configuring CPU for container: %s\n", id);

    /* Create cgroup dir if not done by memory setup */
    if (mkdir(cgroup_path, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[cpu_cgroup] mkdir: %s\n", strerror(errno));
        return -1;
    }

    /* Enable CPU controller in parent */
    cg_write(CGROUP_BASE "/cgroup.subtree_control", "+cpu");

    /* ── Set cpu.weight (proportional sharing) ─────────────── */
    /*
     * cgroups v2 uses cpu.weight (1–10000), not cpu.shares (2–262144)
     * The relationship: weight ≈ shares / 1024 * 100
     * We normalise: 1024 shares → weight 100 (default)
     */
    long weight = (shares * 100) / DEFAULT_SHARES;
    if (weight < 1)   weight = 1;
    if (weight > 10000) weight = 10000;

    snprintf(value_buf, sizeof(value_buf), "%ld", weight);
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/cpu.weight", cgroup_path);
    if (cg_write(ctrl_path, value_buf) < 0) {
        fprintf(stderr, "[cpu_cgroup] Could not set cpu.weight "
                "(need root + cgroups v2)\n");
    } else {
        printf("[cpu_cgroup] cpu.weight = %ld "
               "(from %ld shares)\n", weight, shares);
    }

    /* ── Set cpu.max (hard throttle) ────────────────────────── */
    /*
     * "max" keyword = unlimited (no throttle)
     * Numeric quota_us = throttle to quota_us/period_us fraction
     */
    if (quota_us > 0 && period_us > 0) {
        snprintf(value_buf, sizeof(value_buf),
                 "%ld %ld", quota_us, period_us);
        double pct = (double)quota_us / period_us * 100.0;
        printf("[cpu_cgroup] cpu.max = %ld/%ld µs = %.1f%% of 1 core\n",
               quota_us, period_us, pct);
    } else {
        snprintf(value_buf, sizeof(value_buf), "max %ld",
                 period_us > 0 ? period_us : DEFAULT_PERIOD);
        printf("[cpu_cgroup] cpu.max = unlimited\n");
    }

    snprintf(ctrl_path, sizeof(ctrl_path), "%s/cpu.max", cgroup_path);
    if (cg_write(ctrl_path, value_buf) < 0)
        fprintf(stderr, "[cpu_cgroup] Could not set cpu.max\n");

    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * read_cpu_usage()
 *
 * Reads cpu.stat from the container's cgroup and extracts
 * usage_usec — total CPU time consumed in microseconds.
 *
 * cpu.stat format (cgroups v2):
 *   usage_usec 12345678
 *   user_usec  8000000
 *   system_usec 4345678
 *   nr_periods  100
 *   nr_throttled 5
 *   throttled_usec 234567
 *
 * We use this to measure actual CPU consumed vs configured shares.
 * ═══════════════════════════════════════════════════════════ */
int read_cpu_usage(const char *id, uint64_t *usage_us) {
    char path[512];
    snprintf(path, sizeof(path),
             "%s/container-%s/cpu.stat", CGROUP_BASE, id);

    FILE *f = fopen(path, "r");
    if (!f) {
        /* Gracefully handle: cgroup may not exist yet in demo */
        *usage_us = 0;
        return -1;
    }

    char key[64];
    uint64_t val;
    while (fscanf(f, "%63s %lu", key, &val) == 2) {
        if (strcmp(key, "usage_usec") == 0) {
            *usage_us = val;
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    *usage_us = 0;
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 * SCHEDULER: Weighted Round-Robin
 *
 * This is a USER-SPACE scheduler that sits on top of cgroups.
 * It decides which container to "promote" next based on:
 *   1. How much CPU each container is entitled to (shares/total)
 *   2. How much it has actually used (cpu_used_us)
 *
 * Algorithm: pick the container with the largest
 *   DEFICIT = entitled_us - actual_used_us
 *
 * This is the core idea behind Linux's CFS (Completely Fair
 * Scheduler) — which tracks "virtual runtime" the same way.
 * ═══════════════════════════════════════════════════════════ */

/* Add a container to the scheduler run queue */
int scheduler_add(const char *id, long shares) {
    if (rq_count >= MAX_SCHED_CONTS) {
        fprintf(stderr, "[scheduler] Run queue full\n");
        return -1;
    }
    sched_entry_t *e = &run_queue[rq_count++];
    strncpy(e->id, id, CONTAINER_ID_LEN - 1);
    e->shares       = shares > 0 ? shares : DEFAULT_SHARES;
    e->cpu_used_us  = 0;
    e->last_read_us = now_us();
    e->active       = 1;

    printf("[scheduler] Added container %s with %ld shares\n", id, shares);
    return 0;
}

/* Remove a container from the scheduler */
int scheduler_remove(const char *id) {
    for (int i = 0; i < rq_count; i++) {
        if (strcmp(run_queue[i].id, id) == 0) {
            run_queue[i].active = 0;
            printf("[scheduler] Removed container %s\n", id);
            return 0;
        }
    }
    return -1;
}

/*
 * scheduler_next()
 * Returns the ID of the next container to run, or NULL if empty.
 *
 * Selection criteria: Maximum Weighted Deficit
 *   deficit_i = (shares_i / total_shares) * elapsed_us - used_i
 *
 * The container with the highest deficit is most "behind" relative
 * to what it deserves → it gets the next time slice.
 */
const char *scheduler_next(void) {
    long   total_shares = 0;
    uint64_t elapsed_us = 1000000; /* 1 second window for calculation */

    /* Sum active shares */
    for (int i = 0; i < rq_count; i++)
        if (run_queue[i].active)
            total_shares += run_queue[i].shares;

    if (total_shares == 0) return NULL;

    int     best_idx    = -1;
    double  best_deficit = -1e18;

    for (int i = 0; i < rq_count; i++) {
        if (!run_queue[i].active) continue;

        sched_entry_t *e = &run_queue[i];

        /* Update actual usage from cgroup */
        uint64_t actual;
        if (read_cpu_usage(e->id, &actual) == 0)
            e->cpu_used_us = actual;

        /* Compute how much CPU this container is entitled to */
        double entitled_us = ((double)e->shares / total_shares) * elapsed_us;

        /* Deficit = entitlement - actual usage */
        double deficit = entitled_us - (double)e->cpu_used_us;

        printf("[scheduler] %s: shares=%ld entitled=%.0fµs "
               "used=%luµs deficit=%.0fµs\n",
               e->id, e->shares, entitled_us, e->cpu_used_us, deficit);

        if (deficit > best_deficit) {
            best_deficit = deficit;
            best_idx = i;
        }
    }

    if (best_idx < 0) return NULL;
    return run_queue[best_idx].id;
}

/* ═══════════════════════════════════════════════════════════
 * print_scheduler_fairness()
 *
 * Prints a table showing whether CPU distribution matches
 * the configured shares ratio.
 *
 * Example with 2 containers:
 *   A: shares=2048, used=6.6s → expected 66.7%
 *   B: shares=1024, used=3.3s → expected 33.3%
 *   Fairness deviation: < 5% = PASS
 * ═══════════════════════════════════════════════════════════ */
void print_scheduler_fairness(void) {
    long total_shares = 0;
    uint64_t total_used = 0;

    for (int i = 0; i < rq_count; i++) {
        if (!run_queue[i].active) continue;
        total_shares += run_queue[i].shares;
        /* Refresh usage */
        uint64_t u;
        if (read_cpu_usage(run_queue[i].id, &u) == 0)
            run_queue[i].cpu_used_us = u;
        total_used += run_queue[i].cpu_used_us;
    }

    printf("\n┌────────────────┬────────┬────────────┬──────────┬─────────┐\n");
    printf("│ Container      │ Shares │ Expected%% │ Actual%%  │ Status  │\n");
    printf("├────────────────┼────────┼────────────┼──────────┼─────────┤\n");

    for (int i = 0; i < rq_count; i++) {
        if (!run_queue[i].active) continue;
        sched_entry_t *e = &run_queue[i];

        double expected_pct = total_shares > 0
            ? (double)e->shares / total_shares * 100.0
            : 0.0;
        double actual_pct = total_used > 0
            ? (double)e->cpu_used_us / total_used * 100.0
            : 0.0;
        double deviation = expected_pct - actual_pct;
        if (deviation < 0) deviation = -deviation;

        const char *status = deviation < 5.0 ? "FAIR ✓" : "SKEWED";

        printf("│ %-14s │ %6ld │ %9.1f%% │ %7.1f%% │ %-7s │\n",
               e->id, e->shares, expected_pct, actual_pct, status);
    }
    printf("└────────────────┴────────┴────────────┴──────────┴─────────┘\n");
    printf("Fairness target: deviation < 5%% (project requirement ±5%%)\n\n");
}

/* ═══════════════════════════════════════════════════════════
 * enforce_resource_limits()
 *
 * Called periodically. Reads actual usage and enforces:
 *   - If container exceeds its cpu.max, kernel throttles it
 *   - If container exceeds memory.max, OOM killer fires
 *   - We just verify and report — kernel does the enforcement
 * ═══════════════════════════════════════════════════════════ */
int enforce_resource_limits(container_t *c) {
    uint64_t usage;
    if (read_cpu_usage(c->id, &usage) == 0) {
        printf("[enforce] Container %s CPU used: %lu µs\n",
               c->id, usage);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Demo main()
 * ═══════════════════════════════════════════════════════════ */
#ifdef ROLE3_DEMO
int main(void) {
    printf("=== CPU Scheduler Demo ===\n\n");

    /* Setup CPU cgroups for 3 containers */
    printf("--- Setting up CPU cgroups ---\n");
    setup_cpu_cgroup("sched-A", 2048, 70000, 100000); /* 70% hard cap  */
    setup_cpu_cgroup("sched-B", 1024, 0,     0);      /* unlimited     */
    setup_cpu_cgroup("sched-C", 512,  25000, 100000); /* 25% hard cap  */

    printf("\n--- Building run queue ---\n");
    scheduler_add("sched-A", 2048);
    scheduler_add("sched-B", 1024);
    scheduler_add("sched-C", 512);

    printf("\n--- Weighted Round-Robin scheduling decisions ---\n");
    printf("(In a real system each call triggers a context switch)\n\n");
    for (int tick = 0; tick < 6; tick++) {
        const char *next = scheduler_next();
        printf("Tick %d → Schedule: %s\n", tick + 1, next ? next : "none");
        usleep(100000); /* 100ms between scheduling decisions */
    }

    printf("\n--- Fairness report ---\n");
    print_scheduler_fairness();

    /* Cleanup */
    scheduler_remove("sched-A");
    scheduler_remove("sched-B");
    scheduler_remove("sched-C");

    return 0;
}
#endif
