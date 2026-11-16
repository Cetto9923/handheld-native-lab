#include "xbg_spirv_sample_usage.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>

static void emit(std::vector<uint32_t> &w,uint32_t op,std::initializer_list<uint32_t> args) {
    w.push_back(uint32_t(args.size()+1)<<16|op);w.insert(w.end(),args.begin(),args.end());
}
static std::vector<uint32_t> fixture(uint32_t operation) {
    std::vector<uint32_t> w{0x07230203,0x10000,0,256,0};
    emit(w,25,{10,1,1,0,0,0,1,0});emit(w,27,{11,10});
    emit(w,71,{100,33,7});emit(w,71,{100,34,0});
    emit(w,59,{12,100,0});emit(w,61,{11,101,100});
    emit(w,operation,{20,102,101,30,31});return w;
}
int main(int argc,char **argv) {
    for(uint32_t op:{95u,96u,97u,103u,104u,106u,107u})
        assert(xbg_spirv_filter_independent(fixture(op))==std::set<uint32_t>{7});
    for(uint32_t op:{87u,88u,89u,90u,91u,92u,93u,94u,105u,305u,307u})
        assert(xbg_spirv_filter_independent(fixture(op)).empty());
    auto w=fixture(96);emit(w,88,{20,103,101,30,31});assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);emit(w,57,{20,103,200,101});assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);emit(w,245,{11,103,101,200,101,201});assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);emit(w,169,{11,103,30,101,101});assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);emit(w,12,{20,103,200,1,101});assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);w[8]=3;assert(xbg_spirv_filter_independent(w).empty()); // cube gather
    w=fixture(96);emit(w,100,{10,103,101});emit(w,95,{20,104,103,30});
    assert(xbg_spirv_filter_independent(w)==std::set<uint32_t>{7});
    w=fixture(96);emit(w,83,{11,103,101});emit(w,88,{20,104,103,30,31});
    assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);emit(w,65,{12,103,100,30});emit(w,61,{11,104,103});emit(w,88,{20,105,104,30,31});
    assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);w.push_back(0);assert(xbg_spirv_filter_independent(w).empty());
    w=fixture(96);w.push_back(20u<<16|96);assert(xbg_spirv_filter_independent(w).empty());
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);assert(file);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(bytes.size()%4==0);w.resize(bytes.size()/4);std::memcpy(w.data(),bytes.data(),bytes.size());
        assert(xbg_spirv_filter_independent(w)==(std::set<uint32_t>{76,88,94,100,112}));
    }
}
