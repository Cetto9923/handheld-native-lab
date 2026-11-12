/* Mach-O does not treat the weak guest_process_invoke_thread_exit as optional.
 * Returning 0 matches the absent-symbol path: the registry runs the callback. */
typedef void (*ffrs_exit_callback)(void *);
int guest_process_invoke_thread_exit(ffrs_exit_callback callback, void *argument) {
    (void)callback; (void)argument; return 0;
}
