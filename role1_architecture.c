/*
 * role1_architecture.c  —  Architecture Lead
 *
 * Responsibilities (40% implementation):
 *   1. Container creation using clone() with namespace flags
 *   2. Process lifecycle & state management
 *   3. Zombie prevention via waitpid()
 *   4. Graceful shutdown + forced kill
 *
 * OS Concepts covered: Unit II — Process Management
 *   fork/exec, parent-child, process states, zombie prevention
 */

#define _GNU_SOURCE          /* Required for clone(), unshare() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sched.h>           /* clone(), CLONE_NEW* flags        */
#include <sys/wait.h>        /* waitpid()                        */
#include <sys/types.h>
#include <sys/mount.h>

#include "../include/container.h"

/* ─── Stack size for clone() child ────────────────────────── */
#define STACK_SIZE (1024 * 1024)   /* 1 MB stack per container  */

/* ─── Namespace flags we isolate ──────────────────────────── */
/*
 * CLONE_NEWPID  → Container gets its own PID 1
 * CLONE_NEWNS   → Container gets its own mount table
 * CLONE_NEWNET  → Container gets its own network interfaces
 * CLONE_NEWUTS  → Container gets its own hostname
 * CLONE_NEWIPC  → Container gets its own SysV IPC / POSIX MQ
 * CLONE_NEWUSER → Container root maps to unprivileged host UID
 */
#define NAMESPACE_FLAGS  (CLONE_NEWPID  | \
                          CLONE_NEWNS   | \
                          CLONE_NEWNET  | \
                          CLONE_NEWUTS  | \
                          CLONE_NEWIPC)

/* ─── Arguments passed into the container init process ─────── */
typedef struct {
    container_t *container;
    char        *argv[8];    /* Program to run inside container  */
} clone_args_t;

/* ═══════════════════════════════════════════════════════════
 * HELPER: Generate a short random container ID
 * ═══════════════════════════════════════════════════════════ */
static void generate_id(char *buf, int len) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    srand((unsigned)time(NULL) ^ getpid());
    for (int i = 0; i < len - 1; i++)
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    buf[len - 1] = '\0';
}

/* ═══════════════════════════════════════════════════════════
 * HELPER: String representation of container state
 * ═══════════════════════════════════════════════════════════ */
void container_state_str(container_state_t s, char *buf, int len) {
    const char *names[] = {
        "PENDING", "CREATING", "RUNNING",
        "STOPPING", "STOPPED", "TERMINATED"
    };
    if (s >= 0 && s <= STATE_TERMINATED)
        snprintf(buf, len, "%s", names[s]);
    else
        snprintf(buf, len, "UNKNOWN");
}

/* ═══════════════════════════════════════════════════════════
 * INIT: The first function that runs INSIDE the new container
 *
 * This is the container's PID 1.  Everything here executes in
 * the new namespaces — it sees its own /proc, its own PIDs.
 * ═══════════════════════════════════════════════════════════ */
