#pragma once
// Opt-in fresh cross-edition world startup only. The caller must first verify
// the executable, owner thread and original ready two-human session, and must
// revalidate that session before starting. Native CRC computation is unchanged.
// Opt-in Gold/History fresh-world initialization. The caller must first verify
// the executable, owner and original two-human ready session/setup, before start.
// No CRC input, accumulator, event scalar or calculation is replaced.
#include "crossplay_load_progress_codec.h"
namespace SharedEngineMode {
using namespace CrossplayNativeCodec;
struct Profile {uintptr_t flag=0,getter=0,setter=0;unsigned getter_size=0,setter_size=0;uint64_t getter_hash=0,setter_hash=0;bool gold=false;};
inline Profile Pin(Crossplay::Edition edition);
struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}};
inline uint64_t Hash(uintptr_t p,unsigned n){if(!Detail::Readable(reinterpret_cast<void*>(p),n))return 0;uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<n;++i)hash=(hash^reinterpret_cast<const unsigned char*>(p)[i])*1099511628211ull;return hash;}
inline bool Guard(const Profile& p){return p.flag && p.getter && p.setter && p.getter_size && p.setter_size && Hash(p.getter,p.getter_size)==p.getter_hash && Hash(p.setter,p.setter_size)==p.setter_hash;}
inline bool FlagWritable(uintptr_t p) {
    MEMORY_BASIC_INFORMATION info{};return VirtualQuery(reinterpret_cast<void*>(p),&info,sizeof(info))==sizeof(info)
        && info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
// Internal profile overload permits modeled native accessors in ABI fixtures.
// Runtime callers use the exact Pin and independently verify the fresh session.
inline bool Apply(const Profile& p,DWORD owner,bool& changed) {
    ErrorScope error;LoadProgressDetail::FpScope fp;changed=false;
    if(!owner || owner!=GetCurrentThreadId() || !Guard(p) || !Detail::Readable(reinterpret_cast<void*>(p.flag),1) || !FlagWritable(p.flag))return false;
    const auto before=*reinterpret_cast<const unsigned char*>(p.flag);
    using Getter=unsigned char(__cdecl*)();const auto get=reinterpret_cast<Getter>(p.getter);
    if(before>1 || get()!=before)return false;
    if(p.gold && before) {
        reinterpret_cast<void(__cdecl*)()>(p.setter)();changed=true;
    }
    return get()==0 && *reinterpret_cast<const unsigned char*>(p.flag)==0;
}
inline bool Prepare(Crossplay::Edition edition,DWORD owner,bool& changed){return Apply(Pin(edition),owner,changed);}
inline bool Verify(Crossplay::Edition edition,DWORD owner) {
    ErrorScope error;LoadProgressDetail::FpScope fp;const auto p=Pin(edition);
    return owner && owner==GetCurrentThreadId() && Guard(p) && Detail::Readable(reinterpret_cast<void*>(p.flag),1)
        && *reinterpret_cast<const unsigned char*>(p.flag)==0 && reinterpret_cast<unsigned char(__cdecl*)()>(p.getter)()==0;
}
}

namespace SharedEngineMode {
inline Profile Pin(Crossplay::Edition edition) {
    if(edition==Crossplay::Edition::Gold)return {0x10e5b79,0x41c2e0,0x41c2d0,6,8,0xbc7f1dc891d5dddbull,0x9f1c1b077a84a79cull,true};
    if(edition==Crossplay::Edition::SteamHistory)return {0x1375f29,0x4e7820,0x4e7810,6,7,0x9f2d6b3b86d60740ull,0x868478136e728bd7ull,false};
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x1378e48,0x4e7a80,0x4e7a70,6,7,0x8cbece28146108aeull,0xe9f08a5d0d5ee171ull,false};
    return {};
}
}
