# Mini Docker — Container Orchestration System
### ML2011 Operating Systems | VIT Pune 2025-26

A from-scratch container runtime built in C that implements the core mechanisms behind Docker:
Linux **namespaces**, **cgroups v2**, **overlay filesystem**, **Banker's algorithm for deadlock avoidance**, and a **live metrics dashboard** — all without using Docker itself.

---

## Table of Contents

1. [What This Project Does](#1-what-this-project-does)
2. [Repository Structure](#2-repository-structure)
3. [System Requirements](#3-system-requirements)
4. [Install Dependencies (Ubuntu)](#4-install-dependencies-ubuntu)
5. [Clone the Repository](#5-clone-the-repository)
6. [One-Time Setup](#6-one-time-setup)
7. [Role 4 — Security & Deadlock Avoidance (no sudo)](#7-role-4--security--deadlock-avoidance-no-sudo)
8. [Role 1 — Architecture & Namespaces (needs sudo)](#8-role-1--architecture--namespaces-needs-sudo)
9. [Role 2 — Memory & Storage (needs sudo)](#9-role-2--memory--storage-needs-sudo)
10. [Role 3 — CPU Scheduler (needs sudo)](#10-role-3--cpu-scheduler-needs-sudo)
11. [Role 5 — Monitoring Dashboard (needs sudo)](#11-role-5--monitoring-dashboard-needs-sudo)
12. [Verify What's Running (Viva Commands)](#12-verify-whats-running-viva-commands)
13. [Troubleshooting](#13-troubleshooting)
14. [OS Syllabus Coverage](#14-os-syllabus-coverage)

---

## 1. What This Project Does

| Role | File | What it Demonstrates |
|------|------|----------------------|
| Role 1 | `role1_architecture.c` | `clone()` syscall, PID/Mount/Net/UTS/IPC namespace isolation, process lifecycle, zombie prevention |
| Role 2 | `role2_memory_storage.c` | cgroups v2 memory limits (`memory.max`, `memory.high`), overlay filesystem (copy-on-write), `/proc` isolation |
| Role 3 | `role3_scheduler.c` | CPU cgroup (`cpu.weight`, `cpu.max`), weighted round-robin scheduler, fairness verification |
| Role 4 | `role4_security_sync.c` | Banker's algorithm (deadlock avoidance), POSIX semaphores, IPC namespace, resource graphs |
| Role 5 | `role5_monitoring.c` | Live ASCII metrics dashboard, container startup timing, CPU % accuracy test |

---

## 2. Repository Structure

```
mini-docker/
├── container.h              ← Shared header (ALL roles need this)
├── role1_architecture.c     ← Role 1 source
├── role2_memory_storage.c   ← Role 2 source
├── role3_scheduler.c        ← Role 3 source
├── role4_security_sync.c    ← Role 4 source
├── role5_monitoring.c       ← Role 5 source
├── Makefile
└── README.md
```

After running `make setup` (step 6), the Makefile also creates:

```
mini-docker/
├── src/                     ← Copies of all .c files (compiler needs this)
│   ├── role1_architecture.c
│   ├── role2_memory_storage.c
│   ├── role3_scheduler.c
│   ├── role4_security_sync.c
│   └── role5_monitoring.c
├── include/                 ← Copy of container.h
│   └── container.h
└── bin/                     ← Compiled binaries go here (created automatically)
```

---

## 3. System Requirements

| Requirement | Minimum |
|-------------|---------|
| OS | Ubuntu 20.04 LTS or Ubuntu 22.04 LTS (64-bit) |
| Kernel | 5.4+ (cgroups v2 + all namespace types) |
| RAM | 512 MB free |
| Disk | 200 MB free |
| CPU | Any x86-64 |

> **Roles 1, 2, 3, 5 require `sudo`** because they use Linux namespaces and cgroups, which need root privileges.
> **Role 4 does NOT need `sudo`** — it is pure userspace (Banker's algorithm + semaphores).

### Check your Ubuntu version

```bash
lsb_release -a
uname -r
```

Expected output example:

```
Ubuntu 22.04.3 LTS
5.15.0-91-generic
```

---

## 4. Install Dependencies (Ubuntu)

Open a terminal and run these commands one by one:

### Step 4.1 — Update package list

```bash
sudo apt update
```

### Step 4.2 — Install GCC and Make

```bash
sudo apt install -y gcc make
```

### Step 4.3 — Verify the installation

```bash
gcc --version
make --version
```

Expected output:

```
gcc (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
GNU Make 4.3
```

Any version of GCC 9+ and Make 4+ will work.

### Step 4.4 — Verify cgroups v2 is active (required for Roles 2, 3, 5)

```bash
mount | grep cgroup2
```

Expected output (if cgroups v2 is active):

```
cgroup2 on /sys/fs/cgroup type cgroup2 (rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot)
```

If you see nothing, check:

```bash
cat /proc/filesystems | grep cgroup
```

If `cgroup2` is not listed, your kernel does not support cgroups v2. Upgrade to Ubuntu 22.04.

---

## 5. Clone the Repository

```bash
git clone <your-repo-url> mini-docker
cd mini-docker
```

Replace `<your-repo-url>` with the actual GitHub/GitLab URL. If you already have the folder, just `cd` into it:

```bash
cd mini-docker
ls
```

You should see these files:

```
container.h  Makefile  README.md  role1_architecture.c  role2_memory_storage.c
role3_scheduler.c  role4_security_sync.c  role5_monitoring.c
```

---

## 6. One-Time Setup

The Makefile compiles files from `src/` and `include/` subdirectories. Run this **once** to set up the structure:

```bash
make setup
```

Expected output:

```
  copied role1_architecture.c -> src/role1_architecture.c
  copied role2_memory_storage.c -> src/role2_memory_storage.c
  copied role3_scheduler.c -> src/role3_scheduler.c
  copied role4_security_sync.c -> src/role4_security_sync.c
  copied role5_monitoring.c -> src/role5_monitoring.c
  copied container.h -> include/container.h

Setup complete! Now run your role:
  make role4              # no sudo needed
  sudo make role1         # needs root
```

> You only need to run `make setup` **once**. After that, go directly to your role below.

---

## 7. Role 4 — Security & Deadlock Avoidance (no sudo)

**Assigned to:** Security & Synchronization team member

**OS Concepts:** Banker's Algorithm (deadlock avoidance), POSIX semaphores, IPC namespace, resource graph

**No root needed — run this first to verify your setup works.**

### Step 7.1 — Build

```bash
make role4
```

Expected output:

```
gcc -Wall -Wextra -g -I./include -DROLE4_DEMO src/role4_security_sync.c -o bin/role4_demo -lpthread
Built. Run: ./bin/role4_demo
```

### Step 7.2 — Run

```bash
./bin/role4_demo
```

### Expected Output

```
=== Security & Synchronization Demo ===

--- Banker's Algorithm ---
Resources: R0=CPU_units, R1=Mem_blocks, R2=Net_slots

[bankers] Initialized: 3 containers, 3 resource types
[bankers] Available resources: 3 3 2

1. Initial safety check:
[bankers] State is SAFE. Safe sequence: C1 C0 C2

2. C1 requests [1,0,2] (SAFE — should be granted):
[bankers] State is SAFE. Safe sequence: C1 C0 C2
[bankers] GRANTED: C1 request approved

3. C0 requests [0,2,0] (may be denied):
[bankers] State is UNSAFE — deadlock possible!
[bankers] DENIED: granting would cause unsafe state

--- POSIX Semaphore Demo ---
[sem] Container C0 acquiring shared resource...
[sem] Container C0 holds resource (critical section)
[sem] Container C0 releasing resource
[sem] Container C1 acquiring shared resource...
```

### What This Demonstrates

- **Banker's Algorithm**: Before granting any resource request, the system simulates the allocation and checks whether a safe execution sequence still exists. If granting the request would lead to a state where deadlock is possible, the request is **denied**.
- **Safe sequence**: An ordering of containers (e.g., C1 → C0 → C2) where each container can eventually get all the resources it needs, finish, and release them.
- **POSIX semaphores**: Mutual exclusion between container processes sharing a resource.

---

## 8. Role 1 — Architecture & Namespaces (needs sudo)

**Assigned to:** Architecture Lead team member

**OS Concepts:** `clone()` syscall, PID/Mount/Net/UTS/IPC namespace isolation, process lifecycle states, zombie prevention with `waitpid()`

### Step 8.1 — Build

```bash
sudo make role1
```

Expected output:

```
gcc -Wall -Wextra -g -I./include -DROLE1_DEMO src/role1_architecture.c -o bin/role1_demo -lpthread
Built. Run: sudo ./bin/role1_demo
```

### Step 8.2 — Run

```bash
sudo ./bin/role1_demo
```

### Expected Output

```
[container_create] id=demo01 state=CREATING
[container:demo01] PID inside namespace = 1
[container:demo01] execve → /bin/sh
[container_create] id=demo01 pid=12345 state=RUNNING

Container created:
  ID    : demo01
  PID   : 12345
  State : RUNNING
  CPU   : 512 shares, 50000us quota
  RAM   : 256 MB limit

[container_stop] id=demo01 pid=12345 force=0
[container_stop] demo01 exited gracefully
[container_destroy] id=demo01
[container_destroy] id=demo01 state=TERMINATED
```

> Note: The container runs `/bin/sh` for 3 seconds then gracefully shuts down. You will NOT get an interactive shell — the demo automatically stops it.

### What This Demonstrates

- **`clone()` with namespace flags**: Creates a child process that lives in isolated PID, Mount, Network, UTS (hostname), and IPC namespaces. The child sees itself as PID 1.
- **`/proc` remount**: After `CLONE_NEWPID`, the old `/proc` still shows host processes. The container remounts it so `ps` inside only shows its own processes.
- **Graceful shutdown**: SIGTERM → wait 5 seconds → SIGKILL (same as Docker's `docker stop`).
- **Zombie prevention**: `SIGCHLD` handler calls `waitpid(-1, WNOHANG)` to immediately reap any exited container.

### Verify Namespace Isolation (while container is running)

In a **second terminal**, while `./bin/role1_demo` is still running:

```bash
# Find the container's PID
sudo ps aux | grep role1_demo

# See its namespace file descriptors
sudo ls -la /proc/<PID>/ns/

# Compare with host namespaces
ls -la /proc/self/ns/
```

The inode numbers for `pid`, `mnt`, `net`, `uts`, `ipc` will be different between the container process and host — proof of isolation.

---

## 9. Role 2 — Memory & Storage (needs sudo)

**Assigned to:** Memory & Storage team member

**OS Concepts:** cgroups v2 memory limits, OOM killer, overlay filesystem (copy-on-write), `/proc` isolation, `tmpfs`

### Step 9.1 — Build

```bash
sudo make role2
```

Expected output:

```
gcc -Wall -Wextra -g -I./include -DROLE2_DEMO src/role2_memory_storage.c -o bin/role2_demo -lpthread
Built. Run: sudo ./bin/role2_demo
```

### Step 9.2 — Run

```bash
sudo ./bin/role2_demo
```

### Expected Output

```
=== Memory & Storage Demo ===

--- Memory cgroup ---
[memory_cgroup] Creating cgroup: /sys/fs/cgroup/container-mem-test01
[memory_cgroup] memory.max = 256 MB (268435456 bytes)
[memory_cgroup] memory.high = 209715200 bytes
[memory_cgroup] Verified memory.max = 268435456 bytes

--- Overlay filesystem ---
[overlay_fs] Setting up overlay for container: mem-test01
[overlay_fs] Options: lowerdir=/,upperdir=/tmp/containers/upper,workdir=/tmp/containers/work
[overlay_fs] Overlay mounted at: /tmp/containers/merged
[overlay_fs] Container sees merged view (CoW layer active)

--- Virtual fs mounts ---
[mount_proc] /proc mounted (container-only view)
[mount_proc] /tmp mounted as tmpfs (64 MB, RAM-backed)
[mount_proc] /dev mounted as devtmpfs

=== Teardown ===
[teardown_rootfs] Cleaning up container mem-test01
[teardown_rootfs] Cleanup complete for mem-test01
```

### What This Demonstrates

- **`memory.max`**: Hard memory cap. If the container exceeds 256 MB, the OOM killer terminates it.
- **`memory.high`**: Soft limit (80% of hard limit). The kernel aggressively reclaims memory pages when this is crossed, but doesn't kill yet.
- **Overlay FS (copy-on-write)**:
  - `lower` = host root `/` (read-only, shared across all containers)
  - `upper` = `/tmp/containers/upper` (container's writable layer)
  - `merged` = the view the container sees (writes go to `upper`, reads fall through to `lower`)
  - Delete the container → just remove `upper/`. The base image is untouched.
- **`memory.oom.group = 1`**: OOM kills the entire cgroup (all container processes), not just one.

### Verify the cgroup limits after running

```bash
# Check memory limit was set
cat /sys/fs/cgroup/container-mem-test01/memory.max

# Check soft limit
cat /sys/fs/cgroup/container-mem-test01/memory.high
```

> If these files don't exist after the demo, teardown already cleaned them up — that's expected.

---

## 10. Role 3 — CPU Scheduler (needs sudo)

**Assigned to:** Scheduler team member

**OS Concepts:** CPU scheduling (Round Robin, Priority/Shares), cgroups v2 CPU controls, weighted scheduling, fairness

### Step 10.1 — Build

```bash
sudo make role3
```

Expected output:

```
gcc -Wall -Wextra -g -I./include -DROLE3_DEMO src/role3_scheduler.c -o bin/role3_demo -lpthread
Built. Run: sudo ./bin/role3_demo
```

### Step 10.2 — Run

```bash
sudo ./bin/role3_demo
```

### Expected Output

```
=== CPU Scheduler Demo ===

--- Setting up CPU cgroups ---
[cpu_cgroup] container-web01: cpu.weight=200, cpu.max=50000 100000
[cpu_cgroup] container-db01:  cpu.weight=400, cpu.max=80000 100000
[cpu_cgroup] container-bg01:  cpu.weight=100, cpu.max=20000 100000

--- Weighted Round-Robin Scheduler ---
Shares ratio — web01:db01:bg01 = 2:4:1

Scheduling round 1:
  [sched] db01   → 4 time-slices  (57.1% of CPU)
  [sched] web01  → 2 time-slices  (28.6% of CPU)
  [sched] bg01   → 1 time-slice   (14.3% of CPU)

--- Fairness Verification ---
  Expected  db01 : 57.1%  Actual: 56.8%  PASS (within ±5%)
  Expected  web01: 28.6%  Actual: 28.9%  PASS (within ±5%)
  Expected  bg01 : 14.3%  Actual: 14.3%  PASS (within ±5%)
```

### What This Demonstrates

- **`cpu.weight`** (soft limit): Proportional CPU sharing when the system is busy. `db01` with weight 400 gets 2× CPU time vs `web01` with weight 200.
- **`cpu.max`** (hard limit): Hard throttle — e.g., `50000 100000` means 50ms out of every 100ms period = 50% of one core maximum. The process is throttled (not killed) when it hits this.
- **Weighted Round-Robin**: User-space scheduler that allocates time slices proportional to each container's weight.
- **Fairness target**: CPU allocation must be within ±5% of the configured ratio.

### Verify CPU weights after running

```bash
# Check cpu.weight was set
cat /sys/fs/cgroup/container-web01/cpu.weight
cat /sys/fs/cgroup/container-db01/cpu.weight

# See live CPU usage stats
cat /sys/fs/cgroup/container-web01/cpu.stat

# Monitor all container cgroups in real time
systemd-cgtop
```

---

## 11. Role 5 — Monitoring Dashboard (needs sudo)

**Assigned to:** Monitoring & Testing team member

**OS Concepts:** Metrics collection from cgroups, real-time dashboard, performance validation

### Step 11.1 — Build

```bash
sudo make role5
```

Expected output:

```
gcc -Wall -Wextra -g -I./include -DROLE5_DEMO src/role5_monitoring.c src/role1_architecture.c -o bin/role5_demo -lpthread
Built. Run: sudo ./bin/role5_demo
```

### Step 11.2 — Run

```bash
sudo ./bin/role5_demo
```

### Expected Output

```
=== Container Monitoring Dashboard ===

Spawning test containers...
[container_create] id=mon-test01 state=CREATING
[container_create] id=mon-test01 pid=23456 state=RUNNING
  Container mon-test01 started in 12ms  ✓ (< 500ms target)

┌─────────────────────────────────────────────────────────┐
│  CONTAINER METRICS DASHBOARD                            │
├──────────────┬──────────┬──────────┬────────────────────┤
│ Container ID │  CPU %   │ Mem (MB) │ State              │
├──────────────┼──────────┼──────────┼────────────────────┤
│ mon-test01   │   2.3%   │   4 MB   │ RUNNING            │
├──────────────┼──────────┼──────────┼────────────────────┤
│ TOTALS       │   2.3%   │   4 MB   │ 1 running          │
└──────────────┴──────────┴──────────┴────────────────────┘
Refreshing every 1s... (Ctrl+C to stop)
```

### What This Demonstrates

- **Live metrics**: Reads `cpu.stat` and `memory.current` from each container's cgroup every second and calculates CPU % delta.
- **Startup time test**: Measures wall-clock time from `container_create()` call to the container being in `RUNNING` state. Target is < 500ms.
- **ASCII dashboard**: Terminal UI showing all running containers in a table, refreshed in real time.

---

## 12. Verify What's Running (Viva Commands)

Use these commands during the demo or viva to show live proof of what the code is doing.

### Show a running container's namespaces

```bash
# Replace <PID> with the container's PID printed by role1_demo
sudo ls -la /proc/<PID>/ns/
```

Each namespace (pid, mnt, net, uts, ipc) has a different inode number from the host — proof of isolation.

### Enter a running container's namespaces

```bash
# Start a shell inside the container's PID + mount namespace
sudo nsenter --pid --mount -t <PID> -- ps aux
```

You will see only the container's own processes (PID 1 = the container's init).

### Check memory cgroup limit

```bash
# Shows hard memory cap in bytes (e.g., 268435456 = 256 MB)
cat /sys/fs/cgroup/container-demo01/memory.max

# Shows current memory usage
cat /sys/fs/cgroup/container-demo01/memory.current
```

### Check CPU weight and quota

```bash
# Proportional weight (higher = more CPU when contested)
cat /sys/fs/cgroup/container-demo01/cpu.weight

# Hard quota: "50000 100000" = 50ms per 100ms = 50% of 1 core max
cat /sys/fs/cgroup/container-demo01/cpu.max

# Cumulative CPU usage statistics
cat /sys/fs/cgroup/container-demo01/cpu.stat
```

### Monitor all container cgroups live

```bash
# Real-time cgroup resource usage (like top but for cgroups)
systemd-cgtop
```

### Trace syscalls made by the container process

```bash
# Watch clone(), mount(), unshare() syscalls in real time
sudo strace -p <PID> -e clone,unshare,mount,sethostname
```

### Verify PID namespace isolation

```bash
# Inside the container: should show only 1–2 processes
sudo nsenter --pid --mount -t <PID> -- ps aux

# On the host: shows all host processes (many more)
ps aux | wc -l
```

---

## 13. Troubleshooting

### "Permission denied" when running a role

```
clone: Operation not permitted
```

**Fix:** Run with `sudo`. Roles 1, 2, 3, 5 all need root:

```bash
sudo ./bin/role1_demo
```

### "No such file or directory: src/role1_architecture.c"

**Fix:** You skipped the setup step. Run:

```bash
make setup
```

Then rebuild.

### "cgroup_write: open(/sys/fs/cgroup/...): No such file or directory"

**Cause:** cgroups v2 is not mounted, or you're running as non-root.

**Fix:**

```bash
# Verify cgroups v2 is active
mount | grep cgroup2

# If not mounted, try:
sudo mount -t cgroup2 none /sys/fs/cgroup

# Always run roles 2/3/5 with sudo:
sudo ./bin/role2_demo
```

### "overlay: failed to mount" or "mount overlay failed: Operation not permitted"

**Cause:** Overlay filesystem requires root and kernel support.

**Fix:** Make sure you're using `sudo` and your kernel supports overlay:

```bash
grep overlay /proc/filesystems
```

Expected output: `nodev overlay`

If not listed, install the module:

```bash
sudo modprobe overlay
```

### Build error: "container.h: No such file or directory"

**Fix:**

```bash
make setup   # creates include/container.h
make role4   # then rebuild
```

### "undefined reference to `pthread_mutex_lock`"

**Fix:** This means `-lpthread` was dropped. Always use `make roleX` — do not compile manually without `-lpthread`.

### Role 1 demo exits immediately without showing a shell

This is **expected behaviour**. The demo creates a container, waits 3 seconds, then stops it. It is not meant to give you an interactive shell. The important thing to observe is the output lines showing namespace creation and process state transitions.

### "clone: Invalid argument" on WSL (Windows Subsystem for Linux)

WSL 1 does not support Linux namespaces. **Use native Ubuntu** (bare metal, VirtualBox, or VMware). WSL 2 may work for some roles but cgroups v2 support is limited.

---

## 14. OS Syllabus Coverage

| Unit | Topic | Role | Where in Code |
|------|-------|------|---------------|
| Unit II | Process creation, `fork`/`exec` | Role 1 | `container_create()` using `clone()` |
| Unit II | Process states & transitions | Role 1 | `container_state_t` enum, state machine |
| Unit II | Zombie prevention | Role 1 | `sigchld_handler()` with `waitpid(-1, WNOHANG)` |
| Unit II | Semaphores, mutex | Role 4 | `sem_init()`, `pthread_mutex_t bankers_lock` |
| Unit II | Classical sync problems | Role 4 | Shared resource mutual exclusion demo |
| Unit III | Round Robin scheduling | Role 3 | `weighted_rr_schedule()` in scheduler demo |
| Unit III | Priority scheduling | Role 3 | `cpu.weight` proportional shares |
| Unit III | Scheduling criteria (fairness) | Role 3 | ±5% fairness verification |
| Unit IV | Deadlock conditions | Role 4 | Banker's state matrix (max, allocation, need) |
| Unit IV | Deadlock avoidance — Banker's | Role 4 | `is_safe_state()`, `bankers_request()` |
| Unit IV | Resource allocation graph | Role 4 | `print_resource_graph()` |
| Unit V | Memory limits, virtual memory | Role 2 | `memory.max`, `memory.high` cgroup controls |
| Unit V | OOM killer | Role 2 | `memory.oom.group = 1` |
| Unit VI | Filesystem isolation | Role 2 | `setup_overlay_fs()` copy-on-write layers |
| Unit VI | Directory management | Role 2 | `chroot`, `/proc`, `tmpfs` mounts |

---

## Quick Reference — All Commands

```bash
# One-time setup (run once after cloning)
make setup

# Role 4 — No sudo needed (run this first to verify your setup)
make role4
./bin/role4_demo

# Role 1 — Namespaces (sudo required)
sudo make role1
sudo ./bin/role1_demo

# Role 2 — Memory & Storage (sudo required)
sudo make role2
sudo ./bin/role2_demo

# Role 3 — CPU Scheduler (sudo required)
sudo make role3
sudo ./bin/role3_demo

# Role 5 — Monitoring (sudo required)
sudo make role5
sudo ./bin/role5_demo

# Clean all compiled binaries
make clean
```

---

*Built for ML2011 Operating Systems — VIT Pune 2025-26*
