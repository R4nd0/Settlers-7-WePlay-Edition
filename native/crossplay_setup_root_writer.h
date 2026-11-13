#pragma once
#include "crossplay_preparation_writer.h"
#include "crossplay_setup_signature_codec.h"
#include "crossplay_setup_rule_methods.h"

// Original enclosing rules + active-player writer. Diagnostic ABI only. The
// caller still needs live ownership/dependency/content validation. This does
// not read a remote setup, dispatch a start or permit synchronized gameplay.
namespace CrossplaySetupRootWriter {
struct Pin {uintptr_t entry=0;size_t size=0;uint64_t hash=0;};
inline bool Layout(Crossplay::Edition edition,Pin& p) {
    p={};
    if(edition==Crossplay::Edition::Gold)p={0x9ce7b0,0x90,0xfa8e0289da0b388cull};
    else if(edition==Crossplay::Edition::UbisoftHistory)p={0x5f5790,0x90,0xb445a87502ad2c62ull};
    else if(edition==Crossplay::Edition::SteamHistory)p={0x5f52f0,0x90,0x5593703056e73b44ull};
    else return false;
    return true;
}
inline bool Methods(Crossplay::Edition edition,const Pin& p) {
    Pin expected{};CrossplayPreparationWriter::Pin players{};CrossplaySetupSignatureNative::Pin rules{};
    if(!Layout(edition,expected) || p.entry!=expected.entry || p.size!=expected.size || p.hash!=expected.hash
        || !RuleMethods(edition)
        || !CrossplayPreparationWriter::Layout(edition,players) || !CrossplayPreparationWriter::Methods(edition,players)
        || !CrossplaySetupSignatureNative::Layout(edition,rules))return false;
    for(const auto& region:{std::pair<uintptr_t,std::pair<size_t,uint64_t>>{p.entry,{p.size,p.hash}},
                           {rules.rules,{4096,rules.rules_hash}}}) {
        const auto* code=reinterpret_cast<const unsigned char*>(region.first);
        if(!CrossplayNativeCodec::Detail::Readable(code,region.second.first))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.second.first;++i)hash=(hash^code[i])*1099511628211ull;
        if(hash!=region.second.second)return false;
    }
    return true;
}
namespace Detail {
// Gold: EAX=root, ESI=visitor, stack=version, ret4. History: thiscall
// (root, visitor, version), ret8. Original nested player nodes choose version 1.
inline void Write(Crossplay::Edition edition,const Pin& p,void* root,void* visitor,uint32_t version) {
    if(edition==Crossplay::Edition::Gold) {
        const uintptr_t address=p.entry;
        __asm {
            mov eax,root
            mov esi,visitor
            push version
            call address
        }
    } else reinterpret_cast<void (__thiscall*)(void*,void*,uint32_t)>(p.entry)(root,visitor,version);
}
}
}
