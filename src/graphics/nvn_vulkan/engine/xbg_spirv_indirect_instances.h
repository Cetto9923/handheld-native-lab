#pragma once
#include "xbg_spirv_integer_buffer.h"

struct XbgIndirectInstanceField {
    uint32_t binding;
    uint32_t axis;
};

// Recognizes writes to word 1 of NVN's five-word indexed-indirect records.
// The caller still proves effective resource formats/ranges, other descriptor
// aliases, dispatch extents, input freshness, and GPU allocation lifetimes.
namespace xbg_indirect_instances_detail {
using Instruction=xbg_integer_buffer_detail::Instruction;
struct Affine {
    bool valid=false;
    int axis=-1;
    uint32_t scale=0,offset=0;
};

struct Context {
    const std::vector<uint32_t> &words;
    xbg_integer_buffer_detail::Analysis parsed;
    std::unordered_map<uint32_t,Instruction> definitions;
    std::unordered_map<uint32_t,std::vector<Instruction>> stores;
    std::unordered_set<uint32_t> workgroups;

    const Instruction *definition(uint32_t id)const{
        auto found=definitions.find(id);
        return found==definitions.end()?nullptr:&found->second;
    }
    bool scalar32(uint32_t type,bool integer_only=false)const{
        const auto *d=definition(type);
        if(!d)return false;
        if(d->op==21)return d->count==4 && words[d->at+2]==32 && words[d->at+3]<=1;
        return !integer_only && d->op==22 && d->count==3 && words[d->at+2]==32;
    }
    bool uint32_type(uint32_t type)const{
        const auto *d=definition(type);
        return d && d->op==21 && d->count==4 && words[d->at+2]==32 && words[d->at+3]==0;
    }
    bool constant(uint32_t id,uint32_t &value)const{
        const auto *d=definition(id);
        if(!d || d->op!=43 || d->count!=4 || !scalar32(words[d->at+1],true))return false;
        value=words[d->at+3];return true;
    }
    // Enumerants, strings and indices are not ID uses. Unknown function-body
    // instructions conservatively treat every remaining operand as an ID.
    static bool id_operand(const Instruction &in,uint32_t operand){
        if((in.op>=2 && in.op<=11) || (in.op>=14 && in.op<=17) ||
           (in.op>=19 && in.op<=52) || in.op==317 || in.op==330)return false;
        if(in.op==71 || in.op==72)return operand==1;
        if(in.op==12 && operand==4)return false;
        if(in.op==54 && operand==3)return false;
        if(in.op==59 && operand==3)return false;
        if(in.op==79 && operand>=5)return false;
        if(in.op==81 && operand>=4)return false;
        if(in.op==82 && operand>=5)return false;
        if(in.op==246 && operand>=3)return false;
        if(in.op==247 && operand==2)return false;
        if(in.op==250 && operand>=4)return false;
        if(in.op==251)return false; // Labels/literals; runtime pointers cannot be switch selectors.
        return true;
    }
    bool parse(){
        if(!xbg_integer_buffer_detail::parse(words,parsed))return false;
        bool function=false,unsupported_switch=false;
        for(const auto &in:parsed.instructions){
            size_t i=in.at;uint32_t result=0;
            if(in.op==54){if(function || in.count!=5)return false;function=true;}
            if(in.op==56){if(!function || in.count!=1)return false;function=false;}
            if(in.op==248 && !function)return false;
            if(in.op==251){
                // SPIRV-Tools merge-return wraps the entry point in a switch
                // with no cases. Its sole possible successor is the default.
                if(in.count==3 && in.block && words[i+2] && words[i+2]<words[3])parsed.predecessors[words[i+2]].push_back(in.block);
                else unsupported_switch=true;
            }
            if(in.op>=19 && in.op<=33)result=words[i+1];
            switch(in.op){
            case 43:case 59:case 61:case 83:case 124:
                if(in.count<4)return false;result=words[i+2];break;
            case 65:case 81:case 128:case 132:
                if(in.count<5)return false;result=words[i+2];break;
            case 60:
                if(in.count!=6)return false;result=words[i+2];break;
            case 234:
                if(in.count!=7)return false;result=words[i+2];break;
            case 62:
                stores[words[i+1]].push_back(in);break;
            default:break;
            }
            if(result){
                if(result>=words[3] || definitions.count(result))return false;
                definitions.emplace(result,in);
            }
            if(in.op==71 && in.count==4 && words[i+2]==11 && words[i+3]==26)workgroups.insert(words[i+1]);
        }
        parsed.unsupported_control_flow=unsupported_switch;
        return !function;
    }
    bool local(uint32_t variable,const Instruction &load,uint32_t &source)const{
        const auto *v=definition(variable);
        if(!v || v->op!=59 || v->count!=4 || words[v->at+3]!=7)return false;
        const auto *p=definition(words[v->at+1]);
        if(!p || p->op!=32 || p->count!=4 || words[p->at+2]!=7 || !scalar32(words[p->at+3]))return false;
        if(load.count!=4 || words[load.at+1]!=words[p->at+3])return false;
        auto found=stores.find(variable);
        if(found==stores.end() || found->second.size()!=1)return false;
        const auto &store=found->second.front();
        if(store.count!=3 || !xbg_integer_buffer_detail::dominates(parsed,store.block,load.block) ||
           (store.block==load.block && store.at>=load.at))return false;
        for(const auto &in:parsed.instructions)for(uint32_t j=1;j<in.count;++j){
            if(!id_operand(in,j) || words[in.at+j]!=variable)continue;
            if(in.op==59 && j==2)continue;
            if(in.op==61 && in.count==4 && j==3)continue;
            if(in.op==62 && in.count==3 && j==1)continue;
            return false;
        }
        source=words[store.at+2];return true;
    }
    bool workgroup(uint32_t variable)const{
        const auto *v=definition(variable);
        if(!workgroups.count(variable) || !v || v->op!=59 || v->count!=4 || words[v->at+3]!=1)return false;
        const auto *p=definition(words[v->at+1]);
        if(!p || p->op!=32 || p->count!=4 || words[p->at+2]!=1)return false;
        const auto *vector=definition(words[p->at+3]);
        return vector && vector->op==23 && vector->count==4 && words[vector->at+3]==3 && uint32_type(words[vector->at+2]);
    }
    Affine expression(uint32_t id,std::unordered_set<uint32_t> &visiting,unsigned depth=0)const{
        if(depth>96 || !visiting.insert(id).second)return {};
        struct Leave {std::unordered_set<uint32_t> &ids;uint32_t id;~Leave(){ids.erase(id);}} leave{visiting,id};
        const auto *d=definition(id);if(!d)return {};
        size_t i=d->at;uint32_t value=0;
        if(constant(id,value))return {true,-1,0,value};
        if(d->op==61 && d->count==4 && scalar32(words[i+1])){
            const auto *pointer=definition(words[i+3]);
            if(pointer && pointer->op==65 && pointer->count==5 && workgroup(words[pointer->at+3]) &&
               uint32_type(words[i+1]) && constant(words[pointer->at+4],value) && value<3)return {true,int(value),1,0};
            uint32_t source=0;
            if(local(words[i+3],*d,source))return expression(source,visiting,depth+1);
            return {};
        }
        if(d->op==81 && d->count==5 && uint32_type(words[i+1]) && words[i+4]<3){
            const auto *load=definition(words[i+3]);
            if(load && load->op==61 && load->count==4 && workgroup(words[load->at+3]))return {true,int(words[i+4]),1,0};
            return {};
        }
        if((d->op==124 || d->op==83) && d->count==4 && scalar32(words[i+1])){
            const auto *source=definition(words[i+3]);
            if(!source || !scalar32(words[source->at+1]))return {};
            return expression(words[i+3],visiting,depth+1);
        }
        if((d->op==128 || d->op==132) && d->count==5 && scalar32(words[i+1],true)){
            auto a=expression(words[i+3],visiting,depth+1),b=expression(words[i+4],visiting,depth+1);
            if(!a.valid || !b.valid || (a.axis>=0 && b.axis>=0 && a.axis!=b.axis))return {};
            if(d->op==128)return {true,a.axis>=0?a.axis:b.axis,a.scale+b.scale,a.offset+b.offset};
            if(a.axis>=0 && b.axis>=0)return {};
            return {true,a.axis>=0?a.axis:b.axis,a.scale*b.offset+b.scale*a.offset,a.offset*b.offset};
        }
        return {};
    }
    bool target(uint32_t variable,XbgIndirectInstanceField &fact)const{
        const auto *v=definition(variable);
        if(!v || v->op!=59 || v->count!=4 || words[v->at+3]!=0)return false;
        const auto *p=definition(words[v->at+1]);
        if(!p || p->op!=32 || p->count!=4 || words[p->at+2]!=0)return false;
        const auto *image=definition(words[p->at+3]);
        if(!image || image->op!=25 || image->count!=9 || !uint32_type(words[image->at+2]) ||
           words[image->at+3]!=5 || words[image->at+4] || words[image->at+5] || words[image->at+6] ||
           words[image->at+7]!=2 || words[image->at+8]!=33)return false;
        bool binding=false,set=false;uint32_t selected_binding=0;int axis=-1;
        std::unordered_set<uint32_t> pointers,used;
        for(const auto &in:parsed.instructions){
            size_t i=in.at;
            if(in.op==71 && words[i+1]==variable){
                if(in.count!=4)return false;
                if(words[i+2]==33){if(binding)return false;binding=true;selected_binding=words[i+3];}
                else if(words[i+2]==34){if(set || words[i+3])return false;set=true;}
                else return false;
            }
            if(in.op==60 && words[i+3]==variable){
                uint32_t sample=1;
                const auto *pointer=definition(words[i+1]);
                if(!pointer || pointer->op!=32 || pointer->count!=4 || words[pointer->at+2]!=11 || words[pointer->at+3]!=words[image->at+2] ||
                   !constant(words[i+5],sample) || sample)return false;
                std::unordered_set<uint32_t> visiting;
                auto coord=expression(words[i+4],visiting);
                if(!coord.valid || coord.axis<0 || coord.scale!=5 || coord.offset!=1 || (axis>=0 && coord.axis!=axis))return false;
                axis=coord.axis;pointers.insert(words[i+2]);
            }
        }
        if(!binding || !set || pointers.empty())return false;
        for(const auto &in:parsed.instructions)for(uint32_t j=1;j<in.count;++j){
            size_t i=in.at;if(!id_operand(in,j))continue;
            const uint32_t id=words[i+j];
            if(id==variable){
                if(in.op==59 && j==2)continue;
                if(in.op==71 && j==1)continue;
                if(in.op==60 && j==3)continue;
                return false;
            }
            if(!pointers.count(id))continue;
            if(in.op==60 && j==2)continue;
            if(in.op==234 && in.count==7 && j==3 && words[i+1]==words[image->at+2]){used.insert(id);continue;}
            return false;
        }
        if(used.size()!=pointers.size())return false;
        // The engine's descriptor identities contain a binding and use set 0.
        for(const auto &in:parsed.instructions)if(in.op==71 && in.count==4 && words[in.at+2]==33 &&
            words[in.at+3]==selected_binding && words[in.at+1]!=variable)return false;
        fact={selected_binding,uint32_t(axis)};return true;
    }
};
} // namespace xbg_indirect_instances_detail

// Positive = fact count, zero = unproved structure, -1 = malformed instruction
// stream. The input is immutable. Results refer only to this exact module.
inline int xbg_spirv_indirect_instances(const std::vector<uint32_t> &words,std::vector<XbgIndirectInstanceField> &out){
    out.clear();
    xbg_indirect_instances_detail::Context context{words,{},{},{},{}};
    if(!context.parse())return -1;
    for(const auto &in:context.parsed.instructions)if(in.op==59){
        XbgIndirectInstanceField fact{};
        if(context.target(words[in.at+2],fact))out.push_back(fact);
    }
    std::sort(out.begin(),out.end(),[](const XbgIndirectInstanceField &a,const XbgIndirectInstanceField &b){return a.binding<b.binding;});
    return int(out.size());
}
