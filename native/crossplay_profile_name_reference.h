#pragma once
#include "crossplay_session_config_codec.h"

// ProfileName is an original catalogue reference, not a network principal.
// Mirror the original serializer's existing-ID predicate without changing the
// registry or manufacturing an identifier. Reverse-role profiles only.
namespace CrossplayProfileNameReference {
inline bool InTable(const unsigned char* table,uint32_t id,uintptr_t& registry,uintptr_t& entry){
    using CrossplayNativeCodec::Detail::Readable;registry=entry=0;
    if(!id || id==UINT32_MAX || !Readable(table,0x1c))return false;
    const auto* index=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(table+0x18));
    if(!Readable(index,16))return false;
    const uint32_t count=CrossplayChat::Word(index+8),capacity=CrossplayChat::Word(index+12);
    const auto* entries=reinterpret_cast<const uintptr_t*>(CrossplayChat::Word(index));
    if(!count || count>4096 || capacity<count || capacity>4096 || id>=count || !Readable(entries,count*4))return false;
    const auto symbol=entries[id];
    if(!Readable(reinterpret_cast<const void*>(symbol),12) || !CrossplayChat::Word(reinterpret_cast<const unsigned char*>(symbol)+8))return false;
    registry=reinterpret_cast<uintptr_t>(index);entry=symbol;return true;
}
inline const unsigned char* Table(Crossplay::Edition edition){
    using CrossplayNativeCodec::Detail::Readable;
    const uintptr_t global=edition==Crossplay::Edition::Gold?0x10f835c:edition==Crossplay::Edition::SteamHistory?0x13a2304:0;
    const size_t profile_offset=edition==Crossplay::Edition::Gold?0x188:0x148;
    const size_t table_offset=edition==Crossplay::Edition::Gold?0xa6c:0x9e8;
    if(!global || !Readable(reinterpret_cast<const void*>(global),4))return nullptr;
    const auto* profile=*reinterpret_cast<const unsigned char* const*>(global);
    if(!Readable(profile,profile_offset+4))return nullptr;
    const auto* catalogue=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(profile+profile_offset));
    if(!Readable(catalogue,table_offset+0x1c)
        || *reinterpret_cast<const unsigned char* const*>(global)!=profile
        || CrossplayChat::Word(profile+profile_offset)!=reinterpret_cast<uintptr_t>(catalogue))return nullptr;
    return catalogue+table_offset;
}
inline bool Valid(Crossplay::Edition edition,uint32_t id){
    if(!id || id==UINT32_MAX)return false;
    const auto* table=Table(edition);if(!table)return false;
    uintptr_t index=0,entry=0,after_index=0,after_entry=0;
    return InTable(table,id,index,entry) && Table(edition)==table
        && InTable(table,id,after_index,after_entry) && index==after_index && entry==after_entry;
}
// Zero may remain unresolved during name/lobby preparation. It is never a
// valid serializable selection or permission to start a world.
inline bool KnownOrUnassigned(Crossplay::Edition edition,uint32_t id){
    return (edition==Crossplay::Edition::Gold || edition==Crossplay::Edition::SteamHistory) && (!id || Valid(edition,id));
}
inline bool Key(const std::string& key){
    if(key.empty() || key.size()>128)return false;
    for(unsigned char c:key)if(c<32 || c>126)return false;return true;
}
inline bool NameInTable(const unsigned char* table,uint32_t id,std::string& key){
    key.clear();uintptr_t index=0,entry=0,after_index=0,after_entry=0;std::string value,after;
    if(!InTable(table,id,index,entry)
        || !CrossplayNativeCodec::SessionConfigDetail::NativeString(reinterpret_cast<const unsigned char*>(entry),value) || !Key(value)
        || !InTable(table,id,after_index,after_entry) || index!=after_index || entry!=after_entry
        || !CrossplayNativeCodec::SessionConfigDetail::NativeString(reinterpret_cast<const unsigned char*>(entry),after) || value!=after)return false;
    key=std::move(value);return true;
}
inline bool Name(Crossplay::Edition edition,uint32_t id,std::string& key){
    key.clear();const auto* table=Table(edition);std::string value;
    if(!table || !NameInTable(table,id,value) || Table(edition)!=table)return false;
    key=std::move(value);return true;
}
inline bool ResolveInTable(const unsigned char* table,const std::string& key,uint32_t& id){
    using CrossplayNativeCodec::Detail::Readable;id=0;
    if(!Key(key) || !Readable(table,0x1c))return false;
    const auto* index=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(table+0x18));
    if(!Readable(index,16))return false;
    const auto count=CrossplayChat::Word(index+8),capacity=CrossplayChat::Word(index+12);
    const auto* entries=reinterpret_cast<const uintptr_t*>(CrossplayChat::Word(index));
    if(!count || count>4096 || capacity<count || capacity>4096 || !Readable(entries,count*4))return false;
    uint32_t found=0;
    for(uint32_t i=1;i<count;++i){
        if(!entries[i])continue;std::string candidate;
        if(!NameInTable(table,i,candidate))continue;
        if(candidate==key){if(found)return false;found=i;}
    }
    std::string after;
    if(!found || !NameInTable(table,found,after) || after!=key)return false;
    id=found;return true;
}
inline bool Resolve(Crossplay::Edition edition,const std::string& key,uint32_t& id){
    id=0;const auto* table=Table(edition);uint32_t found=0;
    if(!table || !ResolveInTable(table,key,found) || Table(edition)!=table)return false;
    id=found;return true;
}
}
