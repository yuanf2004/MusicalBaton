#ifndef TEST_NAME_KERNEL_H
#define TEST_NAME_KERNEL_H
#include <stddef.h>
#define BUILD_ASSERT(expr) _Static_assert(expr, #expr)
#define ARG_UNUSED(x) (void)(x)
#define K_FOREVER (-1)
#define K_MUTEX_DEFINE(name) struct k_mutex name
#define K_THREAD_STACK_DEFINE(name, size) char name[size]
#define K_THREAD_STACK_SIZEOF(stack) sizeof(stack)
#define K_WORK_DEFINE(name, callback) struct k_work name = {.handler = callback}
struct k_mutex { int unused; };
struct k_work { void (*handler)(struct k_work *); };
struct k_work_q { int unused; };
static inline void k_mutex_lock(struct k_mutex *m, int t) { (void)m; (void)t; }
static inline void k_mutex_unlock(struct k_mutex *m) { (void)m; }
static inline void k_work_queue_start(struct k_work_q *q, char *s, size_t n, int p, void *c)
{ (void)q; (void)s; (void)n; (void)p; (void)c; }
int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w);
#endif
