#ifndef NSO_HOST_H
#define NSO_HOST_H
#include "../nso/nso_loader.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional exact-name extension, e.g. a typed NVN bootstrap resolver.
 * Return zero for unsupported names. No graphics headers or generic stub ABI. */
void nso_host_set_extension_resolver(nso_import_fn resolver);
/* Android builds strongly reference the linked compiler C++ ABI guard runtime.
 * Link libc++abi (plus its platform dependencies); no dlsym export is required.
 * Other hosts can exercise the same path with NSO_WITH_CXX_GUARDS=1. */
uint64_t nso_host_resolve(uint32_t index,const char *name);
struct nn_uuid { uint64_t w0, w1; };
struct nn_uuid nso_host_generate_uuid(void);
struct nn_location_name { char name[36]; };
uint32_t nso_host_time_initialize(void);
bool nso_host_time_is_initialized(void);
uint32_t nso_host_time_get_current_time(int64_t *out);
uint32_t nso_host_time_get_device_location_name(struct nn_location_name *out);
uint32_t nso_host_time_load_timezone_rule(void *rule, const struct nn_location_name *name);
int nso_host_time_get_day_of_week(int y, int m, int d);
int64_t nso_host_os_get_system_tick_frequency(void);
int64_t nso_host_os_get_system_tick(void);
struct nn_calendar_time {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t hour;
    int8_t minute;
    int8_t second;
    int8_t pad;
};
struct nn_calendar_additional_info {
    uint32_t day_of_week;
    int32_t day_of_year;
    char timezone_name[8];
    bool is_dst;
    char pad[3];
    int32_t utc_offset_seconds;
};
uint32_t nso_host_time_to_calendar_time(struct nn_calendar_time *out_cal,
                                        struct nn_calendar_additional_info *out_info,
                                        const int64_t *posix_time,
                                        const void *rule);
uint64_t nso_host_time_to_calendar_time_in_utc(const int64_t *posix_time);
#ifdef __cplusplus
}
#endif
#endif
