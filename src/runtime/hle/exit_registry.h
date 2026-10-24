#ifndef NSO_EXIT_REGISTRY_H
#define NSO_EXIT_REGISTRY_H
#include <stddef.h>
typedef struct nso_exit_registry nso_exit_registry;
typedef void (*nso_atexit_callback)(void);
typedef void (*nso_cxa_callback)(void *);
nso_exit_registry *nso_exit_registry_create(void);
/* All guest threads must be quiescent before destroy. It discards, never runs. */
void nso_exit_registry_destroy(nso_exit_registry *registry);
int nso_exit_registry_atexit(nso_exit_registry *registry,nso_atexit_callback fn);
int nso_exit_registry_cxa_atexit(nso_exit_registry *registry,nso_cxa_callback fn,void *arg,void *dso);
void nso_exit_registry_finalize(nso_exit_registry *registry,void *dso);
void nso_exit_registry_discard(nso_exit_registry *registry);
size_t nso_exit_registry_pending(nso_exit_registry *registry);
/* One guest process registry shared by its host threads; no implicit host
 * atexit hook. Only actual guest exit/finalize invokes registered callbacks. */
int nso_host_atexit(nso_atexit_callback fn);
int nso_host_cxa_atexit(nso_cxa_callback fn,void *arg,void *dso);
void nso_host_cxa_finalize(void *dso);
void nso_host_discard_exit_callbacks(void);
/* Thread-local exit callback support (__cxa_thread_atexit / __cxa_thread_atexit_impl).
 * Registered callbacks are invoked in LIFO order upon thread exit or explicit thread finalize. */
int nso_host_cxa_thread_atexit(nso_cxa_callback fn,void *arg,void *dso);
int nso_host_cxa_thread_atexit_impl(nso_cxa_callback fn,void *arg,void *dso);
size_t nso_exit_registry_thread_pending(void);
void nso_exit_registry_thread_finalize(void);
void nso_exit_registry_thread_discard(void);
/* Current HLE supports one guest module scope at a time. End requires guest
 * threads quiescent and discards registrations without executing callbacks. */
int nso_host_exit_scope_begin(void *owner);
void nso_host_exit_scope_end(void *owner);
_Noreturn void nso_host_exit(int status);
_Noreturn void nso_host_immediate_exit(int status);
#endif
