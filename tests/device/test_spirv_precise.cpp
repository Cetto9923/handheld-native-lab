#include "xbg_spirv_precise.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static std::vector<uint32_t> load(const char *path) {
    std::ifstream file(path,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    assert(!bytes.empty() && bytes.size()%4==0);
    std::vector<uint32_t> words(bytes.size()/4);
    std::memcpy(words.data(),bytes.data(),bytes.size());
    return words;
}
static void save(const std::string &path,const std::vector<uint32_t> &words) {
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(words.data()),words.size()*4);
    assert(file.good());
}
static size_t find_op(const std::vector<uint32_t> &words,uint32_t op) {
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffff)==op)return at;
    assert(false);return 0;
}
static size_t annotations_end(const std::vector<uint32_t> &words) {
    size_t at=5;
    while(at<words.size() && spv_precise_head(words[at]&0xffff))at+=words[at]>>16;
    return at;
}
static std::vector<uint32_t> without_precise(const std::vector<uint32_t> &words) {
    std::vector<uint32_t> result(words.begin(),words.begin()+5);
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        size_t length=words[at]>>16;
        if((words[at]&0xffff)==71 && length==3 && words[at+2]==42)continue;
        result.insert(result.end(),words.begin()+at,words.begin()+at+length);
    }
    return result;
}
static std::vector<uint32_t> arithmetic_results(const std::vector<uint32_t> &words) {
    std::vector<uint32_t> ids;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if(spv_precise_op(words[at]&0xffff))ids.push_back(words[at+2]);
    return ids;
}
static void decorate(std::vector<uint32_t> &words,const std::vector<uint32_t> &ids) {
    std::vector<uint32_t> annotations;
    for(uint32_t id:ids)annotations.insert(annotations.end(),{3u<<16|71u,id,42});
    words.insert(words.begin()+annotations_end(words),annotations.begin(),annotations.end());
}
static void unchanged(std::vector<uint32_t> words) {
    const auto before=words;
    assert(xbg_spirv_precise(words)==0 && words==before);
}
static void rewrite(const std::vector<uint32_t> &source,size_t expected,
                    const std::string &prefix) {
    auto words=source;
    const size_t added=xbg_spirv_precise(words);
    assert(added==expected && words.size()==source.size()+3*added);
    assert(without_precise(words)==without_precise(source));
    const size_t old_function=find_op(source,54),new_function=find_op(words,54);
    assert(std::equal(source.begin()+old_function,source.end(),words.begin()+new_function,words.end()));
    save(prefix+"-rewritten.spv",words);
    const auto once=words;
    assert(xbg_spirv_precise(words)==0 && words==once);
    save(prefix+"-idempotent.spv",words);
    std::printf("precise added=%zu function_words_unchanged=%zu idempotent=1 file=%s\n",added,source.size()-old_function,prefix.c_str());
}
int main(int argc,char **argv) {
    assert(argc>=3 && argc<=5);
    const std::string directory=argv[2];
    const auto original=load(argv[1]);
    const auto results=arithmetic_results(original);
    assert(results.size()==5);
    rewrite(original,5,directory+"/vertex");

    auto words=original;
    decorate(words,{results[0],results[2]});
    save(directory+"/partial-source.spv",words);
    rewrite(words,3,directory+"/partial");
    words=original;decorate(words,results);
    save(directory+"/complete-source.spv",words);unchanged(words);

    // Explicit Fma remains an OpExtInst with identical operands and result.
    const size_t fma=find_op(original,12);
    words=original;assert(xbg_spirv_precise(words)==5);
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        assert(!((words[at]&0xffff)==71 && words[at+1]==original[fma+2] && words[at+2]==42));

    words=original;
    const size_t entry=find_op(words,15);
    const uint32_t function=words[entry+2];
    words[entry+1]=4; // Fragment
    size_t decoration=find_op(words,71);words[decoration+2]=30; // Location 0
    const uint32_t origin[]={3u<<16|16u,function,7};
    words.insert(words.begin()+entry+(words[entry]>>16),std::begin(origin),std::end(origin));
    save(directory+"/fragment-source.spv",words);unchanged(words);

    words=original;
    for(size_t at=5;at<words.size();) {
        const size_t length=words[at]>>16;
        const uint32_t op=words[at]&0xffff;
        if(op==71 || op==59 || op==62){words.erase(words.begin()+at,words.begin()+at+length);continue;}
        at+=length;
    }
    size_t compute_entry=find_op(words,15);
    const size_t entry_length=words[compute_entry]>>16;
    words[compute_entry+1]=5;
    words.erase(words.begin()+compute_entry+entry_length-1);
    words[compute_entry]=uint32_t(entry_length-1)<<16|15u;
    const uint32_t local_size[]={6u<<16|16u,function,17,1,1,1};
    words.insert(words.begin()+compute_entry+entry_length-1,std::begin(local_size),std::end(local_size));
    save(directory+"/compute-source.spv",words);unchanged(words);

    words=original;
    const uint32_t group=words[3]++;
    const uint32_t group_annotations[]={2u<<16|73u,group,3u<<16|71u,group,42,
                                        3u<<16|74u,group,results[0]};
    words.insert(words.begin()+annotations_end(words),std::begin(group_annotations),std::end(group_annotations));
    save(directory+"/group-source.spv",words);unchanged(words);

    words=original;
    std::vector<uint32_t> second(words.begin()+entry,words.begin()+entry+(words[entry]>>16));
    second[3]=0x65736c65; // "else", existing following NUL retained
    words.insert(words.begin()+entry+(words[entry]>>16),second.begin(),second.end());
    save(directory+"/multiple-source.spv",words);unchanged(words);

    words=original;words.resize(4);unchanged(words);
    words=original;words[3]=0;unchanged(words);
    words=original;words[3]=(1u<<20)+1;unchanged(words);
    words=original;words[3]=1u<<20;
    save(directory+"/max-bound-source.spv",words);
    rewrite(words,5,directory+"/max-bound");
    words=original;words[4]=1;unchanged(words);
    words=original;words[5]=0;unchanged(words);
    words=original;words[5]=0xffff0000u|17u;unchanged(words);
    words=original;words.pop_back();unchanged(words);
    words=original;words[entry+1]=9999;unchanged(words);
    words=original;words[entry+2]=words[3];unchanged(words);
    words=original;
    for(size_t i=entry+3;i<entry+(words[entry]>>16);++i)words[i]=0x41414141;
    unchanged(words);
    words=original;words[find_op(words,129)+2]=words[3];unchanged(words);
    words=original;words[find_op(words,129)+3]=words[3];unchanged(words);
    words=original;words[find_op(words,129)]=4u<<16|129u;unchanged(words);
    words=original;decorate(words,{results[0]});
    decoration=find_op(words,71);
    while(words[decoration+2]!=42)decoration+=words[decoration]>>16;
    words[decoration]=4u<<16|71u;unchanged(words);

    for(int i=3;i<argc;++i) {
        const auto real=load(argv[i]);
        assert(arithmetic_results(real).size()==492);
        rewrite(real,492,directory+"/real-"+std::to_string(i-3));
    }
    std::puts("precise contracts PASS vertex=1 partial=1 complete=1 fma=1 fragment_compute_unchanged=1 group_multiple_unchanged=1 malformed_transactional=1");
}
