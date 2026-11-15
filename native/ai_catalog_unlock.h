#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

// Show the game's regular Visible AI profiles independently of the retired
// profile-content service. Campaign-only variants and placeholders retain the
// original visibility filter. Does not change saves or account entitlements.
namespace WePlayAICatalog {
struct Profile {uintptr_t entry,site;size_t size;uint64_t hash;unsigned char displacement;};
inline Profile Pin(uint32_t edition) {
    switch(edition) {
    case 1:return {0x5b7990,0x5b7aed,944,0xca8bec4e6235f095ull,0x3b};
    case 2:return {0x54c800,0x54c922,900,0xcb5d320dc780d03full,0x58};
    case 3:return {0x54be50,0x54bf72,900,0xade532f7a02387adull,0x58};
    default:return {};
    }
}
inline bool Readable(uintptr_t at,size_t size) {
    MEMORY_BASIC_INFORMATION m{};
    return at && size && at+size>at && VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m))==sizeof(m)
        && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        && reinterpret_cast<uintptr_t>(m.BaseAddress)<=at
        && reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize>=at+size;
}
inline bool Methods(uint32_t edition) {
    const auto p=Pin(edition);
    if(!Readable(p.entry,p.size) || p.site<p.entry || p.site+2>p.entry+p.size)return false;
    const auto* code=reinterpret_cast<const unsigned char*>(p.entry);uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<p.size;++i)hash=(hash^code[i])*1099511628211ull;
    const auto* site=reinterpret_cast<const unsigned char*>(p.site);
    return hash==p.hash && site[0]==0x75 && site[1]==p.displacement;
}
struct State {uint32_t edition=0;DWORD owner=0;bool installed=false,failed=false;};
inline State& Current(){static State s;return s;}
inline bool Install(uint32_t edition) {
    const DWORD error=GetLastError();auto& state=Current();const auto p=Pin(edition);
    if(state.installed) {
        const bool okay=!state.failed && state.edition==edition && state.owner==GetCurrentThreadId()
            && Readable(p.site,2) && !memcmp(reinterpret_cast<void*>(p.site),"\x90\x90",2);
        SetLastError(error);return okay;
    }
    if(state.failed || !Methods(edition)){SetLastError(error);return false;}
    state.edition=edition;state.owner=GetCurrentThreadId();
    auto* site=reinterpret_cast<unsigned char*>(p.site);const unsigned char old[2]={0x75,p.displacement};
    DWORD protection=0;
    if(!VirtualProtect(site,2,PAGE_EXECUTE_READWRITE,&protection)){SetLastError(error);return false;}
    site[0]=site[1]=0x90;
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,2)!=FALSE;
    DWORD ignored=0;const bool restored=VirtualProtect(site,2,protection,&ignored)!=FALSE;
    if(!flushed || !restored) {
        DWORD temporary=0;
        if(VirtualProtect(site,2,PAGE_EXECUTE_READWRITE,&temporary)) {
            memcpy(site,old,2);FlushInstructionCache(GetCurrentProcess(),site,2);
            VirtualProtect(site,2,protection,&ignored);
        }
        state.failed=true;SetLastError(error);return false;
    }
    state.installed=true;SetLastError(error);return true;
}
}
