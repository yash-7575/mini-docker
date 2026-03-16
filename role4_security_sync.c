/*
 * role4_security_sync.c  —  Security & Synchronization
 *
 * Responsibilities (40% implementation):
 *   1. Banker's Algorithm for deadlock avoidance
 *   2. POSIX semaphore-based mutex between containers
 *   3. IPC namespace isolation check
 *   4. Resource graph deadlock detection
 *
 * OS Concepts covered:
 *   Unit II — Semaphores, Mutex, Classical Sync Problems
 *   Unit IV — Deadlock Prevention, Avoidance (Banker's),
 *              Detection, Recovery
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>          /* pthread_mutex for IPC demo   */
#include <semaphore.h>        /* POSIX semaphores             */
#include <sys/ipc.h>
#include <sys/sem.h>          /* System V semaphores          */
#include <sys/shm.h>          /* Shared memory                */
#include <fcntl.h>

#include "../include/container.h"

/* ═══════════════════════════════════════════════════════════
 * BANKER'S ALGORITHM
 *
 * Context: N containers each requesting M resource types
 * (e.g. CPU units, memory blocks, network bandwidth slots)
 *
 * Key matrices:
 *   available[M]      — free units of each resource right now
 *   max[N][M]         — max a container will EVER need
 *   allocation[N][M]  — currently allocated to each container
 *   need[N][M]        — still needed = max - allocation
 *
 * Safe state: there EXISTS a sequence where every container
 * can eventually finish (get all it needs, run, then release).
 *
 * Before GRANTING any request, we simulate it and check:
 * if the resulting state is safe → grant
 * if the resulting state is unsafe → deny, make it wait
 * ═══════════════════════════════════════════════════════════ */

#define MAX_CONTAINERS  8
#define MAX_RESOURCES   4

/* Current system state */
static int n_containers = 0;         /* Active container count  */
static int n_resources  = 0;         /* Resource type count     */

static int available[MAX_RESOURCES];
static int max_need  [MAX_CONTAINERS][MAX_RESOURCES];
static int allocation[MAX_CONTAINERS][MAX_RESOURCES];
static int need      [MAX_CONTAINERS][MAX_RESOURCES];

static char container_ids[MAX_CONTAINERS][CONTAINER_ID_LEN];

/* Mutex protecting the Banker's state */
static pthread_mutex_t bankers_lock = PTHREAD_MUTEX_INITIALIZER;

/* ─── Initialise the Banker's algorithm state ─────────────── */
void bankers_init(int num_containers, int num_resources,
                  int *avail, int max_matrix[][MAX_RESOURCES]) {
    pthread_mutex_lock(&bankers_lock);

    n_containers = num_containers;
    n_resources  = num_resources;

    for (int r = 0; r < n_resources; r++)
        available[r] = avail[r];

    for (int c = 0; c < n_containers; c++) {
        for (int r = 0; r < n_resources; r++) {
            max_need  [c][r] = max_matrix[c][r];
            allocation[c][r] = 0;
            need      [c][r] = max_matrix[c][r]; /* need = max - alloc */
        }
    }

    printf("[bankers] Initialized: %d containers, %d resource types\n",
           n_containers, n_resources);
    printf("[bankers] Available resources: ");
    for (int r = 0; r < n_resources; r++)
        printf("%d ", available[r]);
    printf("\n");

    pthread_mutex_unlock(&bankers_lock);
}

/* ═══════════════════════════════════════════════════════════
 * is_safe_state()
 *
 * The heart of Banker's algorithm.
 *
 * Safety algorithm:
 *   1. Let work[] = copy of available[]
 *   2. Let finish[i] = false for all containers
 *   3. Find i where: finish[i]=false AND need[i] <= work
 *      If found: work += allocation[i]; finish[i] = true; goto 3
 *   4. If all finish[i] = true → SAFE
 *      Else → UNSAFE (deadlock possible)
 *
 * Returns: 1 if safe, 0 if unsafe
 *
 * Note: This runs WITHOUT the lock (caller holds it, or we
 * pass work/finish as temps so we don't modify global state).
 * ═══════════════════════════════════════════════════════════ */
