#include <stdio.h>
#include <stdlib.h>
/* Host stand-in for symbols the event contract does not exercise.
 * A call here fails the test instead of pretending the path ran. */
/* The MultiWait state is controllable so that a contract can drive the
 * scene unload race: the event contract leaves it at zero, the retire
 * contract turns it on. */
int ffrs_test_multiwait_waiting;
int ffrs_test_multiwait_referenced;
int ffrs_multiwait_event_waiting(void *event){(void)event;return ffrs_test_multiwait_waiting;}
int ffrs_multiwait_event_referenced(void *event){(void)event;return ffrs_test_multiwait_referenced;}
void ffrs_multiwait_notify(void){}
void ffrs_multiwait_notify_event(void *event){(void)event;}
void guest_process_interposed_aligned_alloc(void){fprintf(stderr,"event contract called unexpected %s\n","guest_process_interposed_aligned_alloc");abort();}
void guest_process_interposed_free(void){fprintf(stderr,"event contract called unexpected %s\n","guest_process_interposed_free");abort();}
void guest_process_is_guest_memory(void){fprintf(stderr,"event contract called unexpected %s\n","guest_process_is_guest_memory");abort();}
void guest_process_release_guest_page_block(void){fprintf(stderr,"event contract called unexpected %s\n","guest_process_release_guest_page_block");abort();}
void guest_tp_bind_current(void){fprintf(stderr,"event contract called unexpected %s\n","guest_tp_bind_current");abort();}
void guest_tp_mirror_base(void){fprintf(stderr,"event contract called unexpected %s\n","guest_tp_mirror_base");abort();}
void guest_tp_mirror_create(void){fprintf(stderr,"event contract called unexpected %s\n","guest_tp_mirror_create");abort();}
void guest_tp_mirror_free(void){fprintf(stderr,"event contract called unexpected %s\n","guest_tp_mirror_free");abort();}
void guest_tp_mirror_span(void){fprintf(stderr,"event contract called unexpected %s\n","guest_tp_mirror_span");abort();}
void guest_tp_unbind_current(void){fprintf(stderr,"event contract called unexpected %s\n","guest_tp_unbind_current");abort();}
void heap_owner_forget(void){fprintf(stderr,"event contract called unexpected %s\n","heap_owner_forget");abort();}
void heap_owner_lookup(void){fprintf(stderr,"event contract called unexpected %s\n","heap_owner_lookup");abort();}
void heap_owner_record(void){fprintf(stderr,"event contract called unexpected %s\n","heap_owner_record");abort();}
void nso_exit_registry_thread_finalize(void){fprintf(stderr,"event contract called unexpected %s\n","nso_exit_registry_thread_finalize");abort();}
void nso_fault_scope_set_thread_stack(void){fprintf(stderr,"event contract called unexpected %s\n","nso_fault_scope_set_thread_stack");abort();}
void nso_host_acquire_read_lock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_acquire_read_lock");abort();}
void nso_host_acquire_write_lock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_acquire_write_lock");abort();}
void nso_host_allocate_address_region(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_allocate_address_region");abort();}
void nso_host_atexit(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_atexit");abort();}
void nso_host_cxa_atexit(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_cxa_atexit");abort();}
void nso_host_cxa_finalize(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_cxa_finalize");abort();}
void nso_host_cxa_thread_atexit(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_cxa_thread_atexit");abort();}
void nso_host_cxa_thread_atexit_impl(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_cxa_thread_atexit_impl");abort();}
void nso_host_exit(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_exit");abort();}
void nso_host_finalize_condition_variable(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_finalize_condition_variable");abort();}
void nso_host_finalize_mutex(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_finalize_mutex");abort();}
void nso_host_finalize_rwlock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_finalize_rwlock");abort();}
void nso_host_free_address_region(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_free_address_region");abort();}
void nso_host_immediate_exit(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_immediate_exit");abort();}
void nso_host_initialize_condition_variable(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_initialize_condition_variable");abort();}
void nso_host_initialize_mutex(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_initialize_mutex");abort();}
void nso_host_initialize_rwlock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_initialize_rwlock");abort();}
void nso_host_is_mutex_locked_by_current_thread(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_is_mutex_locked_by_current_thread");abort();}
void nso_host_is_read_lock_held(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_is_read_lock_held");abort();}
void nso_host_is_write_lock_held_by_current_thread(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_is_write_lock_held_by_current_thread");abort();}
void nso_host_lock_mutex(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_lock_mutex");abort();}
void nso_host_native_window_generation(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_native_window_generation");abort();}
void nso_host_native_window_get(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_native_window_get");abort();}
void nso_host_native_window_wait_stable(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_native_window_wait_stable");abort();}
void nso_host_release_read_lock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_release_read_lock");abort();}
void nso_host_release_write_lock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_release_write_lock");abort();}
void nso_host_signal_condition_variable(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_signal_condition_variable");abort();}
void nso_host_timed_wait_condition_variable(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_timed_wait_condition_variable");abort();}
void nso_host_try_acquire_read_lock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_try_acquire_read_lock");abort();}
void nso_host_try_acquire_write_lock(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_try_acquire_write_lock");abort();}
void nso_host_try_lock_mutex(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_try_lock_mutex");abort();}
void nso_host_unlock_mutex(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_unlock_mutex");abort();}
void nso_host_wait_condition_variable(void){fprintf(stderr,"event contract called unexpected %s\n","nso_host_wait_condition_variable");abort();}
void nvn_bootstrap_loader(void){fprintf(stderr,"event contract called unexpected %s\n","nvn_bootstrap_loader");abort();}
void nvn_device_get_proc_address(void){fprintf(stderr,"event contract called unexpected %s\n","nvn_device_get_proc_address");abort();}
void nvn_device_get_stats(void){fprintf(stderr,"event contract called unexpected %s\n","nvn_device_get_stats");abort();}
