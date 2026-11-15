#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <zlib.h>

// Process-local storage only. Unpacking publishes complete, byte-identical words
// or leaves the caller's output untouched. The shader cache still compares its
// entire input, including specialized variants, before reusing any output.
struct XbgPackedShaderWords {
    static constexpr size_t max_bytes=16u*1024u*1024u;
    std::vector<uint8_t> data;
    size_t word_count=0;
    bool compressed=false;

    bool assign(const std::vector<uint32_t> &words) {
        if(words.empty() || words.size()>max_bytes/sizeof(uint32_t))return false;
        const size_t bytes=words.size()*sizeof(uint32_t);
        const auto *source=reinterpret_cast<const Bytef *>(words.data());
        std::vector<uint8_t> packed(compressBound(uLong(bytes)));
        uLongf packed_bytes=uLongf(packed.size());
        const int result=compress2(packed.data(),&packed_bytes,source,uLong(bytes),Z_BEST_SPEED);
        const bool use_compressed=result==Z_OK && packed_bytes<bytes;
        std::vector<uint8_t> stored(use_compressed?size_t(packed_bytes):bytes);
        std::memcpy(stored.data(),use_compressed?packed.data():source,stored.size());
        data.swap(stored);
        word_count=words.size();
        compressed=use_compressed;
        return true;
    }

    bool unpack(std::vector<uint32_t> &words) const {
        if(!word_count || word_count>max_bytes/sizeof(uint32_t) || data.empty() || data.size()>max_bytes)return false;
        const size_t bytes=word_count*sizeof(uint32_t);
        if(!compressed && data.size()!=bytes)return false;
        std::vector<uint32_t> restored(word_count);
        if(compressed){
            uLongf restored_bytes=uLongf(bytes);
            if(uncompress(reinterpret_cast<Bytef *>(restored.data()),&restored_bytes,data.data(),uLong(data.size()))!=Z_OK || restored_bytes!=bytes)return false;
        }else{
            std::memcpy(restored.data(),data.data(),bytes);
        }
        words.swap(restored);
        return true;
    }
};
