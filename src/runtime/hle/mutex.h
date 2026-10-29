#ifndef NSO_HOST_MUTEX_H
#define NSO_HOST_MUTEX_H
#include <stdbool.h>
#include <stdint.h>
/* SDK 20.5.17 function-call ABI, with byte-observed public field mirrors.
 * The host owner token is opaque and deliberately unreadable; it is NOT a
 * Switch ThreadType or permission to execute inline SDK thread operations. */
void nso_host_initialize_mutex(void *object,bool recursive,int level);
void nso_host_finalize_mutex(void *object);
void nso_host_lock_mutex(void *object);
bool nso_host_try_lock_mutex(void *object);
void nso_host_unlock_mutex(void *object);
bool nso_host_is_mutex_locked_by_current_thread(const void *object);
uintptr_t nso_host_mutex_thread_token(void);
void nso_host_initialize_condition_variable(void *condition);
void nso_host_finalize_condition_variable(void *condition);
void nso_host_wait_condition_variable(void *condition, void *mutex);
bool nso_host_timed_wait_condition_variable(void *condition, void *mutex, int64_t nanoseconds);
void nso_host_signal_condition_variable(void *condition);

void nso_host_initialize_rwlock(void *object);
void nso_host_finalize_rwlock(void *object);
void nso_host_acquire_read_lock(void *object);
bool nso_host_try_acquire_read_lock(void *object);
void nso_host_release_read_lock(void *object);
void nso_host_acquire_write_lock(void *object);
bool nso_host_try_acquire_write_lock(void *object);
void nso_host_release_write_lock(void *object);
bool nso_host_is_write_lock_held_by_current_thread(const void *object);
bool nso_host_is_read_lock_held(const void *object);
bool nso_host_is_rwlock_owner_thread(const void *object);
#endif
