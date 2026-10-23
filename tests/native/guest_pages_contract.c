#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_pages.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

static int errors, checks, recovery_cases;
#define CHECK(c) do { ++checks; if (!(c)) { ++errors; fprintf(stderr,"line %d: %s\n",__LINE__,#c); } } while (0)
static const size_t unit = 65536;

/* Inspect only our exact VM spans; never read a NONE page to test a fault. */
static int protection_is(uintptr_t address, size_t bytes, int wanted) {
    uintptr_t end = address + bytes;
    while (address < end) {
#if defined(__APPLE__)
        mach_vm_address_t found = address;
        mach_vm_size_t length = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        kern_return_t k = mach_vm_region(mach_task_self(),&found,&length,
            VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
        if (k != KERN_SUCCESS || found > address || !length || info.protection != wanted) return 0;
        uintptr_t next = found + length;
#else
        FILE *f = fopen("/proc/self/maps","r");
        if (!f) return 0;
        char line[512],perms[5]; unsigned long long low,high;
        uintptr_t next = 0;
        while (fgets(line,sizeof(line),f)) {
            if (sscanf(line,"%llx-%llx %4s",&low,&high,perms) != 3 || address < low || address >= high) continue;
            int prot = (perms[0]=='r'?PROT_READ:0)|(perms[1]=='w'?PROT_WRITE:0)|(perms[2]=='x'?PROT_EXEC:0);
            if (prot == wanted) next = (uintptr_t)high;
            break;
        }
        fclose(f);
        if (!next) return 0;
#endif
        if (next <= address) return 0;
        address = next < end ? next : end;
    }
    return 1;
}
static int all_zero(uintptr_t address,size_t bytes) {
    const unsigned char *p = (const unsigned char *)address;
    for (size_t i=0;i<bytes;++i) if (p[i]) return 0;
    return 1;
}
static void snapshot(guest_pages *p,size_t regions,size_t allocations,size_t pending,size_t charged) {
    struct guest_pages_usage u;
    struct guest_pages_result r = guest_pages_usage(p,&u);
    CHECK(r.status == GUEST_PAGES_OK);
    CHECK(u.ledger_valid && u.backing_valid);
    CHECK(u.region_count == regions && u.allocation_count == allocations && u.ticket_count == allocations);
    CHECK(u.pending_bytes == pending && u.charged_bytes == charged && u.backing_bytes == charged);
    CHECK(u.context.pending_bytes >= pending && u.context.charged_bytes >= charged);
}

#if defined(GUEST_PAGES_CONTRACT_WRAP_CONTEXT)
/* Test-link wrappers: the production owner/Context have no new hooks. The
 * Context translation unit renames only these public symbols for this binary.
 * Capture quota state BEFORE entering region_mutex; the VM callback only reads
 * that copy, and never takes a Context lock while region_mutex is held. */
struct guest_context_result guest_charge_reserve_real(guest_context *,size_t,guest_charge_ticket **);
struct guest_context_result guest_charge_commit_real(guest_charge_ticket *);
static size_t reserve_calls, reserve_successes, pending_before_vm;
static int refuse_commit;
struct guest_context_result guest_charge_reserve(guest_context *c,size_t bytes,guest_charge_ticket **out) {
    ++reserve_calls;
    struct guest_context_result r = guest_charge_reserve_real(c,bytes,out);
    if (r.status == GUEST_CONTEXT_OK) {
        struct guest_context_usage u;
        CHECK(guest_context_usage(c,&u).status == GUEST_CONTEXT_OK);
        pending_before_vm = u.pending_bytes;
        ++reserve_successes;
    }
    return r;
}
struct guest_context_result guest_charge_commit(guest_charge_ticket *ticket) {
    if (refuse_commit) {
        struct guest_context_result r = {GUEST_CONTEXT_SYSTEM_ERROR,EIO,0};
        return r;
    }
    return guest_charge_commit_real(ticket);
}
static int protect_calls, replace_calls, fail_restore, reject_replace, seen_precharge;
static int observe_protect(void *address,size_t bytes,int prot) {
    ++protect_calls;
    if (prot == (PROT_READ|PROT_WRITE) && pending_before_vm >= bytes) seen_precharge = 1;
    return mprotect(address,bytes,prot);
}
static int partial_protect_failure(void *address,size_t bytes,int prot) {
    ++protect_calls;
    if (protect_calls == 1) {
        seen_precharge = pending_before_vm >= bytes;
        CHECK(mprotect(address,bytes/2,prot) == 0);
        errno = EACCES; return -1;
    }
    if (fail_restore) { errno = EIO; return -1; }
    return mprotect(address,bytes,prot);
}
static void *replace_none(void *address,size_t bytes) {
    ++replace_calls;
    if (reject_replace) { errno = ENOMEM; return MAP_FAILED; }
    return mmap(address,bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
}
static void reset_hooks(void) {
    nso_host_owned_pages_test_vm(NULL,NULL);
    protect_calls = replace_calls = fail_restore = reject_replace = seen_precharge = refuse_commit = 0;
    pending_before_vm = 0;
}
static void recovery(guest_context *context,guest_pages *p) {
    uintptr_t base = 0;
    struct guest_pages_result r;
    struct guest_pages_usage u;
    CHECK(guest_pages_reserve(p,unit*3,&base).status == GUEST_PAGES_OK);

    reset_hooks();
    nso_host_owned_pages_test_vm(partial_protect_failure,NULL);
    r = guest_pages_map(p,base,unit);
    CHECK(r.status == GUEST_PAGES_VM_ERROR && r.vm_attempted);
    CHECK(r.vm.native_errno == EACCES && !r.vm.restore_errno);
    CHECK(r.ticket_cleanup.status == GUEST_CONTEXT_OK && protect_calls == 2 && seen_precharge);
    CHECK(protection_is(base,unit,PROT_NONE));
    snapshot(p,1,0,0,0); ++recovery_cases;

    reset_hooks(); refuse_commit = 1;
    nso_host_owned_pages_test_vm(observe_protect,replace_none);
    r = guest_pages_map(p,base,unit);
    CHECK(r.status == GUEST_PAGES_CONTEXT_ERROR && r.context.native_error == EIO);
    CHECK(r.rollback_attempted && r.rollback_vm.status == NSO_OWNED_PAGE_OK && replace_calls == 1);
    CHECK(r.ticket_cleanup.status == GUEST_CONTEXT_OK && seen_precharge);
    CHECK(protection_is(base,unit,PROT_NONE));
    snapshot(p,1,0,0,0); ++recovery_cases;

    reset_hooks();
    nso_host_owned_pages_test_vm(observe_protect,replace_none);
    CHECK(guest_pages_map(p,base,unit).status == GUEST_PAGES_OK && seen_precharge);
    size_t successes = reserve_successes, attempts = reserve_calls;
    int grants = protect_calls;
    r = guest_pages_map(p,base+unit,unit*2);
    CHECK(r.status == GUEST_PAGES_QUOTA && !r.vm_attempted);
    CHECK(reserve_calls == attempts+1 && reserve_successes == successes && protect_calls == grants);
    snapshot(p,1,1,0,unit); ++recovery_cases;
    CHECK(guest_pages_free_exact(p,base,unit).status == GUEST_PAGES_OK);

    reset_hooks(); fail_restore = 1;
    nso_host_owned_pages_test_vm(partial_protect_failure,NULL);
    r = guest_pages_map(p,base,unit);
    CHECK(r.status == GUEST_PAGES_QUARANTINED && r.vm.native_errno == EACCES && r.vm.restore_errno == EIO);
    CHECK(protect_calls == 2 && seen_precharge);
    CHECK(guest_pages_usage(p,&u).status == GUEST_PAGES_QUARANTINED);
    CHECK(u.ledger_valid && !u.backing_valid && !u.backing_bytes && u.pending_bytes == unit && !u.charged_bytes && u.ticket_count == 1);
    CHECK(guest_pages_map(p,base+unit,unit).status == GUEST_PAGES_QUARANTINED);
    CHECK(guest_pages_free_exact(p,base,unit).status == GUEST_PAGES_QUARANTINED);
    reset_hooks();
    CHECK(guest_pages_release_region(p,base).status == GUEST_PAGES_OK);
    snapshot(p,0,0,0,0); ++recovery_cases;

    CHECK(guest_pages_reserve(p,unit*2,&base).status == GUEST_PAGES_OK);
    CHECK(guest_pages_map(p,base,unit).status == GUEST_PAGES_OK);
    reset_hooks(); reject_replace = 1;
    nso_host_owned_pages_test_vm(NULL,replace_none);
    r = guest_pages_free_exact(p,base,unit);
    CHECK(r.status == GUEST_PAGES_QUARANTINED && r.vm.native_errno == ENOMEM && replace_calls == 1);
    CHECK(guest_pages_usage(p,&u).status == GUEST_PAGES_QUARANTINED);
    CHECK(u.ledger_valid && !u.backing_valid && !u.pending_bytes && u.charged_bytes == unit && u.ticket_count == 1);
    reset_hooks();
    CHECK(guest_pages_release_region(p,base).status == GUEST_PAGES_OK);
    snapshot(p,0,0,0,0); ++recovery_cases;

    CHECK(guest_pages_reserve(p,unit,&base).status == GUEST_PAGES_OK);
    reset_hooks(); refuse_commit = reject_replace = 1;
    nso_host_owned_pages_test_vm(observe_protect,replace_none);
    r = guest_pages_map(p,base,unit);
    CHECK(r.status == GUEST_PAGES_QUARANTINED && r.rollback_attempted && r.rollback_vm.status == NSO_OWNED_PAGE_QUARANTINED);
    CHECK(guest_pages_usage(p,&u).status == GUEST_PAGES_QUARANTINED);
    CHECK(u.ledger_valid && !u.backing_valid && u.pending_bytes == unit && !u.charged_bytes && u.ticket_count == 1);
    reset_hooks();
    CHECK(guest_pages_release_region(p,base).status == GUEST_PAGES_OK);
    snapshot(p,0,0,0,0); ++recovery_cases;
    struct guest_context_usage cu;
    CHECK(guest_context_usage(context,&cu).status == GUEST_CONTEXT_OK && !cu.pending_bytes && !cu.charged_bytes && !cu.tickets);
}
#endif

int main(void) {
    guest_context *context = NULL;
    guest_pages *p = NULL,*duplicate = NULL;
    struct guest_context_config config = {unit*2,unit};
    struct guest_context_usage cu;
    CHECK(guest_context_create(&config,&context).status == GUEST_CONTEXT_OK);
    if (!context) return 1;
    CHECK(guest_pages_create(context,&p).status == GUEST_PAGES_OK);
    if (!p) return 1;
    CHECK(guest_pages_create(context,&duplicate).status == GUEST_PAGES_BUSY && !duplicate);
    CHECK(guest_pages_reserve(p,1,&(uintptr_t){0}).status == GUEST_PAGES_UNSUPPORTED);
    uintptr_t base=0,other=0;
    CHECK(guest_pages_reserve(p,unit*4,&base).status == GUEST_PAGES_OK);
    CHECK(guest_pages_reserve(p,unit,&other).status == GUEST_PAGES_OK);
    snapshot(p,2,0,0,0);
    CHECK(protection_is(base,unit*4,PROT_NONE));
    CHECK(guest_pages_map(p,base,unit).status == GUEST_PAGES_OK);
    CHECK(protection_is(base,unit,PROT_READ|PROT_WRITE));
    CHECK(all_zero(base,unit));
    memset((void *)base,0x5a,unit);
    CHECK(((unsigned char *)base)[0] == 0x5a && ((unsigned char *)base)[unit-1] == 0x5a);
    CHECK(guest_pages_map(p,base,unit).status == GUEST_PAGES_OVERLAP);
    CHECK(guest_pages_map(p,base+unit,unit).status == GUEST_PAGES_OK);
    snapshot(p,2,2,0,unit*2);
    struct guest_pages_result r = guest_pages_map(p,other,unit);
    CHECK(r.status == GUEST_PAGES_QUOTA && !r.vm_attempted);
    CHECK(protection_is(other,unit,PROT_NONE));
    CHECK(guest_pages_free_exact(p,base,unit/2).status == GUEST_PAGES_UNSUPPORTED);
    CHECK(guest_pages_free_exact(p,base,unit*2).status == GUEST_PAGES_UNSUPPORTED);
    CHECK(guest_pages_free_exact(p,base+unit,unit*2).status == GUEST_PAGES_UNSUPPORTED);
    CHECK(guest_pages_free_exact(p,base+unit*3,unit).status == GUEST_PAGES_NOT_OWNED);
    snapshot(p,2,2,0,unit*2);
    CHECK(guest_pages_free_exact(p,base,unit).status == GUEST_PAGES_OK);
    CHECK(protection_is(base,unit,PROT_NONE));
    CHECK(guest_pages_free_exact(p,base,unit).status == GUEST_PAGES_NOT_OWNED);
    CHECK(guest_pages_map(p,base,unit).status == GUEST_PAGES_OK);
    CHECK(all_zero(base,unit));
    snapshot(p,2,2,0,unit*2);
    CHECK(guest_pages_release_region(p,base+unit).status == GUEST_PAGES_NOT_OWNED);
    CHECK(guest_pages_release_region(p,base).status == GUEST_PAGES_OK);
    CHECK(guest_pages_release_region(p,base).status == GUEST_PAGES_NOT_OWNED);
    snapshot(p,1,0,0,0);
    CHECK(guest_pages_release_region(p,other).status == GUEST_PAGES_OK);
    snapshot(p,0,0,0,0);
    /* Other Context providers coexist; owner usage counts only owned pages. */
    guest_charge_ticket *external = NULL;
    CHECK(guest_charge_reserve(context,unit,&external).status == GUEST_CONTEXT_OK);
    CHECK(guest_charge_commit(external).status == GUEST_CONTEXT_OK);
    snapshot(p,0,0,0,0);
    CHECK(guest_charge_release(external).status == GUEST_CONTEXT_OK);
#if defined(GUEST_PAGES_CONTRACT_WRAP_CONTEXT)
    recovery(context,p);
#endif
    CHECK(guest_pages_reserve(p,unit*2,&base).status == GUEST_PAGES_OK);
    CHECK(guest_pages_map(p,base,unit).status == GUEST_PAGES_OK);
    CHECK(guest_pages_map(p,base+unit,unit).status == GUEST_PAGES_OK);
    CHECK(guest_context_destroy(context).status == GUEST_CONTEXT_BUSY);
    CHECK(guest_pages_destroy(p).status == GUEST_PAGES_OK);
    CHECK(guest_context_usage(context,&cu).status == GUEST_CONTEXT_OK && !cu.pending_bytes && !cu.charged_bytes && !cu.tickets);
    struct nso_owned_page_usage raw;
    CHECK(nso_host_owned_pages_usage(&raw).status == NSO_OWNED_PAGE_OK && !raw.region_count && !raw.reserved_guest_bytes && !raw.mapped_guest_bytes);
    CHECK(guest_pages_create(context,&p).status == GUEST_PAGES_OK);
    CHECK(guest_pages_destroy(p).status == GUEST_PAGES_OK);
    CHECK(guest_context_destroy(context).status == GUEST_CONTEXT_OK);
    printf("{\"checks\":%d,\"errors\":%d,\"recovery_cases\":%d,\"final_pending\":%zu,\"final_charged\":%zu,\"final_tickets\":%zu,\"raw_regions\":%zu,\"guest_page_bytes\":%zu}\n",
        checks,errors,recovery_cases,cu.pending_bytes,cu.charged_bytes,cu.tickets,raw.region_count,unit);
    return errors ? 1 : 0;
}
