#include "xbg_packed_shader_words.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

int shader_reuse=1;
void xbg_logf(const char *,...) {}
namespace warm {
std::atomic<bool> memory_constrained{false};
#include "rewrite_cache_production.inc"
}

static void reset_cache() {
    warm::rewritten.clear();
    warm::rewritten_bytes=0;
    warm::reuse_rejected=false;
    warm::rewritten_retained=0;
    warm::rewritten_denied_budget=0;
    warm::rewritten_peak_bytes=0;
    warm::memory_constrained=false;
}

static void packing_contracts() {
    std::vector<uint32_t> repeated(16384,0x07230203u),restored{77};
    XbgPackedShaderWords packed;
    assert(packed.assign(repeated) && packed.compressed);
    assert(packed.data.size()<repeated.size()*sizeof(uint32_t));
    assert(packed.unpack(restored) && restored==repeated);
    const auto original_data=packed.data;
    assert(!packed.assign({}) && packed.data==original_data);
    packed.data.back()^=1; // Damage the Adler checksum, including for highly repetitive input.
    restored={77};
    assert(!packed.unpack(restored) && restored==std::vector<uint32_t>{77});
    packed.data=original_data;
    packed.word_count++;
    assert(!packed.unpack(restored) && restored==std::vector<uint32_t>{77});
    packed.word_count=XbgPackedShaderWords::max_bytes/4+1;
    assert(!packed.unpack(restored) && restored==std::vector<uint32_t>{77});

    std::vector<uint32_t> noise(16384);
    uint32_t state=0x12345678;
    for(auto &word:noise){state^=state<<13;state^=state>>17;state^=state<<5;word=state;}
    assert(packed.assign(noise) && !packed.compressed);
    assert(packed.unpack(restored) && restored==noise);
    packed.data.pop_back();
    restored={77};
    assert(!packed.unpack(restored) && restored==std::vector<uint32_t>{77});

    reset_cache();
    warm::retain_rewritten("shader",repeated,noise);
    const size_t retained_bytes=warm::rewritten_bytes;
    warm::retain_rewritten("shader",repeated,repeated);
    assert(warm::rewritten.size()==1 && warm::rewritten_bytes==retained_bytes);
    assert(warm::take_rewritten("shader",repeated,restored) && restored==noise);
    assert(warm::rewritten_bytes==0 && warm::rewritten.empty());
    assert(!warm::take_rewritten("shader",repeated,restored) && restored==noise);

    warm::memory_constrained=true;
    warm::retain_rewritten("pressure",repeated,noise);
    assert(warm::rewritten.empty() && warm::rewritten_bytes==0);
    warm::memory_constrained=false;

    warm::retain_rewritten("mismatch",repeated,noise);
    auto mismatch=repeated;mismatch.back()^=1;
    restored={77};
    assert(!warm::take_rewritten("mismatch",mismatch,restored));
    assert(restored==std::vector<uint32_t>{77} && warm::reuse_rejected && warm::rewritten_bytes==0);
    warm::retain_rewritten("after-rejection",repeated,noise);
    assert(warm::rewritten.empty());

    for(bool corrupt_output:{false,true}){
        reset_cache();
        warm::retain_rewritten("corrupt",repeated,repeated);
        auto &entry=warm::rewritten.at("corrupt");
        auto &data=corrupt_output?entry.output.data:entry.input.data;
        data.back()^=1;
        restored={77};
        assert(!warm::take_rewritten("corrupt",repeated,restored));
        assert(restored==std::vector<uint32_t>{77} && warm::reuse_rejected && warm::rewritten_bytes==0);
    }

    reset_cache();
    const size_t pair_bytes=noise.size()*sizeof(uint32_t)*2;
    const size_t fits=warm::rewritten_limit/pair_bytes;
    for(size_t i=0;i<fits+1;i++)warm::retain_rewritten(std::to_string(i),noise,noise);
    assert(warm::rewritten.size()==fits && warm::rewritten_bytes==fits*pair_bytes);
    assert(warm::rewritten_denied_budget==1 && warm::rewritten_peak_bytes<=warm::rewritten_limit);
    assert(warm::take_rewritten("0",noise,restored) && restored==noise);
    warm::retain_rewritten("replacement",noise,noise);
    assert(warm::rewritten.size()==fits && warm::rewritten_bytes==fits*pair_bytes);

    reset_cache();
    std::vector<std::thread> threads;
    for(unsigned i=0;i<4;i++)threads.emplace_back([&]{warm::retain_rewritten("shared",repeated,noise);});
    for(auto &thread:threads)thread.join();
    assert(warm::rewritten.size()==1 && warm::rewritten_retained==1);
    assert(warm::take_rewritten("shared",repeated,restored) && restored==noise);
    assert(warm::rewritten_bytes==0);
    reset_cache();
}

int main(int argc,char **argv) {
    packing_contracts();
    size_t raw_bytes=0,packed_bytes=0;
    for(int i=1;i<argc;i++){
        std::ifstream file(argv[i],std::ios::binary|std::ios::ate);
        assert(file);
        const auto bytes=file.tellg();
        assert(bytes>=20 && bytes%4==0);
        std::vector<uint32_t> words(size_t(bytes)/4),restored;
        file.seekg(0);file.read(reinterpret_cast<char *>(words.data()),bytes);
        assert(file && words[0]==0x07230203u);
        XbgPackedShaderWords packed;
        const auto begin=std::chrono::steady_clock::now();
        assert(packed.assign(words));
        const auto packed_at=std::chrono::steady_clock::now();
        assert(packed.unpack(restored) && restored==words);
        const auto end=std::chrono::steady_clock::now();
        raw_bytes+=size_t(bytes);packed_bytes+=packed.data.size();
        std::cout<<"module="<<argv[i]<<" bytes="<<bytes<<" packed="<<packed.data.size()
                 <<" pack_ms="<<std::chrono::duration<double,std::milli>(packed_at-begin).count()
                 <<" unpack_ms="<<std::chrono::duration<double,std::milli>(end-packed_at).count()<<" exact=1\n";
    }
    std::cout<<"packed shader words PASS real_modules="<<argc-1<<" raw_bytes="<<raw_bytes<<" packed_bytes="<<packed_bytes<<"\n";
}