static int container_init(void *arg) {
    clone_args_t *cargs = (clone_args_t *)arg;
    container_t  *c     = cargs->container;

    printf("[container:%s] PID inside namespace = %d\n", c->id, getpid());

    /*
     * Step 1: Remount /proc
     *   After CLONE_NEWPID, the old /proc still shows HOST processes.
     *   We must unmount it and mount a fresh one that only shows
     *   processes in this PID namespace.
     */
    if (mount("none", "/proc", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        perror("[container_init] making /proc private");
        /* non-fatal, continue */
    }
    if (mount("proc", "/proc", "proc",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
        perror("[container_init] mounting fresh /proc");
        /* non-fatal for demo */
    }

    /*
     * Step 2: Set hostname to container ID
     *   Possible because we have CLONE_NEWUTS — changes here do NOT
     *   affect the host or other containers.
     */
    if (sethostname(c->id, strlen(c->id)) < 0)
        perror("[container_init] sethostname");

    /*
     * Step 3: Execute the container's workload
     *   In a real orchestrator this would be the user's image entrypoint.
     *   For demo we run /bin/sh so you can interact with the container.
     */
    char *default_argv[] = { "/bin/sh", NULL };
    char **argv = cargs->argv[0] ? cargs->argv : default_argv;

    printf("[container:%s] execve → %s\n", c->id, argv[0]);
    execvp(argv[0], argv);

    /* execvp only returns on error */
    perror("[container_init] execvp failed");
    return 1;
}

/* ═══════════════════════════════════════════════════════════
 * container_create()
 *
 * Allocates a new container_t, initialises resource config,
 * and uses clone() to spawn an isolated child process.
 *
 * Returns: 0 on success, -1 on error
 * ═══════════════════════════════════════════════════════════ */
int container_create(const char *id,
                     resource_config_t *res,
                     container_t *out) {

    if (!id || !res || !out) {
        fprintf(stderr, "container_create: NULL argument\n");
        return -1;
    }

    /* ── Populate the container descriptor ─────────────────── */
    memset(out, 0, sizeof(container_t));

    if (id && strlen(id) > 0)
        strncpy(out->id, id, CONTAINER_ID_LEN - 1);
    else
        generate_id(out->id, CONTAINER_ID_LEN);

    memcpy(&out->resources, res, sizeof(resource_config_t));
    out->state      = STATE_CREATING;
    out->created_at = (uint64_t)time(NULL);
    out->pid        = -1;

    /* Default rootfs path — Role 2 will populate this properly */
    snprintf(out->rootfs, ROOTFS_PATH_LEN,
             "/tmp/containers/%s/rootfs", out->id);

    printf("[container_create] id=%s state=CREATING\n", out->id);

    /* ── Allocate stack for the clone() child ───────────────── */
    /*
     * clone() needs a pre-allocated stack for the child.
     * We allocate it and pass a pointer to the TOP (stacks grow down).
     */
    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc stack");
        out->state = STATE_TERMINATED;
        return -1;
    }
    char *stack_top = stack + STACK_SIZE;  /* stacks grow downward */

    /* ── Prepare arguments for container_init() ─────────────── */
    clone_args_t cargs;
    memset(&cargs, 0, sizeof(cargs));
    cargs.container = out;
    /* argv[0] = NULL → container_init falls back to /bin/sh */

    /* ── clone() — THE key syscall ──────────────────────────── */
    /*
     * clone() is like fork() but lets us specify:
     *   NAMESPACE_FLAGS → which namespaces to create fresh
     *   SIGCHLD         → signal sent to parent when child exits
     *
     * The child starts executing container_init() immediately.
     */
    pid_t pid = clone(container_init,
                      stack_top,
                      NAMESPACE_FLAGS | SIGCHLD,
                      (void *)&cargs);

    if (pid < 0) {
        perror("clone");
        free(stack);
        out->state = STATE_TERMINATED;
        return -1;
    }

    out->pid   = pid;
    out->state = STATE_RUNNING;

    printf("[container_create] id=%s pid=%d state=RUNNING\n",
           out->id, out->pid);

    /*
     * NOTE: we intentionally do NOT free(stack) here.
     * The stack is in use by the child process.
     * It will be freed when the child exits and we call waitpid().
     * In production you'd store the stack pointer in container_t.
     */

    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * container_stop()
 *
 * Graceful stop: send SIGTERM, wait up to 5 s, then SIGKILL.
 * This is the same two-phase shutdown Docker uses.
 * ═══════════════════════════════════════════════════════════ */
int container_stop(container_t *c, int force) {
    if (!c || c->pid <= 0) return -1;

    if (c->state != STATE_RUNNING) {
        printf("[container_stop] %s not running (state=%d)\n",
               c->id, c->state);
        return 0;
    }

    c->state = STATE_STOPPING;
    printf("[container_stop] id=%s pid=%d force=%d\n",
           c->id, c->pid, force);

    if (!force) {
        /* ── Phase 1: ask nicely with SIGTERM ─────────────── */
        if (kill(c->pid, SIGTERM) < 0) {
            perror("kill SIGTERM");
        } else {
            /* Wait up to 5 seconds for graceful exit */
            for (int i = 0; i < 50; i++) {
                int status;
                pid_t result = waitpid(c->pid, &status, WNOHANG);
                if (result == c->pid) {
                    /* Process exited cleanly */
                    c->state = STATE_STOPPED;
                    printf("[container_stop] %s exited gracefully\n", c->id);
                    return 0;
                }
                usleep(100000); /* 100ms sleep */
            }
            printf("[container_stop] %s did not exit after SIGTERM, "
                   "sending SIGKILL\n", c->id);
        }
    }

    /* ── Phase 2: force kill with SIGKILL ─────────────────── */
    if (kill(c->pid, SIGKILL) < 0) {
        perror("kill SIGKILL");
        return -1;
    }

    /* Now block until the process is fully dead */
    int status;
    if (waitpid(c->pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    c->state = STATE_STOPPED;
    printf("[container_stop] id=%s killed. Exit code: %d\n",
           c->id, WEXITSTATUS(status));
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * container_destroy()
 *
 * Stop (if running) + cleanup cgroup + namespace fds.
 * After this call the container_t is safe to free/reuse.
 * ═══════════════════════════════════════════════════════════ */
int container_destroy(container_t *c) {
    if (!c) return -1;

    printf("[container_destroy] id=%s\n", c->id);

    /* Stop first if still running */
    if (c->state == STATE_RUNNING)
        container_stop(c, 0);  /* graceful first */

    /* Close namespace file descriptors if open */
    if (c->ns_pid_fd > 0) { close(c->ns_pid_fd); c->ns_pid_fd = 0; }
    if (c->ns_mnt_fd > 0) { close(c->ns_mnt_fd); c->ns_mnt_fd = 0; }
    if (c->cgroup_fd  > 0) { close(c->cgroup_fd);  c->cgroup_fd  = 0; }

    c->state = STATE_TERMINATED;
    c->pid   = -1;

    printf("[container_destroy] id=%s state=TERMINATED\n", c->id);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * SIGCHLD handler — Zombie Prevention
 *
 * Install this handler in the orchestrator process.
 * Whenever ANY child (container) exits, we reap it immediately
 * using waitpid(-1, WNOHANG) so it never becomes a zombie.
 * ═══════════════════════════════════════════════════════════ */
void sigchld_handler(int sig) {
    (void)sig;
    int   status;
    pid_t pid;

    /*
     * Loop: multiple children could have exited between signals.
     * WNOHANG: return immediately if no child has exited.
     */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[sigchld] reaped zombie pid=%d exit=%d\n",
               pid, WEXITSTATUS(status));
        /* TODO: look up container by pid and update its state */
    }
}

/* ═══════════════════════════════════════════════════════════
 * Demo main() — Run to see container creation in action
 * ═══════════════════════════════════════════════════════════ */
#ifdef ROLE1_DEMO
int main(void) {
    /* Install zombie-reaper signal handler */
    struct sigaction sa = { .sa_handler = sigchld_handler,
                            .sa_flags   = SA_RESTART | SA_NOCLDSTOP };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    /* Define resource limits for the container */
    resource_config_t res = {
        .cpu_shares    = 512,
        .cpu_quota_us  = 50000,   /* 50ms per 100ms = 50% of 1 core */
        .cpu_period_us = 100000,
        .mem_limit_mb  = 256,
        .mem_reserve_mb= 128,
        .storage_limit_mb = 1024
    };

    container_t c;
    if (container_create("demo01", &res, &c) < 0) {
        fprintf(stderr, "Failed to create container\n");
        return 1;
    }

    char state_str[32];
    container_state_str(c.state, state_str, sizeof(state_str));
    printf("\nContainer created:\n");
    printf("  ID    : %s\n", c.id);
    printf("  PID   : %d\n", c.pid);
    printf("  State : %s\n", state_str);
    printf("  CPU   : %ld shares, %ldus quota\n",
           c.resources.cpu_shares, c.resources.cpu_quota_us);
    printf("  RAM   : %ld MB limit\n", c.resources.mem_limit_mb);

    /* Let container run for 3 seconds then stop it */
    sleep(3);
    container_stop(&c, 0);
    container_destroy(&c);

    return 0;
}
#endif
