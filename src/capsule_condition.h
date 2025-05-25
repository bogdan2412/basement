#pragma once

#include "config.h"

/* Condition variable implementation via futex, mirroring OCaml stdlib.

   The implementation is copied from the OCaml runtime to avoid this bug:
   https://sourceware.org/bugzilla/show_bug.cgi?id=25847
*/

#ifdef CAML_INTERNALS

#ifdef PLATFORM_LINUX

#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "caml/mlvalues.h"
#include "caml/memory.h"

#include "capsule_mutex.h"

typedef struct {
  _Atomic uint64_t counter;
} * capsule_condition;
#define Condition_val(v) (*((capsule_condition *)Data_custom_val(v)))

#define CONDITION_SUCCESS 0

Caml_inline int capsule_condition_signal(capsule_condition cond) {
  atomic_fetch_add(&cond->counter, 1);
  if (syscall(SYS_futex, &cond->counter, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0) == -1) {
    return errno;
  }
  return CONDITION_SUCCESS;
}

Caml_inline int capsule_condition_broadcast(capsule_condition cond) {
  atomic_fetch_add(&cond->counter, 1);
  if (syscall(SYS_futex, &cond->counter, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0) ==
      -1) {
    return errno;
  }
  return CONDITION_SUCCESS;
}

Caml_inline int capsule_condition_wait(capsule_condition cond, capsule_mutex mut) {
  // Save and restore owner, as the current fiber may be a descendant.
  uint64_t old_count = atomic_load(&cond->counter);
  fiber_t owner = atomic_load_explicit(&mut->owner, memory_order_relaxed);

  // If the mutex operations fail, the condition is in an inconsistent state and it's
  // not safe to return to OCaml.
  if (capsule_mutex_unlock(mut) != MUTEX_SUCCESS) {
    caml_fatal_error("Failed to release mutex.");
  }

  caml_enter_blocking_section();
  int rc = syscall(SYS_futex, &cond->counter, FUTEX_WAIT_PRIVATE, old_count,
                   NULL, NULL, 0);
  // Re-aquire the domain lock to avoid blocking other domains on our systhreads
  caml_leave_blocking_section();

  if (capsule_mutex_lock(mut) != MUTEX_SUCCESS) {
    caml_fatal_error("Failed to re-acquire mutex.");
  }
  atomic_store_explicit(&mut->owner, owner, memory_order_relaxed);

  if (rc == -1 && errno != EAGAIN) {
    return errno;
  }
  return CONDITION_SUCCESS;
}

Caml_inline int capsule_condition_create(capsule_condition *res) {
  capsule_condition cond = caml_stat_alloc_noexc(sizeof(*cond));
  if (cond == NULL) {
    return ENOMEM;
  }
  atomic_store(&cond->counter, 0);
  *res = cond;
  return CONDITION_SUCCESS;
}

Caml_inline int capsule_condition_destroy(capsule_condition c) {
  caml_stat_free(c);
  return CONDITION_SUCCESS;
}

#else /* PLATFORM_LINUX */

typedef caml_plat_cond * capsule_condition;
#define Condition_val(v) (* (capsule_condition *) Data_custom_val(v))

Caml_inline int capsule_condition_create(capsule_condition * res)
{
  int rc;
  capsule_condition c = caml_stat_alloc_noexc(sizeof(pthread_cond_t));
  if (c == NULL) return ENOMEM;
  rc = pthread_cond_init(c, NULL);
  if (rc != 0) { caml_stat_free(c); return rc; }
  *res = c;
  return 0;
}

Caml_inline int capsule_condition_destroy(capsule_condition c)
{
  int rc;
  rc = pthread_cond_destroy(c);
  caml_stat_free(c);
  return rc;
}

Caml_inline int capsule_condition_signal(capsule_condition c)
{
 return pthread_cond_signal(c);
}

Caml_inline int capsule_condition_broadcast(capsule_condition c)
{
    return pthread_cond_broadcast(c);
}

Caml_inline int capsule_condition_wait(capsule_condition c, capsule_mutex m)
{
  return pthread_cond_wait(c, m);
}

#endif /* PLATFORM_LINUX */

#endif /* CAML_INTERNALS */
