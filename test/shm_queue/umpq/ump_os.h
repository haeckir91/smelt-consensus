#ifndef __UMP_OS_H
#define __UMP_OS_H

/**
 * AFAICS, the requirements for the wakeup/sleep code are that notifications are
 * remembered [see also discussion on barrelfish list].
 *
 * This is due to ump_chan_tx_submit_unchecked() doing:
 *
 *   if (wake_state != UMP_RUNNING) {
 *       // we shouldn't be asleep if we're sending!
 *       assert(wake_state == c->discriminant ? UMP_WAIT_1 : UMP_WAIT_0);
 *       ump_wake_peer(c->peer_wake_context);
 *   }
 *
 * And a race between unsetting UMP_RUNNING and actually sleeping on the other
 * end
 *          -AKK
 */

/**
 * There are two implementations for linux (see below):
 *  - one using pthread conditional variables (safe)
 *  - one using bare futexes (seems to work)
 */
#define UMP_OS_USE_PTHREAD_SAFE

#ifdef _WIN32
#include <windows.h>

/*
 * ump_sleep() and ump_wake_peer() are an attempt to abstract Win32 sleep/wakeup
 * events, but on Barrelfish this would be part of the waitset, and on other
 * OSes it may differ entirely.
 */
ALWAYS_INLINE void ump_wake_context_setup(void **evt_ptr)
{
    // create event objects for blocking and wakeup
    // each is an auto-reset event, initially unsignalled
    HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(event != NULL);
    *evt_ptr = event;
}

ALWAYS_INLINE void ump_sleep(void *evt)
{
    DWORD ret = WaitForSingleObject(evt, INFINITE);
    assert(ret == WAIT_OBJECT_0);
}

ALWAYS_INLINE void ump_wake_peer(void *evt)
{
    BOOL ret = SetEvent(evt);
    assert(ret);
}

#elif (defined(__linux__) && defined(UMP_OS_USE_PTHREAD_SAFE))
// pthreads (safe) version



#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

struct wake_context {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    volatile int wakeup;
};

static inline void
ump_wake_context_setup(void **ctx_ptr)
{
    struct wake_context *ctx;

    if ((ctx = (struct wake_context*) malloc(sizeof(*ctx))) == NULL) {
        perror("malloc");
        abort();
    }

    if (pthread_mutex_init(&ctx->mutex, NULL)) {
        perror("pthread_mutex_init()");
        abort();
    }

    if (pthread_cond_init(&ctx->cond, NULL)) {
        perror("pthread_cond_init()");
        abort();
    }

    ctx->wakeup = 0;

    *ctx_ptr = ctx;
}

static inline void
ump_wake_context_destroy(void *ctx)
{
    free(ctx);
}

static inline void
ump_sleep(void *arg)
{
    struct wake_context *ctx = (struct wake_context*) arg;
    pthread_mutex_lock(&ctx->mutex);
    while (!ctx->wakeup)
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
    ctx->wakeup = 0;
    pthread_mutex_unlock(&ctx->mutex);
}

static inline void
ump_wake_peer(void *arg)
{
    struct wake_context *ctx = (struct wake_context*) arg;
    pthread_mutex_lock(&ctx->mutex);
    ctx->wakeup = 1;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
}

#elif defined(__linux__)

// because notifications need to be remembered, the wakeup mechanism needs to
// to maintain state (essentially a single bit).
//
// We use futexes as the more lightweight option.
//
// Here are some alternatives considered:
//  - pthread_kill(): does not maintain state
//  - a pipe(2)/fifo: should work OK -- sleeper needs to empty pipe to reset
//                    state to avoid spurious wakeups
//  - semaphores: difficult to avoid spurious wakeups caused by up() called more
//                than once
//
//
// we don't try to be smart and avoid unnecessary wakeups -- UMP does that already

#include "debug.h"

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#ifndef UMP_OS_USE_PTHREAD_SAFE
#include <errno.h> // EWOULDBLOCK
#endif
static long
sys_futex(void *addr1, int op, int val1, struct timespec *timeout, void *addr2, int val3)
{
    return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

enum {
    UMP_WAKE_FLAG_OFF = 0,
    UMP_WAKE_FLAG_ON  = 1
};

static inline void
ump_wake_context_setup(void **ctx_ptr)
{
    volatile int *flag;
    flag = *ctx_ptr = malloc(sizeof(int));
    if (!flag) {
        perror("malloc");
        abort();
    }
    *flag = UMP_WAKE_FLAG_OFF;
}

static inline void
ump_wake_context_destroy(void *ctx)
{
    free(ctx);
}

static inline void
ump_sleep(void *ctx)
{
    volatile int *flag = ctx;
    int __attribute__((unused)) ret;

    ret = sys_futex((int *)flag, FUTEX_WAIT, UMP_WAKE_FLAG_OFF, NULL, NULL, 0);
    // ret == 0           -> we slept and got woken up
    // ret == EWOULDBLOCK -> wake_peer() set the flag before we blocked
    assert(ret == 0 || errno == EWOULDBLOCK);
    assert(*flag == UMP_WAKE_FLAG_ON);
    *flag = UMP_WAKE_FLAG_OFF;
}

static inline void
ump_wake_peer(void *ctx)
{
    volatile int *flag = ctx;
    *flag = UMP_WAKE_FLAG_ON;
    sys_futex((int *)flag, FUTEX_WAKE, 1, NULL, NULL, 0);
}
#elif defined(BARRELFISH)
/* TODO */
static inline void
ump_wake_peer(void *ctx)
{

}

static inline void
ump_wake_context_setup(void **ctx_ptr)
{

}

static inline void
ump_sleep(void *ctx)
{

}
#else
#error "TODO: OS wakeup/sleep support"
#endif

#endif
