#ifndef FFRS_MULTIWAIT_H
#define FFRS_MULTIWAIT_H
#include <stdint.h>
/* EventType-only subset actually imported by main. WaitAny observes a holder;
 * it does not consume the Event's signal. No native-handle polling emulation. */
uint64_t ffrs_multiwait_lookup(const char *,unsigned *,unsigned *);
void ffrs_multiwait_notify(void);
void ffrs_multiwait_notify_event(void *);
int ffrs_multiwait_event_referenced(void *);
/* Registered holders outlive a quiescent Event during real movie teardown.
 * Only an active wait requires the Event's live predicate storage. */
int ffrs_multiwait_event_waiting(void *);
#endif