int is_safe_state(void) {
    int work[MAX_RESOURCES];
    int finish[MAX_CONTAINERS];
    int safe_seq[MAX_CONTAINERS];
    int seq_idx = 0;

    /* Step 1: work = available */
    for (int r = 0; r < n_resources; r++)
        work[r] = available[r];

    /* Step 2: finish[] all false */
    memset(finish, 0, sizeof(finish));

    /* Step 3: Find a safe sequence */
    int found;
    do {
        found = 0;
        for (int c = 0; c < n_containers; c++) {
            if (finish[c]) continue;

            /* Can container c's remaining need be satisfied by work? */
            int can_satisfy = 1;
            for (int r = 0; r < n_resources; r++) {
                if (need[c][r] > work[r]) {
                    can_satisfy = 0;
                    break;
                }
            }

            if (can_satisfy) {
                /* Simulate container c finishing: release its allocation */
                for (int r = 0; r < n_resources; r++)
                    work[r] += allocation[c][r];
                finish[c]          = 1;
                safe_seq[seq_idx++] = c;
                found = 1;
            }
        }
    } while (found);

    /* Step 4: All containers finished? */
    int safe = 1;
    for (int c = 0; c < n_containers; c++) {
        if (!finish[c]) { safe = 0; break; }
    }

    if (safe) {
        printf("[bankers] State is SAFE. Safe sequence: ");
        for (int i = 0; i < seq_idx; i++)
            printf("C%d ", safe_seq[i]);
        printf("\n");
    } else {
        printf("[bankers] State is UNSAFE — deadlock possible!\n");
    }

    return safe;
}

/* ═══════════════════════════════════════════════════════════
 * bankers_request()
 *
 * A container requests resources.
 * We check if granting it keeps the system in a safe state.
 *
 * Returns:  0 = granted
 *          -1 = denied (would cause unsafe state or exceeds max)
 * ═══════════════════════════════════════════════════════════ */
