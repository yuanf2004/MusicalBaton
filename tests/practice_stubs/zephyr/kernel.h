#pragma once
#include <stdint.h>
#define K_MUTEX_DEFINE(name) int name
#define K_FOREVER 0
extern int64_t test_now;
static inline int64_t k_uptime_get(void) { return test_now; }
static inline void k_mutex_lock(int *lock, int wait) { (void)lock; (void)wait; }
static inline void k_mutex_unlock(int *lock) { (void)lock; }
