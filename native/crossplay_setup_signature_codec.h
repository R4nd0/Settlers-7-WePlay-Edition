#pragma once
#include "crossplay_ready_state_codec.h"
#include "crossplay_setup_signature.h"
#include <bcrypt.h>

// Opt-in native diagnostic reader. Ordinary game adapters exclude this file.
// Use only with the executable-bound participant on its owner thread. Original
// writers run against owned preparation data; no event, Ready or world result
// is created. Template players are NOT the active multiplayer roster.
namespace CrossplaySetupSignatureNative {
struct Pin {uintptr_t manager,type,rules,players;size_t stride,root_size;uint64_t rules_hash,players_hash;};
inline bool Layout(Crossplay::Edition edition,Pin& p) {
    if(edition==Crossplay::Edition::Gold) p={0x10f834c,0xf51af0,0x9cf3f0,0x9ce840,0xd4,0xa0,0x74f1baac722b85f4ull,0x07d26b5801026037ull};
    else if(edition==Crossplay::Edition::UbisoftHistory) p={0x13a5218,0x113ae54,0x5f6490,0x5f5820,0xc4,0x90,0x53a7a72aed77e6d4ull,0xdb8777cb886708baull};
    else if(edition==Crossplay::Edition::SteamHistory) p={0x13a2300,0x113939c,0x5f5ff0,0x5f5380,0xc4,0x90,0xd883f405ebac542full,0xd3cd6d2c9c1486beull};
    else return false;
    return true;
}
inline void Rules(Crossplay::Edition edition,const Pin& p,void* root,void* visitor) {
    if(edition==Crossplay::Edition::Gold)reinterpret_cast<void (__stdcall*)(void*,void*)>(p.rules)(root,visitor);
    else reinterpret_cast<void (__thiscall*)(void*,void*,uint32_t)>(p.rules)(root,visitor,0);
}
inline void Players(Crossplay::Edition edition,const Pin& p,void* root,void* visitor) {
    if(edition==Crossplay::Edition::Gold)reinterpret_cast<void (__stdcall*)(void*,void*,uint32_t)>(p.players)(root,visitor,0);
    else reinterpret_cast<void (__thiscall*)(void*,void*,uint32_t)>(p.players)(root,visitor,0);
}
inline bool Hash(const std::vector<unsigned char>& bytes,Crossplay::Digest& result) {
    result={};if(bytes.empty() || bytes.size()>2*65536+2048)return false;BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD length=0,read=0;
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0
        && BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&read,0)>=0
        && read==sizeof(length) && length && length<=65536;
    std::vector<unsigned char> object(okay?length:0);
    if(okay)okay=BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)>=0
        && BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0
        && BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!okay)result={};return okay;
}
inline bool Methods(const Pin& p) {
    using CrossplayNativeCodec::Detail::Readable;
    for(const auto& method:{std::pair<uintptr_t,uint64_t>{p.rules,p.rules_hash},{p.players,p.players_hash}}) {
        const auto* code=reinterpret_cast<const unsigned char*>(method.first);
        if(!Readable(code,4096))return false;uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<4096;++i)hash=(hash^code[i])*1099511628211ull;
        if(hash!=method.second)return false;
    }
    return true;
}
using CrossplaySetupSignature::Signature;
inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,bool host,Signature& result,void (*trace)(const char*)) {
    using namespace CrossplayNativeCodec;result={};Signature captured{};Pin pin{};
    if(!Layout(edition,pin) || !Methods(pin) || !SessionConfigDetail::Roster(binding,ids,host))return false;
    const auto p=Detail::Pin(edition);
    if(!Detail::Methods(p) || !Detail::Tables(p,edition) || !Detail::Readable(reinterpret_cast<void*>(pin.manager),4))return false;
    const auto* manager=*reinterpret_cast<unsigned char* const*>(pin.manager);
    if(!Detail::Readable(manager,8+2*pin.stride) || *reinterpret_cast<const uintptr_t*>(manager)!=pin.type || manager[4]>1)return false;
    const unsigned mode=manager[4];auto* frame=const_cast<unsigned char*>(manager+((mode-1)&1)*pin.stride);
    auto* root=frame+0x34;std::string map,name,cached;
    auto* cache=edition==Crossplay::Edition::Gold?SessionConfigDetail::GoldConfiguration():ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache || !SessionConfigDetail::NativeString(frame+8,map) || !CrossplaySessionConfig::RelativeMap(map)
        || !SessionConfigDetail::NativeString(frame+0x18,name) || !SessionConfigDetail::NativeString(cache+0x24,cached) || cached!=map
        || !SessionConfigDetail::Fields(p,cache+0x24,cache+0x34,cache+0x44,ids))return false;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+(p.gold?0x48:0x44));
    if(slots[4]!=1 || slots[0x1c]!=1)return false;
    CrossplayNative::LobbySnapshot before;
    if(!binding.CaptureLobby(before) || !before.instance || !Detail::Readable(root,pin.root_size))return false;
    const std::vector<unsigned char> original(root,root+pin.root_size);
    std::vector<unsigned char> signature;
    signature.insert(signature.end(),map.begin(),map.end());signature.push_back(0);
    signature.insert(signature.end(),name.begin(),name.end());signature.push_back(0);
    for(bool players:{false,true}) {
        Detail::OwnedStream stream(p);if(!stream.initialized)return false;
        Detail::Visitor visitor{p.writer_vt,0,&stream.value,0};
        if(trace)trace(players?"native-setup-signature-players-begin":"native-setup-signature-rules-begin");
        if(players)Players(edition,pin,root,&visitor);else Rules(edition,pin,root,&visitor);
        if(trace)trace(players?"native-setup-signature-players-returned":"native-setup-signature-rules-returned");
        const auto& value=stream.value;
        if(value.vt!=p.stream_vt || value.external || !value.length || value.length>65536 || value.position!=value.length
            || value.capacity<value.length || !Detail::Readable(value.bytes,value.length) || visitor.flags
            || memcmp(root,original.data(),original.size()))return false;
        if(players)captured.players=value.length;else captured.rules=value.length;
        const size_t at=signature.size();signature.resize(at+4);CrossplayChat::Put(signature.data()+at,value.length);
        const auto* data=static_cast<const unsigned char*>(value.bytes);signature.insert(signature.end(),data,data+value.length);
    }
    CrossplayNative::LobbySnapshot after;
    if(*reinterpret_cast<unsigned char* const*>(pin.manager)!=manager || manager[4]!=mode
        || !binding.CaptureLobby(after) || after.instance!=before.instance || !SessionConfigDetail::Roster(binding,ids,host))return false;
    if(!Hash(signature,captured.digest) || !CrossplaySetupSignature::Valid(captured))return false;
    result=captured;return true;
}
}
