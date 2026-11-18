#include <mutex>
#include <condition_variable>
#include <thread>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <cassert>
struct {int device=1;} cfg;
std::mutex mu;
std::map<std::string,int> layouts;
std::vector<int> set_layouts;
std::set<int> destroyed_pl,destroyed_sl;
void vkDestroyPipelineLayout(int,int p,void*){assert(destroyed_pl.insert(p).second);}
void vkDestroyDescriptorSetLayout(int,int p,void*){assert(destroyed_sl.insert(p).second);}
int publish(const std::string &k,int sl,int pl){
#include "warm-layout-publish.inc"
}
int main(){
 std::mutex gate;std::condition_variable cv;int arrived=0,out[2]={};
 auto run=[&](int i){
   {std::lock_guard<std::mutex> lock(mu);assert(layouts.find("same")==layouts.end());}
   // Both misses and local Vulkan pair creation complete before publication.
   {std::unique_lock<std::mutex> l(gate);++arrived;cv.notify_all();cv.wait(l,[&]{return arrived==2;});}
   out[i]=publish("same",10+i,100+i);
 };
 std::thread a(run,0),b(run,1);a.join();b.join();
 assert(out[0]==out[1]);assert(layouts.size()==1&&set_layouts.size()==1);
 assert(destroyed_pl.size()==1&&destroyed_sl.size()==1);
 assert(!destroyed_pl.count(out[0]));assert(!destroyed_sl.count(set_layouts[0]));
 // Winner already externally used: a late duplicate still cannot destroy it.
 int again=publish("same",12,102);assert(again==out[0]);assert(!destroyed_pl.count(again));
 assert(set_layouts.size()==1&&destroyed_pl.count(102)&&destroyed_sl.count(12));
 int distinct=publish("other",20,200);assert(distinct==200&&layouts.size()==2&&set_layouts.size()==2);
 for(auto &p:layouts)vkDestroyPipelineLayout(cfg.device,p.second,nullptr);
 for(auto s:set_layouts)vkDestroyDescriptorSetLayout(cfg.device,s,nullptr);
 assert(destroyed_pl==std::set<int>({100,101,102,200}));
 assert(destroyed_sl==std::set<int>({10,11,12,20}));
}
