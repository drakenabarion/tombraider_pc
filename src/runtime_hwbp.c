/* bbport diagnosis: BB_HWBP_WATCH=<hex address>[,...] (up to 4) — hardware data watchpoints (writes, 8 bytes,
 * perf_event, user space only, delivered as a synchronous SIGTRAP) on that guest address in every
 * guest thread. Each hit prints the thread, the instruction after the access (as an eboot.elf
 * address: the game image is loaded 0x600000 above its link addresses), the value there and the
 * game code addresses found on the stack (Tomb Raider: who reads the occlusion query results). */
#define _GNU_SOURCE
#include "runtime.h"
#ifdef __linux__
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <pthread.h>

#define IMAGE_SHIFT 0x600000UL
#define CODE_LO (0x400000UL + IMAGE_SHIFT)
#define CODE_HI (0x166dc48UL + IMAGE_SHIFT)

static uintptr_t watch_list[4];
static int watch_count = -1;
static int watch_init(void) {
    if (watch_count < 0) {
        watch_count = 0;
        for (const char *p = getenv("BB_HWBP_WATCH"); p && *p && watch_count < 4;) {
            char *end;
            watch_list[watch_count++] = (uintptr_t)strtoull(p, &end, 16);
            p = *end == ',' ? end + 1 : end;
            if (end == p && *p) break;
        }
    }
    return watch_count;
}

static volatile int hwbp_lines;
static __thread long hwbp_fd[4] = {-1, -1, -1, -1};

static void hwbp_trap(int sig, siginfo_t *info, void *context) {
    (void)sig;
    if (hwbp_lines > 3000) return;
    ucontext_t *uc = context;
    const uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    /* The first 3 hits of each instruction (a hot reader would fill the log alone), and every
     * hit whose first floats look like a motion-blur reprojection row (1..2.5, .., .., |t|>300). */
    const float *peek4 = (const float *)(uintptr_t)info->si_addr;
    const int matrix_like = peek4[0] > 1.0f && peek4[0] < 2.5f && (peek4[3] > 300.0f || peek4[3] < -300.0f);
    if (!matrix_like) {
        static uintptr_t seen_rip[1024];
        static unsigned char seen_count[1024];
        size_t slot = (rip * 0x9E3779B97F4A7C15ull) >> 54;
        for (int probe = 0; probe < 1024; ++probe, slot = (slot + 1) & 1023) {
            if (seen_rip[slot] == rip || !seen_rip[slot]) break;
        }
        if (seen_rip[slot] && seen_rip[slot] != rip) return;
        seen_rip[slot] = rip;
        if (seen_count[slot] >= 3) return;
        ++seen_count[slot];
    }
    ++hwbp_lines;
    const uintptr_t rsp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    const uintptr_t addr = (uintptr_t)info->si_addr;
    char name[17] = {0};
    prctl(PR_GET_NAME, name, 0, 0, 0);
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "HwbpWatch: tid %ld '%s' after 0x%lx%s value 0x%llx stack:",
                     (long)syscall(SYS_gettid), name,
                     (unsigned long)(rip >= CODE_LO && rip < CODE_HI ? rip - IMAGE_SHIFT : rip),
                     rip >= CODE_LO && rip < CODE_HI ? "" : "(host)",
                     (unsigned long long)*(const volatile uint64_t *)addr);
    const uintptr_t *sp = (const uintptr_t *)rsp;
    int found = 0;
    for (int i = 0; i < 512 && found < 8 && n < (int)sizeof(buf) - 32; ++i) {
        if (sp[i] >= CODE_LO && sp[i] < CODE_HI) {
            n += snprintf(buf + n, sizeof(buf) - n, " 0x%lx", (unsigned long)(sp[i] - IMAGE_SHIFT));
            ++found;
        }
    }
    const float *fv = (const float *)addr;
    n += snprintf(buf + n, sizeof(buf) - n, " | at %#lx floats %g %g %g %g\n", (unsigned long)addr,
                  fv[0], fv[1], fv[2], fv[3]);
    (void)!write(1, buf, n);
}

/* Guest thread ids, for watchpoints armed later from another thread (runtime_hwbp_arm). */
static volatile long thread_ids[256];
static volatile int thread_count;
static long armed_fds[256];
static int armed_count;
static pthread_mutex_t arm_lock = PTHREAD_MUTEX_INITIALIZER;

