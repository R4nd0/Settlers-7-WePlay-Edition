#pragma once
#include "castle_wire.h"
#include <cstring>

// Mixed-edition admission preserves both original CastleForgeData strings.
// The legacy cosmetic codec's Design::type is the serializer's first string
// (Name). Its format stays unchanged; serialized_type is the second (Type).
namespace CrossplayCastleSetup {
constexpr size_t MaximumType=128,Limit=8+4+castle_wire::Limit+4+MaximumType;
inline constexpr unsigned char Magic[8]={'S','7','C','A','S','T',0,1};
struct Value {castle_wire::Design design;std::string serialized_type;};
inline bool Type(const std::string& text) {
    return text.size()<=MaximumType && std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32 && c<=126;});
}
inline bool Encode(const Value& value,std::vector<unsigned char>& result) {
    result.clear();std::vector<unsigned char> legacy;
    if(!Type(value.serialized_type) || !castle_wire::Encode(value.design,legacy))return false;
    result.assign(Magic,Magic+8);castle_wire::Put(result,static_cast<uint32_t>(legacy.size()));
    result.insert(result.end(),legacy.begin(),legacy.end());
    castle_wire::Put(result,static_cast<uint32_t>(value.serialized_type.size()));
    result.insert(result.end(),value.serialized_type.begin(),value.serialized_type.end());return true;
}
inline bool Decode(const std::vector<unsigned char>& bytes,Value& result) {
    result={};
    if(bytes.size()<16 || bytes.size()>Limit || memcmp(bytes.data(),Magic,8))return false;
    size_t at=8;
    const auto word=[&bytes,&at](uint32_t& n) {
        if(at>bytes.size() || bytes.size()-at<4)return false;
        n=0;for(unsigned i=0;i<4;++i)n|=static_cast<uint32_t>(bytes[at++])<<(8*i);return true;
    };
    uint32_t length=0;
    if(!word(length) || length>castle_wire::Limit || length>bytes.size()-at)return false;
    Value value;const std::vector<unsigned char> legacy(bytes.begin()+at,bytes.begin()+at+length);at+=length;
    if(!castle_wire::Decode(legacy,value.design) || !word(length) || length>MaximumType || length!=bytes.size()-at)return false;
    value.serialized_type.assign(reinterpret_cast<const char*>(bytes.data()+at),length);
    if(!Type(value.serialized_type))return false;
    result=std::move(value);return true;
}
}
