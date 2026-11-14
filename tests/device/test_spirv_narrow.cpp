#include "xbg_spirv_narrow.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static size_t count_op(const std::vector<uint32_t> &words, uint32_t opcode) {
    size_t count = 0;
    for (size_t i = 5; i < words.size(); i += words[i] >> 16)
        count += (words[i] & 0xffff) == opcode;
    return count;
}

static void save(const std::string &path, const std::vector<uint32_t> &words) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(words.data()), words.size() * 4);
    assert(file.good());
}

static std::vector<uint32_t> load(const std::string &path) {
    std::ifstream input(path,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    assert(!bytes.empty() && bytes.size()%4==0);
    std::vector<uint32_t> words(bytes.size()/4);
    memcpy(words.data(),bytes.data(),bytes.size());
    return words;
}

static size_t butterfly_xor(const std::vector<uint32_t> &words) {
    size_t candidate=0;
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        uint32_t op=words[at]&0xffff;
        if(op==54)candidate=0;
        if(op==198)candidate=at;
        if(op==345 && candidate)return candidate;
    }
    assert(false);return 0;
}

static void rejected(std::vector<uint32_t> words,const std::string &name,
                     const std::string &directory) {
    save(directory+"/"+name+"-source.spv",words);
    auto before=words;
    assert(xbg_spirv_narrow(words,16,0x30)==0 && words==before);
    save(directory+"/"+name+"-refused.spv",words);
}

