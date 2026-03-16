# Container Orchestration System
## ML2011 Operating Systems — VIT Pune 2025-26

### Project Structure
```
container_system/
├── include/
│   └── container.h          ← Shared data structures (ALL roles use this)
├── src/
│   ├── role1_architecture.c ← Container lifecycle, clone(), namespaces
│   ├── role2_memory_storage.c ← cgroups memory, overlay FS, chroot
│   ├── role3_scheduler.c    ← CPU shares, weighted RR scheduler
│   ├── role4_security_sync.c ← Banker's algorithm, semaphores, IPC
│   └── role5_monitoring.c   ← Metrics dashboard, test suite
├── Makefile
└── README.md
```

### Quick Start (Linux only, needs sudo)
```bash
# Build a specific role demo
make role4          # No sudo needed for Banker's demo
sudo make role1     # Needs root for namespaces
sudo make role2     # Needs root for cgroups + mounts
sudo make role3     # Needs root for cgroups
sudo make role5     # Needs root for container creation

# Run
sudo ./bin/role1_demo
./bin/role4_demo
```

### What each file implements (40% of each role)

| File | What's done (40%) | What's left (60%) |
|------|-------------------|-------------------|
| role1 | clone() with flags, lifecycle states, stop/kill, zombie prevention | REST API, rootfs integration, namespace FD tracking |
| role2 | memory.max/high cgroup, overlay FS mount, /proc remount, tmpfs | Storage quotas, pivot_root, full teardown |
| role3 | cpu.weight + cpu.max cgroup, weighted RR scheduler, fairness table | Dynamic rebalancing, multi-level scheduling |
| role4 | Full Banker's algorithm, POSIX semaphores, request/release/safety | seccomp filters, IPC namespace creation, resource graphs |
| role5 | Live metrics dashboard, test suite skeleton, CPU% delta calculation | Grafana-style UI, stress-ng integration, CI test runner |

### OS Syllabus Coverage

| Unit | Topic | Role |
|------|-------|------|
| Unit II | Process creation, states, zombie prevention | Role 1 |
| Unit II | Semaphores, mutex, classical sync problems | Role 4 |
| Unit III | CPU scheduling (RR, Priority/shares) | Role 3 |
| Unit IV | Deadlock avoidance — Banker's algorithm | Role 4 |
| Unit V | Memory limits, virtual memory, OOM | Role 2 |
| Unit VI | Filesystem isolation, directories, quotas | Role 2 |

### Claude Code Prompts to Complete Remaining 60%

**Role 1 — Add the REST API:**
```
Read src/role1_architecture.c and include/container.h.
Add a simple HTTP API using Python (Flask) that wraps the C functions.
Endpoints: POST /containers/create, GET /containers/{id}, DELETE /containers/{id}.
The Python layer calls the C binary as a subprocess and parses JSON output.
```

**Role 2 — Add pivot_root and storage quotas:**
```
Read src/role2_memory_storage.c. Implement setup_pivot_root() that:
1. Creates a new root inside the container's merged overlay dir
2. Calls pivot_root() to switch the container's root filesystem
3. Unmounts the old root with umount2(old_root, MNT_DETACH)
Also add a storage quota: write to /sys/fs/cgroup/container-{id}/io.max
```

**Role 3 — Add dynamic rebalancing:**
```
Read src/role3_scheduler.c. Add a rebalancer that runs every 5 seconds:
1. Reads actual CPU usage from all container cgroups
2. If a container is consistently below 50% of its allocation, reduce its weight
3. If a container is hitting its cpu.max throttle, log a warning
4. Print a rebalance report showing before/after weights
```

**Role 4 — Add seccomp filter:**
```
Read src/role4_security_sync.c and include/container.h.
Implement apply_seccomp_filter() using libseccomp that:
1. Creates a seccomp context defaulting to SCMP_ACT_ERRNO (deny all)
2. Whitelists safe syscalls: read, write, exit, exit_group, brk, mmap, etc.
3. Explicitly blocks dangerous ones: ptrace, reboot, mount, kexec_load
4. Loads the filter with seccomp_load()
Compile with: gcc -lseccomp
```

**Role 5 — Add stress test runner:**
```
Read src/role5_monitoring.c. Add a load_test(int num_containers) function:
1. Creates num_containers containers in parallel using fork()
2. Records the timestamp before and after each creation
3. Runs stress-ng --cpu 1 --timeout 10s inside each container
4. After 10s, reads cpu.stat for each and computes actual CPU %
5. Compares CPU % to configured shares ratio
6. Prints pass/fail for the ±5% fairness target
```

### Key Commands for Demo/Viva

```bash
# See namespaces of a running container process
ls -la /proc/<PID>/ns/

# Check cgroup memory limit
cat /sys/fs/cgroup/container-demo01/memory.max

# Check CPU weight
cat /sys/fs/cgroup/container-demo01/cpu.weight

# See CPU usage stats
cat /sys/fs/cgroup/container-demo01/cpu.stat

# Monitor all container cgroups live
systemd-cgtop

# Trace syscalls made by container process
sudo strace -p <PID> -e clone,unshare,mount

# Verify PID namespace isolation
sudo nsenter --pid --mount -t <PID> -- ps aux
```

### Expected Output — Role 4 (Banker's)
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
```
