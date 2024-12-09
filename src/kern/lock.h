// lock.h - A sleep lock
//

#ifdef LOCK_TRACE
#define TRACE
#endif

#ifdef LOCK_DEBUG
#define DEBUG
#endif

#ifndef _LOCK_H_
#define _LOCK_H_

#include "thread.h"
#include "halt.h"
#include "console.h"

struct lock {
    struct condition cond;
    int tid; // thread holding lock or -1
};

static inline void lock_init(struct lock * lk, const char * name);
static inline void lock_acquire(struct lock * lk);
static inline void lock_release(struct lock * lk);

// INLINE FUNCTION DEFINITIONS
//

static inline void lock_init(struct lock * lk, const char * name) {
    trace("%s(<%s:%p>", __func__, name, lk);
    condition_init(&lk->cond, name);
    lk->tid = -1;
}

/**
 * Name: lock_acquire
 *
 * Inputs:
 *  struct lock * lk - Pointer to the lock structure to acquire.
 *
 * Outputs:
 *  void 
 *
 * Purpose:
 *  Acquires sleep lock by waiting until it becomes available. The lock is tied to the thread
 *  that successfully acquires it.
 *
 * Side effects:
 *  Suspends the calling thread if lock in use. If lock available, update lock's state. 
 */
static inline void lock_acquire(struct lock * lk) {
    // TODO CP3: FIXME implement this
    // then use in vioblk and kfs drivers
    while(lk->tid >= 0){
        kprintf("Thread <%s:%d> awaiting lock <%s:%p>",
            thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
        condition_wait(&lk->cond);
    }

    kprintf("Thread <%s:%d> acquired lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
    lk->tid = running_thread();
}

static inline void lock_release(struct lock * lk) {
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);

    assert (lk->tid == running_thread());
    
    lk->tid = -1;
    condition_broadcast(&lk->cond);

    kprintf("Thread <%s:%d> released lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
    debug("Thread <%s:%d> released lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
}

#endif // _LOCK_H_