#pragma once

#include "xbg_spirv_typed_storage.h"
#include <string>
#include <utility>

// Canonical identities retain the integer recipe before the typed recipe.
// A rejected identity leaves every output unchanged.
inline bool xbg_shader_identity(
    const std::string &identity,
    std::string &base,
    std::vector<std::pair<uint32_t,uint32_t>> &integer,
    std::vector<std::pair<uint32_t,uint32_t>> *typed=nullptr){
    if(identity.size()<16 || identity.size()>256 || typed==&integer)return false;
    std::string parsed_base=identity.substr(0,16);
    if(parsed_base.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;

    std::vector<std::pair<uint32_t,uint32_t>> parsed_integer,parsed_typed;
    size_t at=16;
    auto number=[&](uint32_t &value){
        const size_t begin=at;
        value=0;
        while(at<identity.size() && identity[at]>='0' && identity[at]<='9'){
            const uint32_t digit=uint32_t(identity[at]-'0');
            if(value>(UINT32_MAX-digit)/10)return false;
            value=value*10+digit;
            ++at;
        }
        return at>begin && (at==begin+1 || identity[begin]!='0');
    };
    auto list=[&](std::vector<std::pair<uint32_t,uint32_t>> &formats,bool integer_family){
        while(true){
            uint32_t binding,format;
            if(!number(binding) || at==identity.size() || identity[at++]!='_' || !number(format))return false;
            if(integer_family){
                if(format!=XBG_VK_FORMAT_R16_UINT && format!=XBG_VK_FORMAT_R32_UINT)return false;
            }else if(!xbg_spirv_image_format(format))return false;
            if(!formats.empty() && binding<=formats.back().first)return false;
            formats.push_back({binding,format});
            if(at==identity.size() || identity[at]==':')return true;
            if(identity[at++]!=',')return false;
        }
    };

    if(identity.compare(at,5,":ib1:")==0){
        at+=5;
        if(!list(parsed_integer,true))return false;
    }
    if(identity.compare(at,6,":stb1:")==0){
        at+=6;
        if(!list(parsed_typed,false))return false;
    }
    if(at!=identity.size())return false;

    base.swap(parsed_base);
    integer.swap(parsed_integer);
    if(typed)typed->swap(parsed_typed);
    return true;
}