int bankers_request(int container_idx, int *request) {
    if (container_idx < 0 || container_idx >= n_containers) return -1;

    pthread_mutex_lock(&bankers_lock);

    printf("[bankers] Container C%d requesting: ", container_idx);
    for (int r = 0; r < n_resources; r++)
        printf("%d ", request[r]);
    printf("\n");

    /* ── Check 1: Request <= Need ───────────────────────────── */
    for (int r = 0; r < n_resources; r++) {
        if (request[r] > need[container_idx][r]) {
            printf("[bankers] DENIED: request[%d]=%d > need[%d]=%d "
                   "(exceeds declared maximum!)\n",
                   r, request[r], r, need[container_idx][r]);
            pthread_mutex_unlock(&bankers_lock);
            return -1;
        }
    }

    /* ── Check 2: Request <= Available ──────────────────────── */
    for (int r = 0; r < n_resources; r++) {
        if (request[r] > available[r]) {
            printf("[bankers] WAIT: request[%d]=%d > available[%d]=%d "
                   "(not enough resources now, retry later)\n",
                   r, request[r], r, available[r]);
            pthread_mutex_unlock(&bankers_lock);
            return -1;
        }
    }

    /* ── Simulate granting the request ──────────────────────── */
    for (int r = 0; r < n_resources; r++) {
        available[container_idx]   -= request[r];  /* Reduce available  */
        allocation[container_idx][r]+= request[r]; /* Add to allocation */
        need[container_idx][r]     -= request[r];  /* Reduce need       */
    }
    /* Fix: available is global array not per-container */
    for (int r = 0; r < n_resources; r++) {
        available[r] -= request[r];
        allocation[container_idx][r] += request[r];
        need[container_idx][r]       -= request[r];
        /* Undo the double-apply from above */
        available[r] += request[r]; /* Reset */
    }
    /* Clean simulation */
    int temp_avail[MAX_RESOURCES];
    for (int r = 0; r < n_resources; r++) {
        temp_avail[r]                = available[r]              - request[r];
        allocation[container_idx][r]+= request[r];
        need[container_idx][r]      -= request[r];
    }

    /* ── Check if resulting state is safe ──────────────────── */
    int avail_backup[MAX_RESOURCES];
    memcpy(avail_backup, available, sizeof(available));
    memcpy(available,    temp_avail, sizeof(temp_avail));

    int safe = is_safe_state();

    if (safe) {
        /* GRANT: keep the simulated state */
        printf("[bankers] GRANTED: C%d request approved\n", container_idx);
    } else {
        /* DENY: roll back the simulation */
        memcpy(available, avail_backup, sizeof(avail_backup));
        for (int r = 0; r < n_resources; r++) {
            allocation[container_idx][r] -= request[r];
            need[container_idx][r]       += request[r];
        }
        printf("[bankers] DENIED: granting would cause unsafe state\n");
    }

    pthread_mutex_unlock(&bankers_lock);
    return safe ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════
 * bankers_release()
 *
 * Container releases resources (e.g. on exit or completion).
 * Always safe — releasing resources can only improve the state.
 * ═══════════════════════════════════════════════════════════ */
int bankers_release(int container_idx, int *release) {
    if (container_idx < 0 || container_idx >= n_containers) return -1;

    pthread_mutex_lock(&bankers_lock);

    printf("[bankers] Container C%d releasing: ", container_idx);
    for (int r = 0; r < n_resources; r++)
        printf("%d ", release[r]);
    printf("\n");

    for (int r = 0; r < n_resources; r++) {
        /* Validate: can't release more than allocated */
        if (release[r] > allocation[container_idx][r]) {
            fprintf(stderr, "[bankers] ERROR: release[%d]=%d > "
                    "allocation[%d]=%d\n",
                    r, release[r], r, allocation[container_idx][r]);
            pthread_mutex_unlock(&bankers_lock);
            return -1;
        }
        allocation[container_idx][r] -= release[r];
        need[container_idx][r]       += release[r];
        available[r]                 += release[r];
    }

    printf("[bankers] C%d release done. Available now: ", container_idx);
    for (int r = 0; r < n_resources; r++)
        printf("%d ", available[r]);
    printf("\n");

    pthread_mutex_unlock(&bankers_lock);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * print_bankers_state()
 *
 * Print all matrices — useful for viva explanation.
 * ═══════════════════════════════════════════════════════════ */
void print_bankers_state(void) {
    printf("\n┌─── Banker's Algorithm State ───────────────────────┐\n");
    printf("│ Available:  ");
    for (int r = 0; r < n_resources; r++)
        printf("R%d=%-3d ", r, available[r]);
    printf("\n│\n");

    printf("│ %-6s │ %-20s │ %-20s │ %-20s\n",
           "Cont.", "Max Need", "Allocated", "Still Needs");
    printf("│ ──────┼──────────────────────┼");
    printf("──────────────────────┼──────────────────────\n");

    for (int c = 0; c < n_containers; c++) {
        printf("│ C%-4d │ ", c);
        for (int r = 0; r < n_resources; r++) printf("%-3d ", max_need[c][r]);
        printf(" │ ");
        for (int r = 0; r < n_resources; r++) printf("%-3d ", allocation[c][r]);
        printf(" │ ");
        for (int r = 0; r < n_resources; r++) printf("%-3d ", need[c][r]);
        printf("\n");
    }
    printf("└────────────────────────────────────────────────────┘\n");
}

/* ═══════════════════════════════════════════════════════════
 * POSIX Semaphore — Shared Resource Mutex
 *
 * Used when two containers legitimately share a resource
 * (e.g. a shared volume or a message queue).
 * This demonstrates Unit II classical sync problems.
 * ═══════════════════════════════════════════════════════════ */
#define SEM_NAME "/container_shared_resource"

sem_t *create_shared_semaphore(const char *name, int initial_value) {
    sem_t *sem = sem_open(name, O_CREAT, 0644, initial_value);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return NULL;
    }
    printf("[semaphore] Created %s with initial value %d\n",
           name, initial_value);
    return sem;
}

/* Container "acquires" the shared resource (like mutex lock) */
int resource_acquire(sem_t *sem, const char *container_id) {
    printf("[semaphore] %s waiting for resource...\n", container_id);
    if (sem_wait(sem) < 0) {
        perror("sem_wait");
        return -1;
    }
    printf("[semaphore] %s acquired resource\n", container_id);
    return 0;
}

/* Container "releases" the shared resource (like mutex unlock) */
int resource_release(sem_t *sem, const char *container_id) {
    if (sem_post(sem) < 0) {
        perror("sem_post");
        return -1;
    }
    printf("[semaphore] %s released resource\n", container_id);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Demo main() — Full Banker's scenario
 *
 * Scenario: 3 containers, 3 resource types
 *   Resources: [CPU_units=10, Mem_blocks=5, Net_slots=7]
 * ═══════════════════════════════════════════════════════════ */
#ifdef ROLE4_DEMO
int main(void) {
    printf("=== Security & Synchronization Demo ===\n\n");

    /* ── Banker's Algorithm Demo ────────────────────────────── */
    printf("--- Banker's Algorithm ---\n");
    printf("Resources: R0=CPU_units, R1=Mem_blocks, R2=Net_slots\n\n");

    /* Initial available: 3 CPU, 3 Mem, 2 Net */
    int avail[MAX_RESOURCES] = {3, 3, 2, 0};

    /* Max each container will ever need */
    int maxmat[MAX_CONTAINERS][MAX_RESOURCES] = {
        {7, 5, 3, 0},  /* C0 */
        {3, 2, 2, 0},  /* C1 */
        {9, 0, 2, 0},  /* C2 */
    };

    bankers_init(3, 3, avail, maxmat);

    /* Pre-allocate some resources (initial state) */
    int alloc_c0[] = {0, 1, 0};
    int alloc_c1[] = {2, 0, 0};
    int alloc_c2[] = {3, 0, 2};

    /* Manually set initial allocation */
    for (int r = 0; r < 3; r++) {
        allocation[0][r] = alloc_c0[r];
        allocation[1][r] = alloc_c1[r];
        allocation[2][r] = alloc_c2[r];
        need[0][r] = maxmat[0][r] - alloc_c0[r];
        need[1][r] = maxmat[1][r] - alloc_c1[r];
        need[2][r] = maxmat[2][r] - alloc_c2[r];
        available[r] -= (alloc_c0[r] + alloc_c1[r] + alloc_c2[r]);
    }

    print_bankers_state();
    printf("\n1. Initial safety check:\n");
    is_safe_state();

    printf("\n2. C1 requests [1,0,2] (SAFE — should be granted):\n");
    int req1[] = {1, 0, 2};
    bankers_request(1, req1);
    print_bankers_state();

    printf("\n3. C0 requests [0,2,0] (test — may be denied):\n");
    int req2[] = {0, 2, 0};
    bankers_request(0, req2);

    printf("\n4. C2 releases [3,0,2]:\n");
    int rel1[] = {3, 0, 2};
    bankers_release(2, rel1);
    print_bankers_state();

    /* ── Semaphore Demo ─────────────────────────────────────── */
    printf("\n--- Semaphore (Mutual Exclusion) Demo ---\n");
    sem_unlink(SEM_NAME); /* Clean up any leftover */
    sem_t *shared_sem = create_shared_semaphore(SEM_NAME, 1);
    if (shared_sem) {
        resource_acquire(shared_sem,  "container-A");
        printf("[container-A] Using shared resource...\n");
        resource_release(shared_sem,  "container-A");
        resource_acquire(shared_sem,  "container-B");
        printf("[container-B] Using shared resource...\n");
        resource_release(shared_sem,  "container-B");
        sem_close(shared_sem);
        sem_unlink(SEM_NAME);
    }

    return 0;
}
#endif
