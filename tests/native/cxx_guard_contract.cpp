#include "../../src/runtime/hle/nso_host.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cxxabi.h>
#include <pthread.h>
#include <sched.h>
#include <type_traits>
namespace abi=__cxxabiv1;
using acquire_fn=int(*)(uint64_t*);
using finish_fn=void(*)(uint64_t*);
static_assert(sizeof(uint64_t)==8 && alignof(uint64_t)==8);
static_assert(std::is_same<decltype(&abi::__cxa_guard_acquire),acquire_fn>::value);
static_assert(std::is_same<decltype(&abi::__cxa_guard_release),finish_fn>::value);
static_assert(std::is_same<decltype(&abi::__cxa_guard_abort),finish_fn>::value);
constexpr unsigned threads_count=8,iterations=10000;
static std::atomic<unsigned> constructors{0},ready{0},failures{0};
static std::atomic<bool> start{false};
struct Value {
    unsigned value;
    Value():value(0x73851a4u) {
        constructors.fetch_add(1);
        for(unsigned i=0;i<1000;i++)sched_yield();
    }
};
static Value &local_static() {static Value value;return value;}
static void *local_worker(void *out) {
    ready.fetch_add(1);
    while(!start.load())sched_yield();
    Value *first=nullptr;
    for(unsigned i=0;i<iterations;i++) {
        Value *value=&local_static();
        if(value->value!=0x73851a4u || (first && first!=value))failures.fetch_add(1);
        first=value;
    }
    *static_cast<Value**>(out)=first;
    return nullptr;
}
static acquire_fn acquire;
static finish_fn release_guard,abort_guard;
alignas(8) static uint64_t manual_guard;
static unsigned manual_value;
static std::atomic<unsigned> manual_constructors{0};
static void *abort_waiter(void*) {
    ready.fetch_add(1);
    if(acquire(&manual_guard)) {
        manual_constructors.fetch_add(1);
        manual_value=0x405080u;
        release_guard(&manual_guard);
    }
    if(manual_value!=0x405080u)failures.fetch_add(1);
    return nullptr;
}
static std::atomic<unsigned> retry_attempts{0};
struct Retry {
    unsigned value;
    Retry():value(0x19a4u) {if(retry_attempts.fetch_add(1)==0)throw 17;}
};
static Retry &retry_static() {static Retry value;return value;}
int main() {
    acquire=reinterpret_cast<acquire_fn>(nso_host_resolve(4,"__cxa_guard_acquire"));
    release_guard=reinterpret_cast<finish_fn>(nso_host_resolve(5,"__cxa_guard_release"));
    abort_guard=reinterpret_cast<finish_fn>(nso_host_resolve(253,"__cxa_guard_abort"));
    if(acquire!=&abi::__cxa_guard_acquire || release_guard!=&abi::__cxa_guard_release ||
       abort_guard!=&abi::__cxa_guard_abort) {
        std::fputs("resolver did not return the linked typed compiler runtime\n",stderr);return 1;
    }
    alignas(8) uint64_t guard=0;
    if(acquire(&guard)!=1)return 1;
    abort_guard(&guard);
    if((reinterpret_cast<unsigned char*>(&guard)[0]&1) || acquire(&guard)!=1)return 1;
    release_guard(&guard);
    if(!(reinterpret_cast<unsigned char*>(&guard)[0]&1) || acquire(&guard)!=0)return 1;
    std::puts("phase=manual_acquire_abort_release completed");std::fflush(stdout);
    pthread_t threads[threads_count];Value *values[threads_count]={};
    for(unsigned i=0;i<threads_count;i++)if(pthread_create(&threads[i],nullptr,local_worker,&values[i]))return 1;
    while(ready.load()!=threads_count)sched_yield();
    start.store(true);
    for(auto &thread:threads)if(pthread_join(thread,nullptr))return 1;
    for(auto *value:values)if(value!=values[0])failures.fetch_add(1);
    if(constructors.load()!=1)failures.fetch_add(1);
    std::printf("phase=local_static completed constructors=%u errors=%u\n",constructors.load(),failures.load());
    std::fflush(stdout);
    // An in-progress owner aborts while other real runtime acquires contend.
    ready.store(0);
    if(acquire(&manual_guard)!=1)return 1;
    for(auto &thread:threads)if(pthread_create(&thread,nullptr,abort_waiter,nullptr))return 1;
    while(ready.load()!=threads_count)sched_yield();
    abort_guard(&manual_guard);
    for(auto &thread:threads)if(pthread_join(thread,nullptr))return 1;
    if(manual_constructors.load()!=1 || acquire(&manual_guard)!=0)failures.fetch_add(1);
    std::printf("phase=contended_abort completed constructors=%u errors=%u\n",manual_constructors.load(),failures.load());
    std::fflush(stdout);
    bool caught=false;
    try {(void)retry_static();}catch(int value) {caught=value==17;}
    if(!caught || retry_static().value!=0x19a4u || retry_static().value!=0x19a4u || retry_attempts.load()!=2)
        failures.fetch_add(1);
    std::printf("{\"threads\":8,\"local_static_calls\":80000,\"constructors\":%u,"
                "\"abort_waiter_constructors\":%u,\"exception_retry_attempts\":%u,"
                "\"guard_size\":8,\"typed_resolver\":true,\"errors\":%u}\n",
                constructors.load(),manual_constructors.load(),retry_attempts.load(),failures.load());
    return failures.load()?1:0;
}
