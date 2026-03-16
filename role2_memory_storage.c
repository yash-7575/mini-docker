/*
 * role2_memory_storage.c  —  Memory & Storage Manager
 *
 * Responsibilities (40% implementation):
 *   1. Memory cgroup setup (memory.max, memory.high, OOM handling)
 *   2. Filesystem isolation with chroot
 *   3. Overlay filesystem mount (copy-on-write layers)
 *   4. /proc and tmpfs mounting inside container
 *
 * OS Concepts covered:
 *   Unit V — Memory Management (limits, virtual memory, OOM)
 *   Unit VI — File Management (fs isolation, directories, quotas)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>

#include "../include/container.h"

/* ─── cgroup v2 base path ──────────────────────────────────── */
#define CGROUP_BASE "/sys/fs/cgroup"

/* ═══════════════════════════════════════════════════════════
 * HELPER: Write a value into a cgroup control file
 *
 * cgroups v2 is controlled entirely through the filesystem.
 * Writing "268435456" to memory.max sets a 256 MB limit.
 * ═══════════════════════════════════════════════════════════ */
static int cgroup_write(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "[cgroup_write] open(%s): %s\n",
                path, strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    if (n < 0) {
        fprintf(stderr, "[cgroup_write] write(%s, %s): %s\n",
                path, value, strerror(errno));
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * HELPER: Read a value from a cgroup control file
 * ═══════════════════════════════════════════════════════════ */
static int cgroup_read(const char *path, char *buf, int len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[cgroup_read] open(%s): %s\n",
                path, strerror(errno));
        return -1;
    }
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    /* Strip trailing newline */
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * HELPER: Create directory if it doesn't exist
 * ═══════════════════════════════════════════════════════════ */
static int mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    /* Simple single-level mkdir for demo; production uses recursive */
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mkdir_p] %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * setup_memory_cgroup()
 *
 * Creates a cgroup for the container and sets:
 *   memory.max  → hard limit (container gets OOM-killed if exceeded)
 *   memory.high → soft limit (kernel starts reclaiming memory here,
 *                             but doesn't kill yet)
 *
 * How cgroups v2 memory limits work:
 *   memory.high < memory.max
 *   Below .high  → normal operation
 *   Above .high  → kernel aggressively reclaims pages (swapping out)
 *   Above .max   → OOM killer terminates the container
 * ═══════════════════════════════════════════════════════════ */
int setup_memory_cgroup(const char *container_id,
                        long limit_mb,
                        long reserve_mb) {
    char cgroup_path[512];
    char value_buf[64];

    snprintf(cgroup_path, sizeof(cgroup_path),
             "%s/container-%s", CGROUP_BASE, container_id);

    printf("[memory_cgroup] Creating cgroup: %s\n", cgroup_path);

    /* Step 1: Create the cgroup directory */
    if (mkdir_p(cgroup_path) < 0) return -1;

    /* Step 2: Enable memory controller
     * In cgroups v2, you enable controllers by writing to
     * the PARENT cgroup's cgroup.subtree_control file.
     */
    if (cgroup_write(CGROUP_BASE "/cgroup.subtree_control",
                     "+memory") < 0) {
        fprintf(stderr, "[memory_cgroup] WARNING: could not enable "
                "memory controller (need root + cgroups v2)\n");
        /* Continue anyway for demo purposes */
    }

    /* Step 3: Set hard limit (memory.max)
     * Value in bytes. "max" = unlimited.
     * Example: 268435456 = 256 MB
     */
    long limit_bytes = limit_mb * 1024 * 1024;
    snprintf(value_buf, sizeof(value_buf), "%ld", limit_bytes);

    char ctrl_path[600];
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/memory.max", cgroup_path);
    if (cgroup_write(ctrl_path, value_buf) < 0) {
        fprintf(stderr, "[memory_cgroup] Could not set memory.max "
                "(try running as root)\n");
    } else {
        printf("[memory_cgroup] memory.max = %ld MB (%ld bytes)\n",
               limit_mb, limit_bytes);
    }

    /* Step 4: Set soft limit (memory.high)
     * Set to 80% of hard limit as a sensible default.
     * Kernel starts reclaiming when this is crossed.
     */
    long high_bytes = reserve_mb > 0
                        ? reserve_mb * 1024 * 1024
                        : (long)(limit_bytes * 0.8);
    snprintf(value_buf, sizeof(value_buf), "%ld", high_bytes);
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/memory.high", cgroup_path);
    if (cgroup_write(ctrl_path, value_buf) < 0) {
        fprintf(stderr, "[memory_cgroup] Could not set memory.high\n");
    } else {
        printf("[memory_cgroup] memory.high = %ld bytes\n", high_bytes);
    }

    /* Step 5: Configure OOM killer behaviour
     * memory.oom.group = 1 means: when OOM, kill the ENTIRE cgroup
     * (all processes in the container), not just one process.
     * This prevents a partially-dead container from lingering.
     */
    snprintf(ctrl_path, sizeof(ctrl_path),
             "%s/memory.oom.group", cgroup_path);
    cgroup_write(ctrl_path, "1");

    /* Step 6: Read back to verify */
    char readback[64];
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/memory.max", cgroup_path);
    if (cgroup_read(ctrl_path, readback, sizeof(readback)) == 0)
        printf("[memory_cgroup] Verified memory.max = %s bytes\n", readback);

    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * setup_overlay_fs()
 *
 * Mounts an overlay filesystem. This is how Docker images work:
 *
 *   lower  (read-only)  = base OS image, shared across containers
 *   upper  (read-write) = container's own writable layer
 *   work                = overlay internal working directory
 *   merged              = the unified view the container sees
 *
 * If the container modifies /etc/hosts:
 *   - The original stays in lower (untouched, shared)
 *   - The modified copy goes into upper (container-specific)
 *   - merged shows the upper version (copy-on-write)
 *
 * When the container is deleted, just remove upper/ — base is intact.
 * ═══════════════════════════════════════════════════════════ */
int setup_overlay_fs(const char *id,
                     const char *lower,
                     const char *upper,
                     const char *work,
                     const char *merged) {

    printf("[overlay_fs] Setting up overlay for container: %s\n", id);

    /* Create required directories */
    mkdir_p(upper);
    mkdir_p(work);
    mkdir_p(merged);

    /*
     * Build the mount options string.
     * Format: "lowerdir=<path>,upperdir=<path>,workdir=<path>"
     */
    char options[1024];
    snprintf(options, sizeof(options),
             "lowerdir=%s,upperdir=%s,workdir=%s",
             lower, upper, work);

    printf("[overlay_fs] Options: %s\n", options);

    /*
     * Mount the overlay filesystem.
     * The "source" (first arg) is just "overlay" — a kernel module.
     * The actual paths are in the options string.
     */
    if (mount("overlay", merged, "overlay", 0, options) < 0) {
        fprintf(stderr, "[overlay_fs] mount overlay failed: %s\n"
                "  (needs root and kernel overlay support)\n",
                strerror(errno));
        return -1;  /* Non-fatal in demo — real system needs this */
    }

    printf("[overlay_fs] Overlay mounted at: %s\n", merged);
    printf("[overlay_fs] Container sees merged view (CoW layer active)\n");
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * mount_proc_and_dev()
 *
 * After creating a new PID namespace (CLONE_NEWPID), the old
 * /proc still shows HOST processes. We must replace it.
 *
 * Also mount a minimal /dev using devtmpfs and a tmpfs for /tmp.
 *
 * This runs INSIDE the container (called from container_init).
 * ═══════════════════════════════════════════════════════════ */
int mount_proc_and_dev(const char *rootfs) {
    char path[512];
    int  ret = 0;

    /* ── 1. Make the mount tree private ────────────────────── */
    /*
     * MS_PRIVATE: unmounts/mounts here don't propagate to parent NS.
     * This is REQUIRED before doing anything with mounts inside
     * a new mount namespace.
     */
    if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        perror("[mount_proc] making root private");
        /* Continue — may already be private */
    }

    /* ── 2. Mount fresh /proc ───────────────────────────────── */
    /*
     * This /proc will ONLY show processes in this PID namespace.
     * Without this, `ps aux` inside the container shows ALL host processes.
     *
     * MS_NOSUID  — prevent setuid escalation via proc
     * MS_NODEV   — no device files in proc
     * MS_NOEXEC  — no executing binaries from proc
     */
    snprintf(path, sizeof(path), "%s/proc", rootfs);
    mkdir_p(path);

    if (mount("proc", path, "proc",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
        fprintf(stderr, "[mount_proc] mount /proc: %s\n", strerror(errno));
        ret = -1;
    } else {
        printf("[mount_proc] /proc mounted (container-only view)\n");
    }

    /* ── 3. Mount tmpfs on /tmp ─────────────────────────────── */
    /*
     * tmpfs is a RAM-backed filesystem. Each container gets its own,
     * so /tmp files are never visible between containers.
     * "size=64m" caps it at 64 MB.
     */
    snprintf(path, sizeof(path), "%s/tmp", rootfs);
    mkdir_p(path);

    if (mount("tmpfs", path, "tmpfs",
              MS_NOSUID | MS_NODEV, "size=64m") < 0) {
        fprintf(stderr, "[mount_proc] mount /tmp: %s\n", strerror(errno));
    } else {
        printf("[mount_proc] /tmp mounted as tmpfs (64 MB, RAM-backed)\n");
    }

    /* ── 4. Mount devtmpfs on /dev ──────────────────────────── */
    snprintf(path, sizeof(path), "%s/dev", rootfs);
    mkdir_p(path);

    if (mount("devtmpfs", path, "devtmpfs",
              MS_NOSUID | MS_NOEXEC, NULL) < 0) {
        /* Fallback: just report, not critical for demo */
        fprintf(stderr, "[mount_proc] mount /dev: %s\n", strerror(errno));
    } else {
        printf("[mount_proc] /dev mounted as devtmpfs\n");
    }

    return ret;
}

/* ═══════════════════════════════════════════════════════════
 * setup_rootfs()
 *
 * High-level function called by Architecture Lead's
 * container_create() to set up the complete filesystem.
 *
 * Sequence:
 *   1. Create container directory structure
 *   2. Set up overlay (if lower image exists)
 *   3. Mount /proc, /tmp, /dev
 * ═══════════════════════════════════════════════════════════ */
int setup_rootfs(container_t *c) {
    char base[512], upper[512], work[512], merged[512];

    snprintf(base,   sizeof(base),   "/tmp/containers/%s", c->id);
    snprintf(upper,  sizeof(upper),  "%s/upper",           base);
    snprintf(work,   sizeof(work),   "%s/work",            base);
    snprintf(merged, sizeof(merged), "%s/merged",          base);

    /* Use /bin as a minimal read-only lower layer for demo */
    const char *lower = "/";

    printf("[setup_rootfs] Preparing rootfs for container %s\n", c->id);
    mkdir_p(base);

    /* Set container rootfs to the merged overlay view */
    strncpy(c->rootfs, merged, ROOTFS_PATH_LEN - 1);

    /* Setup overlay (copy-on-write) filesystem */
    if (setup_overlay_fs(c->id, lower, upper, work, merged) < 0) {
        printf("[setup_rootfs] WARNING: overlay failed, "
               "using host root (demo mode)\n");
        strncpy(c->rootfs, "/", ROOTFS_PATH_LEN - 1);
    }

    /* Mount virtual filesystems inside the container's root */
    mount_proc_and_dev(c->rootfs);

    /* Apply memory cgroup limits */
    setup_memory_cgroup(c->id,
                        c->resources.mem_limit_mb,
                        c->resources.mem_reserve_mb);

    printf("[setup_rootfs] rootfs ready at: %s\n", c->rootfs);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * teardown_rootfs()
 *
 * Called on container_destroy(). Unmounts all filesystems
 * and removes the container's directory tree.
 * ═══════════════════════════════════════════════════════════ */
int teardown_rootfs(container_t *c) {
    char path[512];

    printf("[teardown_rootfs] Cleaning up container %s\n", c->id);

    /* Unmount in reverse order */
    snprintf(path, sizeof(path), "%s/dev",  c->rootfs); umount2(path, MNT_DETACH);
    snprintf(path, sizeof(path), "%s/tmp",  c->rootfs); umount2(path, MNT_DETACH);
    snprintf(path, sizeof(path), "%s/proc", c->rootfs); umount2(path, MNT_DETACH);

    /* Unmount the overlay itself */
    umount2(c->rootfs, MNT_DETACH);

    /* Remove cgroup directory */
    char cgroup_path[512];
    snprintf(cgroup_path, sizeof(cgroup_path),
             "%s/container-%s", CGROUP_BASE, c->id);
    rmdir(cgroup_path);  /* Only works if cgroup has no tasks */

    printf("[teardown_rootfs] Cleanup complete for %s\n", c->id);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Demo main()
 * ═══════════════════════════════════════════════════════════ */
#ifdef ROLE2_DEMO
int main(void) {
    container_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.id, "mem-test01", CONTAINER_ID_LEN - 1);
    c.resources.mem_limit_mb   = 256;
    c.resources.mem_reserve_mb = 200;
    strncpy(c.rootfs, "/tmp/containers/mem-test01/merged", ROOTFS_PATH_LEN - 1);

    printf("=== Memory & Storage Demo ===\n\n");

    printf("--- Memory cgroup ---\n");
    setup_memory_cgroup(c.id,
                        c.resources.mem_limit_mb,
                        c.resources.mem_reserve_mb);

    printf("\n--- Overlay filesystem ---\n");
    setup_overlay_fs(c.id,
                     "/",                           /* lower (read-only base) */
                     "/tmp/containers/upper",       /* upper (writable layer) */
                     "/tmp/containers/work",        /* overlay work dir       */
                     "/tmp/containers/merged");     /* merged view            */

    printf("\n--- Virtual fs mounts ---\n");
    mount_proc_and_dev("/tmp/containers/merged");

    printf("\n=== Teardown ===\n");
    teardown_rootfs(&c);

    return 0;
}
#endif
