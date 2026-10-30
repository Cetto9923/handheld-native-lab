// Fixture compiles the CURRENT production functions extracted by the runner.
// GPU completion is mocked: this proves branch/order dependencies, not GPU behavior.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <map>
#include "../../src/graphics/nvn_vulkan/engine/xbg_writeback_policy.h"
struct NVNcounterData {uint64_t counter,timestamp;};
struct {uint64_t sync_query=0,sync_writeback=0,why_access=0,access_by[12]={};} perf;
static bool resolving_pending=false,read_preflight=true,defer_query_preflight=true,completed=false,reenter=false;
static unsigned waits=0,invalidates=0,reports=0,events=0;
struct Buffer {void *mapped;void invalidate(){assert(completed);++invalidates;}};
struct Writeback {uint64_t addr,bytes;Buffer buffer;};
struct Report {uint64_t addr;};
static std::vector<Writeback> pending_writebacks;
static std::vector<Report> pending_reports;
static std::map<int,int> epochs;static int counter_epoch=1;
void preflight_cpu_reads();
static void need(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static void gpu_sync(uint64_t &reason){++reason;++waits;assert(resolving_pending);if(reenter)preflight_cpu_reads();completed=true;}
static void invalidate_cpu_range(uint64_t,uint64_t){assert(completed);}
static void write_report(const Report &r){assert(completed);++reports;NVNcounterData value{0,123};memcpy(reinterpret_cast<void*>(r.addr),&value,sizeof value);}
static void apply_ready_events(){assert(completed);++events;}
#include "preflight_functions.inc"
static void reset(){perf={};resolving_pending=false;read_preflight=true;defer_query_preflight=true;completed=false;reenter=false;waits=invalidates=reports=events=0;pending_writebacks.clear();pending_reports.clear();epochs.clear();}
int main(){
 uint64_t destination=0,source=42;NVNcounterData report{};
 reset();preflight_cpu_reads();preflight_cpu_reads();assert(waits==0);
 reset();pending_reports.push_back({uint64_t(uintptr_t(&report))});epochs={{1,0},{2,0}};preflight_cpu_reads();assert(waits==0&&!pending_reports.empty());cpu_access(uint64_t(uintptr_t(&report)),16,3);assert(waits==1&&reports==1&&report.timestamp==123&&epochs.size()==1&&pending_reports.empty());preflight_cpu_reads();assert(waits==1);
 reset();defer_query_preflight=false;pending_reports.push_back({uint64_t(uintptr_t(&report))});epochs={{1,0},{2,0}};preflight_cpu_reads();assert(waits==1&&reports==1&&report.timestamp==123&&epochs.size()==1);preflight_cpu_reads();assert(waits==1);
 reset();pending_writebacks.push_back({uint64_t(uintptr_t(&destination)),8,{&source}});pending_reports.push_back({uint64_t(uintptr_t(&report))});reenter=true;preflight_cpu_reads();assert(waits==1&&invalidates==1&&reports==1&&destination==42&&events==1);assert(!resolving_pending);preflight_cpu_reads();assert(waits==1);
 reset();pending_writebacks.push_back({uint64_t(uintptr_t(&destination)),8,{&source}});cpu_access(uint64_t(uintptr_t(&destination))+8,8,3);assert(waits==0);cpu_access(uint64_t(uintptr_t(&destination))+7,1,3);assert(waits==1&&perf.access_by[3]==1);
 reset();pending_reports.push_back({uint64_t(uintptr_t(&report))});cpu_access(uint64_t(uintptr_t(&report))+16,1,3);assert(waits==0);cpu_access(uint64_t(uintptr_t(&report)),16,3);assert(waits==1&&perf.access_by[7]==1);
 reset();pending_writebacks.push_back({uint64_t(uintptr_t(&destination)),8,{&source}});bool threw=false;try{cpu_access(UINT64_MAX,2,3);}catch(const std::runtime_error&){threw=true;}assert(threw&&waits==0);cpu_access(0,0,3);assert(waits==0);
 reset();pending_reports.push_back({uint64_t(uintptr_t(&report))});cpu_access(uint64_t(uintptr_t(&report)),16,1);assert(waits==0); // existing snapshot-report exception; characterized, NOT approved for arbitrary CPU reads.
 reset();pending_reports.push_back({uint64_t(uintptr_t(&report))});read_preflight=false;preflight_cpu_reads();assert(waits==0&&!pending_reports.empty()); // test-only guard characterization, never changes runtime config.
 puts("current-source preflight branch/order/range/reentry contracts passed; mock GPU only");return 0;
}
