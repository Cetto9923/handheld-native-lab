#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xbg_vertex_input_detail {
struct Instruction { size_t at;uint32_t length,op;bool function; };
struct Type {
    uint32_t op=0,element=0,count=0,storage=0,width=0,numeric=3;
    size_t at=0;
};
struct Pointer { uint32_t pointee,numeric; };
struct Load { size_t at;uint32_t type,result,pointer,raw=0,new_type=0; };
}

// Numeric classes match xbg_vertex_input_classes: 0 float, 1 SINT, 2 UINT.
// Retype scalar/vector32 VS inputs and their closed component-access chains.
// Loads recover the original register bits through the original result ID.
// Return 1 changed, 0 already matched/unused requests, -1 unsupported. A zero
// or negative result leaves the complete input module unchanged.
inline int xbg_spirv_vertex_input(std::vector<uint32_t> &words,
    const std::vector<std::pair<uint32_t,uint32_t>> &requests) {
    using namespace xbg_vertex_input_detail;
    if(requests.empty())return 0;
    if(words.size()<5 || words[0]!=0x07230203 || !words[3] || words[3]>(1u<<20) ||
       words[4] || words[1]<0x00010000 || words[1]>0x00010600 || (words[1]&0xff))return -1;
    const uint32_t bound=words[3];
    auto valid_id=[&](uint32_t id){return id && id<bound;};
    std::map<uint32_t,uint32_t> requested;
    for(const auto &r:requests) {
        if(r.second>2)return -1;
        auto inserted=requested.emplace(r);
        if(!inserted.second && inserted.first->second!=r.second)return -1;
    }
    std::vector<Instruction> instructions;
    std::unordered_map<uint32_t,Type> types;
    std::unordered_map<uint32_t,uint32_t> locations,components;
    std::unordered_set<uint32_t> interface;
    unsigned entries=0;
    bool in_function=false;
    for(size_t at=5;at<words.size();) {
        const uint32_t *x=words.data()+at;
        const uint32_t length=x[0]>>16,op=x[0]&0xffff;
        if(!length || length>words.size()-at)return -1;
        if(op==73 || op==74 || op==75)return -1;
        if(op==15) {
            if(length<4 || ++entries!=1 || x[1]!=0 || !valid_id(x[2]))return -1;
            size_t name=3;
            while(name<length && (x[name]&0xff) && (x[name]&0xff00) &&
                  (x[name]&0xff0000) && (x[name]&0xff000000))++name;
            if(name==length)return -1;
            for(size_t i=name+1;i<length;++i) {
                if(!valid_id(x[i]))return -1;
                interface.insert(x[i]);
            }
        }
        if(op==71) {
            if(length<3 || !valid_id(x[1]))return -1;
            if(x[2]==30 || x[2]==31) {
                if(length!=4)return -1;
                auto &map=x[2]==30?locations:components;
                if(!map.emplace(x[1],x[3]).second)return -1;
            }
        }
        if(op==21 || op==22 || op==23 || op==32) {
            if(length!=(op==22?3u:4u) || !valid_id(x[1]) || types.count(x[1]))return -1;
            Type t;t.op=op;t.at=at;
            if(op==21 || op==22) {
                t.width=x[2];
                if(op==21 && x[3]>1)return -1;
                t.numeric=op==22?0:x[3]?1:2;
            } else {
                t.element=op==23?x[2]:x[3];
                if(!valid_id(t.element))return -1;
                if(op==23)t.count=x[3];else t.storage=x[2];
            }
            types.emplace(x[1],t);
        }
        if(op==54) {
            if(length!=5 || in_function || !valid_id(x[1]) || !valid_id(x[2]) || !valid_id(x[4]))return -1;
            in_function=true;
        }
        instructions.push_back({at,length,op,in_function});
        if(op==56) {
            if(length!=1 || !in_function)return -1;
            in_function=false;
        }
        if(op==59 && (length<4 || length>5 || !valid_id(x[1]) || !valid_id(x[2])))return -1;
        if(op==61 || op==65 || op==66) {
            if(length<(op==61?4u:5u) || !valid_id(x[1]) || !valid_id(x[2]))return -1;
            for(uint32_t i=3;i<(op==61?4u:length);++i)if(!valid_id(x[i]))return -1;
        }
        at+=length;
    }
    if(entries!=1 || in_function)return -1;
    auto shape=[&](uint32_t id,uint32_t &numeric,uint32_t &count) {
        auto found=types.find(id);
        if(found==types.end())return false;
        const Type *t=&found->second;count=1;
        if(t->op==23) {
            count=t->count;
            found=types.find(t->element);
            if(count<2 || count>4 || found==types.end())return false;
            t=&found->second;
        }
        if((t->op!=21 && t->op!=22) || t->width!=32)return false;
        numeric=t->numeric;return true;
    };
    std::unordered_map<uint32_t,Pointer> pointers;
    std::map<size_t,uint32_t> pointer_sites;
    std::unordered_set<uint32_t> selected_locations;
    size_t insert_at=words.size();
    for(const auto &in:instructions)if(in.op==59) {
        const uint32_t *x=words.data()+in.at;
        auto location=locations.find(x[2]);
        if(x[3]!=1 || location==locations.end())continue;
        auto request=requested.find(location->second);
        if(request==requested.end())continue;
        auto pointer=types.find(x[1]);uint32_t numeric=0,count=0;
        if(in.function || in.length!=4 || components.count(x[2]) || !interface.count(x[2]) ||
           !selected_locations.insert(location->second).second || pointer==types.end() ||
           pointer->second.op!=32 || pointer->second.storage!=1 ||
           !shape(pointer->second.element,numeric,count))return -1;
        if(numeric==request->second)continue;
        pointers.emplace(x[2],Pointer{pointer->second.element,request->second});
        pointer_sites.emplace(in.at,x[2]);insert_at=std::min(insert_at,in.at);
    }
    if(pointers.empty())return 0;
    std::vector<Load> loads;
    for(const auto &in:instructions) {
        const uint32_t *x=words.data()+in.at;
        if(in.op==65 || in.op==66) {
            auto base=pointers.find(x[3]);
            if(base==pointers.end())continue;
            auto source=types.find(base->second.pointee),pointer=types.find(x[1]);
            if(in.length!=5 || !valid_id(x[4]) || source==types.end() || source->second.op!=23 ||
               pointer==types.end() || pointer->second.op!=32 || pointer->second.storage!=1 ||
               pointer->second.element!=source->second.element || pointers.count(x[2]))return -1;
            pointers.emplace(x[2],Pointer{pointer->second.element,base->second.numeric});
            pointer_sites.emplace(in.at,x[2]);
        }
        if(in.op==61) {
            auto pointer=pointers.find(x[3]);
            if(pointer==pointers.end())continue;
            if(in.length!=4 || x[1]!=pointer->second.pointee)return -1;
            loads.push_back({in.at,x[1],x[2],x[3]});
        }
    }
    // Pointer operands are closed over the selected variables and access chains.
    // Skip established literal positions, so an extended opcode or shuffle lane
    // numerically equal to a pointer ID cannot be mistaken for a pointer escape.
    for(const auto &in:instructions) {
        const uint32_t *x=words.data()+in.at;
        if(!in.function) {
            if(in.op==59 && in.length>4 && pointers.count(x[4]))return -1;
            continue;
        }
        for(uint32_t i=1;i<in.length;++i)if(pointers.count(x[i])) {
            if(((in.op==65 || in.op==66) && (i==2 || i==3)) || (in.op==61 && i==3))continue;
            if((in.op==8 && i>=2) || (in.op==12 && i==4) ||
               (in.op==79 && i>=5) || (in.op==81 && i>=4) || (in.op==82 && i>=5) ||
               (in.op==246 && i>=3) || (in.op==247 && i==2) || (in.op==250 && i>=4))continue;
            return -1;
        }
    }
    uint32_t next_id=bound;
    std::vector<uint32_t> declarations;
    std::unordered_set<size_t> moved;
    std::unordered_set<uint32_t> emitted;
    bool supported=true;
    auto allocate=[&]() {
        if(next_id>=(1u<<20)){supported=false;return 0u;}
        return next_id++;
    };
    std::function<void(uint32_t)> emit=[&](uint32_t id) {
        auto found=types.find(id);
        if(found==types.end() || !emitted.insert(id).second)return;
        const Type t=found->second;
        if(t.at<insert_at)return;
        if(t.op==23 || t.op==32)emit(t.element);
        const size_t length=words[t.at]>>16;
        declarations.insert(declarations.end(),words.begin()+t.at,words.begin()+t.at+length);
        moved.insert(t.at);
    };
    auto numeric_type=[&](uint32_t numeric,uint32_t count) {
        uint32_t scalar=0,result=0;
        for(const auto &pair:types) {
            const Type &t=pair.second;
            if((t.op==21 || t.op==22) && t.width==32 && t.numeric==numeric)scalar=pair.first;
        }
        if(!scalar) {
            scalar=allocate();
            if(numeric==0)declarations.insert(declarations.end(),{3u<<16|22u,scalar,32});
            else declarations.insert(declarations.end(),{4u<<16|21u,scalar,32,uint32_t(numeric==1)});
            Type t;t.op=numeric?21:22;t.width=32;t.numeric=numeric;types.emplace(scalar,t);emitted.insert(scalar);
        } else emit(scalar);
        if(count==1)return scalar;
        for(const auto &pair:types)if(pair.second.op==23 && pair.second.element==scalar && pair.second.count==count)result=pair.first;
        if(!result) {
            result=allocate();declarations.insert(declarations.end(),{4u<<16|23u,result,scalar,count});
            Type t;t.op=23;t.element=scalar;t.count=count;types.emplace(result,t);emitted.insert(result);
        } else emit(result);
        return result;
    };
    std::unordered_map<size_t,uint32_t> replacement_types;
    for(const auto &site:pointer_sites) {
        const Pointer p=pointers.at(site.second);uint32_t old_numeric=0,count=0;
        if(!shape(p.pointee,old_numeric,count))return -1;
        const uint32_t value=numeric_type(p.numeric,count);uint32_t pointer=0;
        for(const auto &pair:types)if(pair.second.op==32 && pair.second.storage==1 && pair.second.element==value)pointer=pair.first;
        if(!pointer) {
            pointer=allocate();declarations.insert(declarations.end(),{4u<<16|32u,pointer,1,value});
            Type t;t.op=32;t.element=value;t.storage=1;types.emplace(pointer,t);emitted.insert(pointer);
        } else emit(pointer);
        replacement_types.emplace(site.first,pointer);
    }
    for(auto &load:loads) {
        const Pointer p=pointers.at(load.pointer);uint32_t numeric=0,count=0;
        if(!shape(load.type,numeric,count))return -1;
        load.new_type=numeric_type(p.numeric,count);load.raw=allocate();
    }
    if(!supported)return -1;
    std::vector<uint32_t> output(words.begin(),words.begin()+5);
    size_t load_index=0;
    for(const auto &in:instructions) {
        if(in.at==insert_at)output.insert(output.end(),declarations.begin(),declarations.end());
        if(moved.count(in.at))continue;
        const size_t at=output.size();
        output.insert(output.end(),words.begin()+in.at,words.begin()+in.at+in.length);
        auto replacement=replacement_types.find(in.at);
        if(replacement!=replacement_types.end())output[at+1]=replacement->second;
        if(load_index<loads.size() && loads[load_index].at==in.at) {
            const Load &load=loads[load_index++];output[at+1]=load.new_type;output[at+2]=load.raw;
            output.insert(output.end(),{4u<<16|124u,load.type,load.result,load.raw});
        }
    }
    output[3]=next_id;words.swap(output);return 1;
}
