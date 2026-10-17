/* Kernel event flags: a 64-bit pattern that threads wait on (all or any of a set of bits), with
 * optional clearing on wake. Follows shadPS4's event_flag.cpp. Thread-priority queues are
 * served FIFO, as with semaphores (runtime_sema.c): host threads have no PS4 priorities. */
#define _GNU_SOURCE
#include "runtime.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define EPERM_ 1
#define ESRCH_ 3
#define EACCES_ 13
#define EINVAL_ 22
#define ETIMEDOUT_ 60
#define ENAMETOOLONG_ 63
#define ECANCELED_ 85
enum { WAIT_AND = 0x01, WAIT_OR = 0x02, CLEAR_ALL = 0x10, CLEAR_PAT = 0x20 };
typedef struct Waiter {
    uint64_t bits, result;
    uint32_t mode;
    int32_t status; /* 1 while waiting; then 0 or an error */
    pthread_cond_t event;
    struct Waiter *next;
} Waiter;
typedef struct EventFlag {
    uint64_t pattern;
    int multi, deleted;
    unsigned traced; /* blocking waits reported (BB_TRACE_EVF) */
    unsigned active;
    Waiter *first;
    char name[32];
} EventFlag;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static size_t created, deleted, waits, sets;
#ifdef _WIN32
#define WAIT_CLOCK CLOCK_REALTIME
#else
#define WAIT_CLOCK CLOCK_MONOTONIC
#endif
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(WAIT_CLOCK, &t);
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}
static int met(uint64_t pattern, uint64_t bits, uint32_t mode) {
    return (mode & 0xf) == WAIT_AND ? (pattern & bits) == bits : (pattern & bits) != 0;
}
static void clear_for(EventFlag *f, uint64_t bits, uint32_t mode) {
    if ((mode & 0xf0) == CLEAR_ALL) f->pattern = 0;
    else if ((mode & 0xf0) == CLEAR_PAT) f->pattern &= ~bits;
}
static void unlink_waiter(EventFlag *f, Waiter *w) {
    for (Waiter **p = &f->first; *p; p = &(*p)->next)
        if (*p == w) { *p = w->next; return; }
}
static ABI int32_t evf_create(EventFlag **out, const char *name, uint32_t attr, uint64_t pattern,
                              const void *options) {
    if (!out || !name || options || attr > 0x22 || (attr & 0xf) > 2 || ((attr & 0xf0) && (attr & 0xf0) != 0x10 && (attr & 0xf0) != 0x20))
        return ERR(EINVAL_);
    if (strlen(name) >= 32) return ERR(ENAMETOOLONG_);
    EventFlag *f = calloc(1, sizeof(*f));
    if (!f) return ERR(12);
    f->pattern = pattern;
    f->multi = (attr & 0xf0) == 0x20;
    strcpy(f->name, name);
    pthread_mutex_lock(&lock);
    ++created;
    pthread_mutex_unlock(&lock);
    *out = f;
    return 0;
}
static void wake_all(EventFlag *f, int32_t status) {
    while (f->first) {
        Waiter *w = f->first;
        f->first = w->next;
        w->result = f->pattern;
        w->status = status;
        pthread_cond_signal(&w->event);
    }
}
static ABI int32_t evf_delete(EventFlag *f) {
    if (!f) return ERR(ESRCH_);
    pthread_mutex_lock(&lock);
    f->deleted = 1;
    ++deleted;
    wake_all(f, ERR(EACCES_));
    int idle = !f->active;
    pthread_mutex_unlock(&lock);
    if (idle) free(f);
    return 0;
}
static ABI int32_t evf_set(EventFlag *f, uint64_t bits) {
    if (!f) return ERR(ESRCH_);
    pthread_mutex_lock(&lock);
    f->pattern |= bits;
    ++sets;
    for (Waiter **p = &f->first; *p;) {
        Waiter *w = *p;
        if (!met(f->pattern, w->bits, w->mode)) { p = &w->next; continue; }
        w->result = f->pattern;
        clear_for(f, w->bits, w->mode);
        w->status = 0;
        *p = w->next;
        pthread_cond_signal(&w->event);
    }
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t evf_clear(EventFlag *f, uint64_t keep) {
    if (!f) return ERR(ESRCH_);
    pthread_mutex_lock(&lock);
    f->pattern &= keep;
    pthread_mutex_unlock(&lock);
    return 0;
}
static int32_t wait_flag(EventFlag *f, uint64_t bits, uint32_t mode, uint64_t *result, uint32_t *timeout, int block) {
    if (!f) return ERR(ESRCH_);
    if (!bits || ((mode & 0xf) != WAIT_AND && (mode & 0xf) != WAIT_OR) ||
        ((mode & 0xf0) && (mode & 0xf0) != CLEAR_ALL && (mode & 0xf0) != CLEAR_PAT))
        return ERR(EINVAL_);
    pthread_mutex_lock(&lock);
    int32_t status;
    if (f->deleted) status = ERR(EACCES_);
    else if (!f->multi && f->first) status = ERR(EPERM_);
    else if (met(f->pattern, bits, mode)) {
        if (result) *result = f->pattern;
        clear_for(f, bits, mode);
        status = 0;
    } else if (!block) {
        if (result) *result = f->pattern;
        status = ERR(16); /* EBUSY */
    } else if (timeout && !*timeout) {
        if (result) *result = f->pattern;
        status = ERR(ETIMEDOUT_);
    } else {
        Waiter w = {.bits = bits, .mode = mode, .status = 1};
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, WAIT_CLOCK);
        pthread_cond_init(&w.event, &attr);
        pthread_condattr_destroy(&attr);
        Waiter **tail = &f->first;
        while (*tail) tail = &(*tail)->next;
        *tail = &w;
        ++f->active;
        ++waits;
        if (getenv("BB_TRACE_EVF")) { /* blocking waits, for diagnosis */
            ++f->traced;
            printf("Runtime: event flag \"%s\" wait bits=%#llx mode=%#x pattern=%#llx timeout=%s%u us\n", f->name,
                   (unsigned long long)bits, mode, (unsigned long long)f->pattern, timeout ? "" : "none ",
                   timeout ? *timeout : 0);
        }
        uint64_t deadline = timeout ? now_ns() + (uint64_t)*timeout * 1000 : 0;
        struct timespec end = {.tv_sec = (time_t)(deadline / 1000000000), .tv_nsec = (long)(deadline % 1000000000)};
        while (w.status == 1) {
            int e = timeout ? pthread_cond_timedwait(&w.event, &lock, &end) : pthread_cond_wait(&w.event, &lock);
            if (e == ETIMEDOUT && w.status == 1) {
                unlink_waiter(f, &w);
                w.result = f->pattern;
                w.status = ERR(ETIMEDOUT_);
            }
        }
        status = w.status;
        if (result) *result = w.result;
        if (timeout) {
            uint64_t now = now_ns();
            *timeout = status ? 0 : (uint32_t)(now >= deadline ? 0 : (deadline - now) / 1000);
        }
        pthread_cond_destroy(&w.event);
        if (!--f->active && f->deleted) { pthread_mutex_unlock(&lock); free(f); return status; }
    }
    pthread_mutex_unlock(&lock);
    return status;
}
static ABI int32_t evf_wait(EventFlag *f, uint64_t bits, uint32_t mode, uint64_t *result, uint32_t *timeout) {
    return wait_flag(f, bits, mode, result, timeout, 1);
}
static ABI int32_t evf_poll(EventFlag *f, uint64_t bits, uint32_t mode, uint64_t *result) {
    return wait_flag(f, bits, mode, result, NULL, 0);
}
static ABI int32_t evf_cancel(EventFlag *f, uint64_t pattern, int32_t *waiters) {
    if (!f) return ERR(ESRCH_);
    pthread_mutex_lock(&lock);
    int32_t n = 0;
    for (Waiter *w = f->first; w; w = w->next) ++n;
    if (waiters) *waiters = n;
    f->pattern = pattern;
    wake_all(f, ERR(ECANCELED_));
    pthread_mutex_unlock(&lock);
    return 0;
}
static const RuntimeExport exports[] = {
    {"sceKernelCreateEventFlag", evf_create}, {"sceKernelDeleteEventFlag", evf_delete},
    {"sceKernelSetEventFlag", evf_set},       {"sceKernelClearEventFlag", evf_clear},
    {"sceKernelWaitEventFlag", evf_wait},     {"sceKernelPollEventFlag", evf_poll},
    {"sceKernelCancelEventFlag", evf_cancel},
};
uintptr_t runtime_evf_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
void runtime_evf_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: event flags created=%zu, deleted=%zu, blocking waits=%zu, sets=%zu\n", created, deleted, waits, sets);
    pthread_mutex_unlock(&lock);
}
