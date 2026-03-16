#ifndef CONTAINER_H
#define CONTAINER_H

#include <sys/types.h>
#include <stdint.h>

/* ─── Container States ─────────────────────────────────────── */
typedef enum {
    STATE_PENDING    = 0,
    STATE_CREATING   = 1,
    STATE_RUNNING    = 2,
    STATE_STOPPING   = 3,
    STATE_STOPPED    = 4,
    STATE_TERMINATED = 5
} container_state_t;

/* ─── Resource Config ──────────────────────────────────────── */
typedef struct {
    long   cpu_shares;       /* Relative CPU weight (default 1024)     */
    long   cpu_quota_us;     /* Hard CPU quota in microseconds          */
    long   cpu_period_us;    /* Period for quota (default 100000 = 100ms)*/
    long   mem_limit_mb;     /* Hard memory limit in MB                 */
    long   mem_reserve_mb;   /* Soft memory reservation in MB           */
    long   storage_limit_mb; /* Disk quota in MB                        */
} resource_config_t;

/* ─── Container Struct ─────────────────────────────────────── */
#define MAX_CONTAINERS  64
#define CONTAINER_ID_LEN 16
#define ROOTFS_PATH_LEN  256

typedef struct {
    char              id[CONTAINER_ID_LEN];   /* Unique container ID      */
    pid_t             pid;                    /* Host PID of init process  */
    container_state_t state;                  /* Current lifecycle state   */
    resource_config_t resources;              /* CPU/mem/storage config    */
    char              rootfs[ROOTFS_PATH_LEN];/* Root filesystem path      */
    int               ns_pid_fd;             /* FD to PID namespace       */
    int               ns_mnt_fd;             /* FD to Mount namespace     */
    int               cgroup_fd;             /* FD to cgroup directory    */
    uint64_t          created_at;            /* Unix timestamp            */
} container_t;

/* ─── Global Container Table ───────────────────────────────── */
typedef struct {
    container_t containers[MAX_CONTAINERS];
    int         count;
} container_table_t;

/* ─── Function Declarations ────────────────────────────────── */

/* Role 1 — Architecture Lead */
int  container_create(const char *id, resource_config_t *res, container_t *out);
int  container_start(container_t *c);
int  container_stop(container_t *c, int force);
int  container_destroy(container_t *c);
void container_state_str(container_state_t s, char *buf, int len);

/* Role 2 — Memory & Storage */
int  setup_memory_cgroup(const char *id, long limit_mb, long reserve_mb);
int  setup_rootfs(container_t *c);
int  setup_overlay_fs(const char *id, const char *lower, const char *upper, const char *work, const char *merged);
int  mount_proc_and_dev(const char *rootfs);
int  teardown_rootfs(container_t *c);

/* Role 3 — Scheduler */
int  setup_cpu_cgroup(const char *id, long shares, long quota_us, long period_us);
int  read_cpu_usage(const char *id, uint64_t *usage_us);
int  enforce_resource_limits(container_t *c);

/* Role 4 — Security & Sync */
int  setup_ipc_namespace(container_t *c);
int  bankers_request(int container_idx, int *request);
int  bankers_release(int container_idx, int *release);
int  is_safe_state(void);
int  apply_seccomp_filter(void);

/* Role 5 — Monitoring */
int  collect_metrics(const char *id, double *cpu_pct, long *mem_mb);
void print_metrics_table(container_table_t *table);

#endif /* CONTAINER_H */