static void install_trap(void) {
    static int installed;
    if (installed) return;
    installed = 1;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = hwbp_trap;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTRAP, &sa, NULL);
}

/* A write watchpoint (8 bytes) on `address` in every guest thread started so far, replacing the
 * previous one (BB_TRACE_FIND: the slot the traced constants were written to). */
static void arm_one(uintptr_t address);
void runtime_hwbp_arm_list(const uintptr_t *addresses, int count) {
    pthread_mutex_lock(&arm_lock);
    install_trap();
    for (int i = 0; i < armed_count; ++i) close((int)armed_fds[i]);
    armed_count = 0;
    for (int a = 0; a < count && a < 4; ++a) arm_one(addresses[a]);
    printf("HwbpWatch: armed %d addresses in %d threads\n", count < 4 ? count : 4, thread_count);
    pthread_mutex_unlock(&arm_lock);
}
static void arm_one(uintptr_t address) {
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.size = sizeof(attr);
    /* BB_HWBP_READS=1: reads too (who uses a value, not only who writes it). */
    attr.bp_type = getenv("BB_HWBP_READS") ? HW_BREAKPOINT_RW : HW_BREAKPOINT_W;
    attr.bp_addr = address;
    attr.bp_len = HW_BREAKPOINT_LEN_8;
    attr.sample_period = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.sigtrap = 1;
    attr.remove_on_exec = 1;
    const int n = thread_count;
    for (int i = 0; i < n && armed_count < 256; ++i) {
        const long fd = syscall(SYS_perf_event_open, &attr, (pid_t)thread_ids[i], -1, -1, PERF_FLAG_FD_CLOEXEC);
        if (fd >= 0) armed_fds[armed_count++] = fd;
    }
}
void runtime_hwbp_arm(uintptr_t address) {
    pthread_mutex_lock(&arm_lock);
    install_trap();
    for (int i = 0; i < armed_count; ++i) close((int)armed_fds[i]);
    armed_count = 0;
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.size = sizeof(attr);
    attr.bp_type = getenv("BB_HWBP_READS") ? HW_BREAKPOINT_RW : HW_BREAKPOINT_W;
    attr.bp_addr = address;
    attr.bp_len = HW_BREAKPOINT_LEN_8;
    attr.sample_period = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.sigtrap = 1;
    attr.remove_on_exec = 1;
    const int n = thread_count;
    for (int i = 0; i < n && armed_count < 256; ++i) {
        const long fd = syscall(SYS_perf_event_open, &attr, (pid_t)thread_ids[i], -1, -1, PERF_FLAG_FD_CLOEXEC);
        if (fd >= 0) armed_fds[armed_count++] = fd;
    }
    printf("HwbpWatch: armed %#lx in %d threads\n", (unsigned long)address, armed_count);
    pthread_mutex_unlock(&arm_lock);
}

void runtime_hwbp_thread_start(void) {
    {
        const int i = __atomic_fetch_add(&thread_count, 1, __ATOMIC_RELAXED);
        if (i < 256) thread_ids[i] = syscall(SYS_gettid);
    }
    if (!watch_init()) return;
    static int installed;
    if (!installed) {
        installed = 1;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = hwbp_trap;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTRAP, &sa, NULL);
    }
    for (int w = 0; w < watch_count; ++w) {
    const uintptr_t addr = watch_list[w];
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.size = sizeof(attr);
    attr.bp_type = getenv("BB_HWBP_READS") ? HW_BREAKPOINT_RW : HW_BREAKPOINT_W;
    attr.bp_addr = addr;
    attr.bp_len = HW_BREAKPOINT_LEN_8;
    attr.sample_period = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.sigtrap = 1;
    attr.remove_on_exec = 1;
    hwbp_fd[w] = syscall(SYS_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    static volatile int reported;
    if (hwbp_fd[w] < 0 && !reported) {
        reported = 1;
        perror("HwbpWatch: perf_event_open");
    }
    }
}

void runtime_hwbp_thread_end(void) {
    for (int w = 0; w < 4; ++w) {
        if (hwbp_fd[w] >= 0) {
            close((int)hwbp_fd[w]);
            hwbp_fd[w] = -1;
        }
    }
}
#else
void runtime_hwbp_arm(uintptr_t address) { (void)address; }
void runtime_hwbp_arm_list(const uintptr_t *addresses, int count) { (void)addresses; (void)count; }
void runtime_hwbp_thread_start(void) {}
void runtime_hwbp_thread_end(void) {}
#endif
