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
#endif