int main(int argc, char **argv) {
    assert(argc==3 || argc==8);
    auto original=load(argv[1]);

    for (uint32_t size : {8u, 16u}) {
        auto words = original;
        assert(xbg_spirv_narrow(words, size, 0x30) ==
               (SPV_NARROW_GUARD | SPV_NARROW_STEP));
        size_t steps = size == 8 ? 8 : 4;
        assert(count_op(words, 83) == count_op(original, 83) + steps);
        assert(count_op(words, 345) == count_op(original, 345));
        assert(count_op(words, 176) == count_op(original, 176) + 1);
        save(std::string(argv[2]) + "/narrow-" + std::to_string(size) + ".spv", words);
        auto rewritten=words;
        assert(xbg_spirv_narrow(words,size,0x30)==0 && words==rewritten);
    }
    for (uint32_t size : {0u, 3u, 17u, 32u, 64u}) {
        auto words = original;
        assert(xbg_spirv_narrow(words, size, 0x30) == 0 && words == original);
    }
    auto words = original;
    assert(xbg_spirv_narrow(words, 16, 0x10) == 0 && words == original);
    words.resize(4);
    auto before = words;
    assert(xbg_spirv_narrow(words, 16, 0x30) == 0 && words == before);
    words = original;
    words[5] = 0;
    before = words;
    assert(xbg_spirv_narrow(words, 16, 0x30) == 0 && words == before);
    words = original;
    words[3] = 0xffffffff;
    before = words;
    assert(xbg_spirv_narrow(words, 16, 0x30) == 0 && words == before);

    const size_t xor_at=butterfly_xor(original);
    uint32_t zero=0;
    for(size_t at=5;at<original.size();at+=original[at]>>16)
        if((original[at]&0xffff)==43 && (original[at]>>16)==4 &&
           original[at+1]==original[xor_at+1] && original[at+3]==0)zero=original[at+2];
    assert(zero);
    words=original;
    // Valid helper: its actual source is lane&0, while an unused XOR remains.
    // An opcode census incorrectly removes the lane-0 combine at offset 16.
    std::vector<uint32_t> unused(original.begin()+xor_at,original.begin()+xor_at+5);
    unused[2]=words[3]++;
    words[xor_at]=(5u<<16)|199u;
    words[xor_at+4]=zero;
    words.insert(words.begin()+xor_at,unused.begin(),unused.end());
    rejected(words,"unrelated-xor",argv[2]);

    words=original;
    words[xor_at]=(5u<<16)|128u;
    rejected(words,"unsupported-down",argv[2]);

    words=original;
    uint32_t unsigned_type=0,unsigned_zero=0,unsigned_one=0;
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        uint32_t op=words[at]&0xffff;
        if(op==21 && words[at+2]==32 && words[at+3]==0)unsigned_type=words[at+1];
        if(op==43 && (words[at]>>16)==4 && words[at+1]==unsigned_type) {
            if(words[at+3]==0)unsigned_zero=words[at+2];
            if(words[at+3]==1)unsigned_one=words[at+2];
        }
    }
    assert(unsigned_zero && unsigned_one);
    bool false_predicate=false;
    for(size_t at=xor_at;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffff)==178) {
            words[at+3]=unsigned_one;words[at+4]=unsigned_zero;
            false_predicate=true;break;
        }
    assert(false_predicate);
    rejected(words,"unrelated-predicate",argv[2]);

    words=original;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffff)==128)words[at]=(words[at]&0xffff0000u)|130u;
    rejected(words,"unsupported-combine",argv[2]);

    words=original;
    std::vector<uint32_t> loaded(words[3]),stored(words[3]);
    bool aliased=false;
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        uint32_t op=words[at]&0xffff;
        if(op==61)loaded[words[at+2]]=words[at+3];
        if(op==62)stored[words[at+1]]=words[at+2];
        if(op==57 && (words[at]>>16)==8) {
            uint32_t source=loaded[stored[words[at+4]]];
            assert(source);
            words[at+7]=source;aliased=true;break;
        }
    }
    assert(aliased);
    rejected(words,"aliased-predicate",argv[2]);

    words=original;
    std::vector<uint32_t> constants(words[3]),variable_type(words[3]),pointee(words[3]);
    loaded.assign(words[3],0);stored.assign(words[3],0);
    size_t alias_at=0;
    uint32_t source=0,offset_pointer=0;
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        uint32_t op=words[at]&0xffff;
        if(op==32)pointee[words[at+1]]=words[at+3];
        if(op==59)variable_type[words[at+2]]=words[at+1];
        if(op==43 && (words[at]>>16)==4)constants[words[at+2]]=words[at+3];
        if(op==61)loaded[words[at+2]]=words[at+3];
        if(op==124)loaded[words[at+2]]=loaded[words[at+3]];
        if(op==62)stored[words[at+1]]=words[at+2];
        if(op==57 && (words[at]>>16)==8 && constants[stored[words[at+5]]]==16) {
            source=loaded[stored[words[at+4]]];
            offset_pointer=words[at+5];
            size_t after=at+(words[at]>>16);
            assert((words[after]&0xffff)==62 && words[after+2]==words[at+2]);
            alias_at=after+(words[after]>>16);break;
        }
    }
    assert(alias_at && source && variable_type[source]);
    uint32_t integer_one=0;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffff)==43 && (words[at]>>16)==4 &&
           words[at+1]==pointee[variable_type[source]] && words[at+3]==1)integer_one=words[at+2];
    assert(integer_one);
    uint32_t alias=words[3]++;
    const uint32_t overwrite[]={4u<<16|83u,variable_type[source],alias,source,
                                3u<<16|62u,alias,integer_one};
    words.insert(words.begin()+alias_at,std::begin(overwrite),std::end(overwrite));
    rejected(words,"overwrite-alias",argv[2]);

    words=original;
    const uint32_t copy[]={3u<<16|63u,source,offset_pointer};
    words.insert(words.begin()+alias_at,std::begin(copy),std::end(copy));
    rejected(words,"copy-memory",argv[2]);

    words=original;
    uint32_t uint64_type=words[3]++,wide_zero=words[3]++;
    const uint32_t capability[]={2u<<16|17u,11u}; // Int64
    words.insert(words.begin()+5,std::begin(capability),std::end(capability));
    size_t first_function=5;
    while((words[first_function]&0xffff)!=54)first_function+=words[first_function]>>16;
    const uint32_t wide_declarations[]={4u<<16|21u,uint64_type,64u,0u,
        5u<<16|43u,uint64_type,wide_zero,0u,0u};
    words.insert(words.begin()+first_function,std::begin(wide_declarations),std::end(wide_declarations));
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffff)==345){words[at+5]=wide_zero;break;}
    rejected(words,"wide-index",argv[2]);

    words=original;
    uint32_t float_type=0,glsl=0;
    first_function=5;
    while((words[first_function]&0xffff)!=54) {
        uint32_t op=words[first_function]&0xffff;
        if(op==22 && words[first_function+2]==32)float_type=words[first_function+1];
        if(op==11)glsl=words[first_function+1];
        first_function+=words[first_function]>>16;
    }
    assert(float_type && glsl);
    uint32_t one_float=words[3]++,frexp_result=words[3]++;
    const uint32_t float_constant[]={4u<<16|43u,float_type,one_float,0x3f800000u};
    words.insert(words.begin()+first_function,std::begin(float_constant),std::end(float_constant));
    const uint32_t frexp[]={7u<<16|12u,float_type,frexp_result,glsl,51u,one_float,source};
    words.insert(words.begin()+alias_at+4,std::begin(frexp),std::end(frexp));
    rejected(words,"out-pointer-write",argv[2]);

    uint32_t integer_four=0;
    for(size_t at=5;at<original.size();at+=original[at]>>16)
        if((original[at]&0xffff)==43 && (original[at]>>16)==4 &&
           original[at+1]==pointee[variable_type[source]] && original[at+3]==4)integer_four=original[at+2];
    assert(integer_four);
    words=original;
    const uint32_t sized_copy[]={4u<<16|64u,source,offset_pointer,integer_four};
    words.insert(words.begin()+alias_at,std::begin(sized_copy),std::end(sized_copy));
    before=words;
    assert(xbg_spirv_narrow(words,16,0x30)==0 && words==before);

    if(argc==8) {
        const int expected[]={SPV_NARROW_GUARD,SPV_NARROW_GUARD|SPV_NARROW_STEP,
            SPV_NARROW_GUARD,SPV_NARROW_GUARD,SPV_NARROW_GUARD|SPV_NARROW_STEP};
        for(unsigned i=0;i<5;++i) {
            auto real=load(argv[i+3]);
            assert(xbg_spirv_narrow(real,16,0x30)==expected[i]);
            save(std::string(argv[2])+"/real-"+std::to_string(i)+".spv",real);
        }
    }
    return 0;
}
