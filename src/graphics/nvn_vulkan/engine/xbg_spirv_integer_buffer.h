#pragma once
#include "xbg_spirv_stencil.h"
#include <algorithm>

// This is a format-agnostic structural analysis. The caller must additionally
// verify that the effective bound view is R16_UINT or R32_UINT before selecting a variant.
// Only scalar extracts carried directly, or through a single-store Function
// variable whose store dominates its loads, into integer bitcasts are accepted.
namespace xbg_integer_buffer_detail {
struct Instruction { size_t at; uint32_t count,op,block; };
struct Analysis {
    std::vector<Instruction> instructions;
    std::unordered_map<uint32_t,size_t> definitions;
    std::unordered_map<uint32_t,std::vector<uint32_t>> predecessors;
    std::unordered_set<uint32_t> entries;
    std::unordered_set<uint32_t> loads,images,fetches,extracts,variables,values;
    size_t variable_at=0,pointer_at=0,image_at=0;
    uint32_t descriptor=0,float_type=0,float_vector=0,uint_type=0;
    bool unsupported_control_flow=false;
};

inline bool parse(const std::vector<uint32_t> &w,Analysis &a){
    if(w.size()<5 || w[0]!=0x07230203u || !w[3] || w[4])return false;
    uint32_t block=0;bool entry_pending=false;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n || n>w.size()-i)return false;
        uint32_t minimum=1;
        switch(op){
        case 21:case 23:case 32:case 59:case 61:case 100:case 124:minimum=4;break;
        case 22:case 27:case 62:minimum=3;break;
        case 25:minimum=9;break;
        case 71:minimum=3;break;
        case 81:case 95:minimum=5;break;
        case 248:minimum=2;break;
        case 249:minimum=2;break;
        case 250:minimum=4;break;
        default:break;
        }
        if(n<minimum)return false;
        if(op==54){block=0;entry_pending=true;}
        if(op==248){block=w[i+1];if(entry_pending){a.entries.insert(block);entry_pending=false;}}
        if(op==249)a.predecessors[w[i+1]].push_back(block);
        if(op==250){a.predecessors[w[i+2]].push_back(block);a.predecessors[w[i+3]].push_back(block);}
        if(op==251)a.unsupported_control_flow=true;
        if(op==56)block=0;
        a.instructions.push_back({i,n,op,block});
        if(op>=19 && op<=33){
            if(n<2 || !w[i+1] || w[i+1]>=w[3] || a.definitions.count(w[i+1]))return false;
            a.definitions[w[i+1]]=i;
        }
        if(op==59){
            if(!w[i+2] || w[i+2]>=w[3] || a.definitions.count(w[i+2]))return false;
            a.definitions[w[i+2]]=i;
        }
        if(op==21 && n==4 && w[i+2]==32 && w[i+3]==0)a.uint_type=w[i+1];
        i+=n;
    }
    return true;
}

inline bool dominates(const Analysis &a,uint32_t store_block,uint32_t load_block){
    if(!store_block || !load_block || a.unsupported_control_flow)return false;
    std::vector<uint32_t> pending={load_block};
    std::unordered_set<uint32_t> visited;
    while(!pending.empty()){
        uint32_t block=pending.back();pending.pop_back();
        if(block==store_block || !visited.insert(block).second)continue;
        if(a.entries.count(block))return false;
        auto pred=a.predecessors.find(block);
        if(pred==a.predecessors.end() || pred->second.empty())return false;
        pending.insert(pending.end(),pred->second.begin(),pred->second.end());
    }
    return true;
}

inline bool type(const std::vector<uint32_t> &w,const Analysis &a,uint32_t id,uint32_t op,size_t &at){
    auto found=a.definitions.find(id);
    if(found==a.definitions.end())return false;
    at=found->second;
    return (w[at]&65535u)==op;
}

inline bool analyze(const std::vector<uint32_t> &w,uint32_t binding,Analysis &a){
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==71 && in.count==4 && w[i+2]==33 && w[i+3]==binding){
            if(a.descriptor)return false;
            a.descriptor=w[i+1];
        }
    }
    size_t sampled_at=0,float_at=0;
    if(!a.descriptor || !type(w,a,a.descriptor,59,a.variable_at))return false;
    size_t vi=a.variable_at;
    if((w[vi]>>16)!=4 || w[vi+3]!=0 || !type(w,a,w[vi+1],32,a.pointer_at))return false;
    size_t pi=a.pointer_at;
    if((w[pi]>>16)!=4 || w[pi+2]!=0 || !type(w,a,w[pi+3],27,sampled_at))return false;
    if((w[sampled_at]>>16)!=3 || !type(w,a,w[sampled_at+2],25,a.image_at))return false;
    size_t ii=a.image_at;
    if((w[ii]>>16)!=9 || w[ii+3]!=5 || w[ii+4]!=0 || w[ii+5]!=0 || w[ii+6]!=0 || w[ii+7]!=1 || w[ii+8]!=0)return false;
    a.float_type=w[ii+2];
    if(!type(w,a,a.float_type,22,float_at) || (w[float_at]>>16)!=3 || w[float_at+2]!=32)return false;
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==23 && in.count==4 && w[i+2]==a.float_type && w[i+3]==4)a.float_vector=w[i+1];
        if(in.op==61 && in.count==4 && w[i+3]==a.descriptor && w[i+1]==w[sampled_at+1])a.loads.insert(w[i+2]);
    }
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==100 && in.count==4 && a.loads.count(w[i+3]) && w[i+1]==w[ii+1])a.images.insert(w[i+2]);
    }
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==95 && a.images.count(w[i+3])){
            if(w[i+1]!=a.float_vector || in.count>6 || (in.count==6 && w[i+5]))return false;
            a.fetches.insert(w[i+2]);
        }
    }
    if(a.fetches.empty())return false;
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==81 && a.fetches.count(w[i+3])){
            if(in.count!=5 || w[i+1]!=a.float_type || w[i+4]>3)return false;
            a.extracts.insert(w[i+2]);
        }
    }
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==62 && a.extracts.count(w[i+2])){
            size_t variable_at=0,pointer_at=0;
            if(in.count!=3 || !in.block || !type(w,a,w[i+1],59,variable_at) || (w[variable_at]>>16)!=4 || w[variable_at+3]!=7)return false;
            if(!type(w,a,w[variable_at+1],32,pointer_at) || (w[pointer_at]>>16)!=4 || w[pointer_at+2]!=7 || w[pointer_at+3]!=a.float_type)return false;
            a.variables.insert(w[i+1]);
        }
    }
    for(uint32_t variable:a.variables){
        size_t stored_at=0;uint32_t stored_block=0,stores=0,loads=0;
        for(const auto &in:a.instructions){
            size_t i=in.at;
            if(in.op==62 && w[i+1]==variable){
                if(in.count!=3 || !a.extracts.count(w[i+2]))return false;
                ++stores;stored_at=i;stored_block=in.block;
            }
        }
        if(stores!=1)return false;
        for(const auto &in:a.instructions){
            size_t i=in.at;
            if(in.op==61 && w[i+3]==variable){
                if(in.count!=4 || w[i+1]!=a.float_type || !dominates(a,stored_block,in.block) || (in.block==stored_block && i<=stored_at))return false;
                a.values.insert(w[i+2]);++loads;
            }
        }
        if(!loads)return false;
    }
    // Every occurrence of an affected ID must be a recognized declaration,
    // debug name, or one of the exact data-flow operands checked above. Unknown
    // instructions are conservatively rejected even if a literal equals an ID.
    std::unordered_set<uint32_t> all={a.descriptor};
    for(const auto *ids:{&a.loads,&a.images,&a.fetches,&a.extracts,&a.variables,&a.values})all.insert(ids->begin(),ids->end());
    std::unordered_set<uint32_t> consumed;
    for(const auto &in:a.instructions){
        size_t i=in.at;
        // Debug strings and line numbers are literals, not value consumers.
        if(in.op>=2 && in.op<=8)continue;
        if(in.op==317 || in.op==330)continue;
        if(in.op==43 || in.op==50)continue; // Scalar constant payload is literal.
        // Runtime values cannot occur in a valid type declaration. StorageClass
        // enumerants can otherwise collide with a descriptor's numeric ID.
        if(in.op>=19 && in.op<=39)continue;
        if(in.op==71){
            if(!all.count(w[i+1]))continue;
            if(w[i+1]==a.descriptor && in.count==4 && (w[i+2]==33 || w[i+2]==34))continue;
            return false;
        }
        for(uint32_t j=1;j<in.count;++j){
            if(in.op==59 && j==3)continue; // StorageClass is a literal.
            uint32_t id=w[i+j];
            if(!all.count(id))continue;
            bool allowed=false,consume=false;
            if(in.op==59 && j==2 && (id==a.descriptor || a.variables.count(id)))allowed=true;
            if(in.op==61 && in.count==4){
                if(j==2 && (a.loads.count(id) || a.values.count(id)))allowed=true;
                if(j==3 && (id==a.descriptor || a.variables.count(id))){allowed=true;consume=true;}
            }
            if(in.op==100 && in.count==4){
                if(j==2 && a.images.count(id))allowed=true;
                if(j==3 && a.loads.count(id)){allowed=true;consume=true;}
            }
            if(in.op==95 && a.fetches.count(w[i+2])){
                if(j==2 && a.fetches.count(id))allowed=true;
                if(j==3 && a.images.count(id)){allowed=true;consume=true;}
            }
            if(in.op==81 && in.count==5 && a.extracts.count(w[i+2])){
                if(j==2 && a.extracts.count(id))allowed=true;
                if(j==3 && a.fetches.count(id)){allowed=true;consume=true;}
            }
            if(in.op==62 && in.count==3 && a.variables.count(w[i+1]) && a.extracts.count(w[i+2])){allowed=true;consume=true;}
            if(in.op==124 && in.count==4 && j==3 && (a.extracts.count(id) || a.values.count(id))){
                size_t int_at=0;
                allowed=type(w,a,w[i+1],21,int_at) && (w[int_at]>>16)==4 && w[int_at+2]==32 && w[int_at+3]<=1;
                consume=allowed;
            }
            if(!allowed)return false;
            if(consume)consumed.insert(id);
        }
    }
    for(uint32_t id:all)if(!consumed.count(id))return false;
    return true;
}
} // namespace xbg_integer_buffer_detail

inline std::vector<uint32_t> xbg_spirv_integer_buffer_candidates(const std::vector<uint32_t> &words){
    using namespace xbg_integer_buffer_detail;
    Analysis parsed;
    if(!parse(words,parsed))return {};
    std::vector<uint32_t> bindings;
    for(const auto &in:parsed.instructions){
        size_t i=in.at;
        if(in.op!=71 || in.count!=4 || words[i+2]!=33)continue;
        size_t variable_at=0,pointer_at=0,sampled_at=0,image_at=0;
        if(!type(words,parsed,words[i+1],59,variable_at) || !type(words,parsed,words[variable_at+1],32,pointer_at))continue;
        if(!type(words,parsed,words[pointer_at+3],27,sampled_at) || !type(words,parsed,words[sampled_at+2],25,image_at) || words[image_at+3]!=5)continue;
        Analysis a=parsed;
        if(analyze(words,words[i+3],a))bindings.push_back(words[i+3]);
    }
    std::sort(bindings.begin(),bindings.end());
    bindings.erase(std::unique(bindings.begin(),bindings.end()),bindings.end());
    return bindings;
}

// Positive = rewritten fetch count, zero = unsupported shape, -1 = malformed
// instruction stream. Rejected input is always left byte-for-byte unchanged.
inline int xbg_spirv_integer_buffer_rewrite(std::vector<uint32_t> &words,uint32_t binding){
    using namespace xbg_integer_buffer_detail;
    Analysis a;
    if(!parse(words,a))return -1;
    if(!analyze(words,binding,a))return 0;
    uint32_t bound=words[3],uint_type=a.uint_type,uint_vector=0,function_pointer=0;
    std::vector<uint32_t> declarations;
    if(!uint_type){uint_type=bound++;declarations.insert(declarations.end(),{(4u<<16)|21u,uint_type,32u,0u});}
    auto types=xbg_integer_image_types(words,a.image_at,a.pointer_at,a.variable_at,uint_type,true,bound);
    declarations.insert(declarations.end(),types.declarations.begin(),types.declarations.end());
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(in.op==23 && in.count==4 && words[i+2]==uint_type && words[i+3]==4)uint_vector=words[i+1];
        if(in.op==32 && in.count==4 && words[i+2]==7 && words[i+3]==uint_type)function_pointer=words[i+1];
    }
    if(!uint_vector){uint_vector=bound++;declarations.insert(declarations.end(),{(4u<<16)|23u,uint_vector,uint_type,4u});}
    if(!a.variables.empty() && !function_pointer){function_pointer=bound++;declarations.insert(declarations.end(),{(4u<<16)|32u,function_pointer,7u,uint_type});}
    std::vector<uint32_t> out(words.begin(),words.begin()+5);
    for(const auto &in:a.instructions){
        size_t i=in.at;
        if(types.moved.count(i))continue;
        if(i==a.variable_at)out.insert(out.end(),declarations.begin(),declarations.end());
        size_t at=out.size();out.insert(out.end(),words.begin()+i,words.begin()+i+in.count);
        if(i==a.variable_at)out[at+1]=types.pointer;
        if(in.op==59 && a.variables.count(words[i+2]))out[at+1]=function_pointer;
        if(in.op==61 && a.loads.count(words[i+2]))out[at+1]=types.sampled;
        if(in.op==100 && a.images.count(words[i+2]))out[at+1]=types.image;
        if(in.op==95 && a.fetches.count(words[i+2]))out[at+1]=uint_vector;
        if((in.op==81 && a.extracts.count(words[i+2])) || (in.op==61 && a.values.count(words[i+2])))out[at+1]=uint_type;
    }
    out[3]=bound;words.swap(out);
    return int(a.fetches.size());
}
