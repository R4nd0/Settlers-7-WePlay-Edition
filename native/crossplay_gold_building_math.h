#pragma once
// Opt-in Gold/History geometry candidate; ordinary adapters do not include it.
// The caller must bind the executable and authenticated fresh-world predicate.
// Original CRC/desync checks remain responsible for detecting simulation drift.
#include "crossplay_building_point_math.h"
#include "crossplay_load_progress_codec.h"
#include <cmath>
namespace GoldBuildingMath {
using namespace CrossplayNativeCodec;
inline uintptr_t original=0x65ed40,main_address=0x10f835c;inline DWORD owner=0;inline bool (*ready)()=nullptr;
inline uint32_t corrected=0,excluded=0;inline bool installed=false;
inline constexpr uintptr_t tables[]={15969828,15972972,15974420,15975932,15977260,15983548,15984964};
inline bool Table(uintptr_t p){for(auto value:tables)if(value==p)return true;return false;}
inline bool Writable(void* p,size_t size) {
    MEMORY_BASIC_INFORMATION info{};if(!p || !size || VirtualQuery(p,&info,sizeof(info))!=sizeof(info))return false;
    const uintptr_t at=reinterpret_cast<uintptr_t>(p),base=reinterpret_cast<uintptr_t>(info.BaseAddress);
    return info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
        && at>=base && size<=info.RegionSize && at-base<=info.RegionSize-size;
}
inline bool Finite(BuildingPointMath::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
struct Vector {uintptr_t begin,end,capacity;};
inline bool VectorPair(const Vector& out,const uintptr_t* input,uint32_t& count) {
    if(out.end<out.begin || out.capacity<out.end || (out.end-out.begin)%24 || input[1]<input[0] || (input[1]-input[0])%24)return false;
    count=static_cast<uint32_t>((input[1]-input[0])/24);
    return count<=4096 && out.end-out.begin==count*24 && (!count || (Detail::Readable(reinterpret_cast<void*>(input[0]),count*24) && Writable(reinterpret_cast<void*>(out.begin),count*24)));
}
inline void __stdcall Recompute(void* object) {
    const DWORD error=GetLastError();
    {LoadProgressDetail::FpScope fp;
    if(owner==GetCurrentThreadId() && ready && ready()) {
        using namespace BuildingPointMath;
        auto* bytes=static_cast<unsigned char*>(object);bool valid=Detail::Readable(object,0x18c)&&Writable(object,0x18c)&&Table(*reinterpret_cast<uintptr_t*>(object));
        uintptr_t props=0;Vector output[2]{};uintptr_t input[2][2]{};uint32_t counts[2]{};Point position{};float angle=0;Point singles[4]{};
        if(valid) {
            const auto main=*reinterpret_cast<const uintptr_t*>(main_address);valid=Detail::Readable(reinterpret_cast<void*>(main),0x190);
            if(valid) {
                const auto manager=*reinterpret_cast<const uintptr_t*>(main+0x18c);valid=Detail::Readable(reinterpret_cast<void*>(manager),8);
                if(valid) {
                    const auto table=*reinterpret_cast<const uintptr_t*>(manager+4),index=*reinterpret_cast<const uint32_t*>(bytes+4);
                    valid=index<65536 && Detail::Readable(reinterpret_cast<void*>(table+index*4),4);
                    if(valid){props=*reinterpret_cast<const uintptr_t*>(table+index*4);valid=Detail::Readable(reinterpret_cast<void*>(props),0xf0);}
                }
            }
        }
        if(valid) {
            memcpy(&position,bytes+0x28,8);memcpy(&angle,bytes+0x30,4);valid=Finite(position)&&std::isfinite(angle)&&std::fabs(angle)<=1000000.0f;
            memcpy(&output[0],bytes+0x140,12);memcpy(&output[1],bytes+0x150,12);
            memcpy(input[0],reinterpret_cast<void*>(props+0x60),8);memcpy(input[1],reinterpret_cast<void*>(props+0x70),8);
            valid=valid&&VectorPair(output[0],input[0],counts[0])&&VectorPair(output[1],input[1],counts[1]);
            for(unsigned i=0;i<4;++i){memcpy(&singles[i],reinterpret_cast<void*>(props+0xd0+i*8),8);valid=valid&&Finite(singles[i]);}
            for(unsigned i=0;valid&&i<2;++i)for(uint32_t j=0;valid&&j<counts[i];++j){Point pair[2]{};memcpy(pair,reinterpret_cast<void*>(input[i][0]+j*24),16);valid=Finite(pair[0])&&Finite(pair[1]);}
        }
        if(valid) {
            const auto rotation=RotateDegrees(angle);
            for(unsigned i=0;i<2;++i)for(uint32_t j=0;j<counts[i];++j) {
                Point pair[2]{};memcpy(pair,reinterpret_cast<void*>(input[i][0]+j*24),16);
                pair[0]=Transform(pair[0],position,rotation);pair[1]=Transform(pair[1],position,rotation);
                memcpy(reinterpret_cast<void*>(output[i].begin+j*24),pair,16);
            }
            const auto act=Transform(singles[0],position,rotation),depot=Transform(singles[2],position,rotation);
            memcpy(bytes+0x174,&depot,8);memcpy(bytes+0x17c,&act,8);
            if(!ZeroBits(singles[1])){const auto lounge=Transform(singles[1],position,rotation);memcpy(bytes+0x16c,&lounge,8);}
            const auto constructor=ZeroBits(singles[3])?act:Transform(singles[3],position,rotation);memcpy(bytes+0x184,&constructor,8);++corrected;
        } else ++excluded;
    }}
    SetLastError(error);
}
__declspec(naked) inline void Bridge() {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,4
        mov dword ptr [ebp-4],ecx
        call dword ptr [original]
        pushfd
        pushad
        push dword ptr [ebp-4]
        call Recompute
        popad
        popfd
        mov esp,ebp
        pop ebp
        ret
    }
}
// Full table, method, RTTI and numeric-helper pins are generated from the exact
// analysis image; the enclosing diagnostic manifest retains its executable pin.
inline bool Guard();
inline bool Install(DWORD thread,bool (*predicate)()) {
    if(installed || original!=0x65ed40 || main_address!=0x10f835c || thread!=GetCurrentThreadId() || !predicate || !Guard())return false;
    DWORD protection[7]{};unsigned changed=0;bool restored=true;
    for(unsigned i=0;i<7;++i) {
        auto* slot=reinterpret_cast<uintptr_t*>(tables[i]+59*4);
        if(!VirtualProtect(slot,4,PAGE_READWRITE,&protection[i]))break;
        *slot=reinterpret_cast<uintptr_t>(&Bridge);MemoryBarrier();DWORD ignored=0;
        ++changed;if(!VirtualProtect(slot,4,protection[i],&ignored)){restored=false;break;}
    }
    if(changed!=7 || !restored) {
        for(unsigned i=0;i<changed;++i){auto* slot=reinterpret_cast<uintptr_t*>(tables[i]+59*4);DWORD temporary=0,ignored=0;if(VirtualProtect(slot,4,PAGE_READWRITE,&temporary)){*slot=original;MemoryBarrier();VirtualProtect(slot,4,protection[i],&ignored);}}
        return false;
    }
    owner=thread;ready=predicate;installed=true;return true;
}
}

namespace GoldBuildingMath {
inline bool Guard() {
    uint64_t hash=14695981039346656037ull;
    struct Region {uintptr_t address;size_t length;};
    const Region regions[]={{0x65ed40,1186},{0x5de490,81},{0x4e9910,61},{0x4d4550,100},{0xf3ae20,244},{0x62ab50,8},{0xa302e0,8},{0x62aa00,8},{0x62a9f0,8},{0x65d760,8},{0x65d740,8},{0x62a850,8},{0x62aa50,8},{0x62aa40,8},{0x62a870,8},{0x62a860,8},{0x62a890,8},{0x62a880,8},{0x62a8b0,8},{0x62a8a0,8},{0x62a8c0,8},{0x62a8d0,8},{0x62aa90,8},{0x62aa80,8},{0xa30300,8},{0x62c2b0,8},{0x62a8e0,8},{0x62a960,8},{0x65e7a0,8},{0x65e7c0,8},{0x65e7e0,8},{0x62a970,8},{0x62a980,8},{0x62c2d0,8},{0x62ae20,8},{0x65ea90,8},{0x65d540,8},{0x65d4e0,8},{0x65d5a0,8},{0x65d640,8},{0x65d5e0,8},{0x65d6a0,8},{0x65d6e0,8},{0x65d720,8},{0x65d780,8},{0x65d840,8},{0x65d9e0,8},{0x62c4b0,8},{0x65dcf0,8},{0x65e2d0,8},{0x65ded0,8},{0x65e300,8},{0x65e360,8},{0x62b5f0,8},{0x62b640,8},{0x65f4c0,8},{0x65f510,8},{0x65f2f0,8},{0x65f350,8},{0x62aa30,8},{0x62b6f0,8},{0x62b700,8},{0x62ba50,8},{0x62b680,8},{0x65ed40,8},{0xf78364,20},{0x10b3b68,30},{0xf3ba68,244},{0xbae9fe,8},{0x656f50,8},{0x65c850,8},{0x65db50,8},{0x65e3c0,8},{0x65e410,8},{0xf7973c,20},{0x10b3b90,22},{0xf3c010,244},{0x66b090,8},{0x66d5b0,8},{0x66b390,8},{0x66ebc0,8},{0x66c950,8},{0x66bf00,8},{0x66be50,8},{0x66e2a0,8},{0x66e2c0,8},{0x66e4e0,8},{0x66b0a0,8},{0x66d530,8},{0x66d590,8},{0x66bde0,8},{0xf79f08,20},{0x10b5bc4,18},{0xf3c5f8,244},{0x68d1d0,8},{0x68ebf0,8},{0x68ec10,8},{0x68d380,8},{0x68e260,8},{0x68e200,8},{0x68d8c0,8},{0x68dd40,8},{0x68dda0,8},{0x68d1f0,8},{0x68dfa0,8},{0x68e180,8},{0x68e1b0,8},{0x68e3f0,8},{0x68e310,8},{0xf7a5d8,20},{0x10b647c,22},{0xf3cb28,244},{0x698770,8},{0x698f70,8},{0x698f90,8},{0x6988c0,8},{0x699340,8},{0x698cc0,8},{0x698d30,8},{0x698dd0,8},{0x698e10,8},{0x698e40,8},{0x698f50,8},{0x698c50,8},{0xf7a948,20},{0x10b6aa0,24},{0xf3e3b8,244},{0x6f5db0,8},{0x6f69f0,8},{0x6f6a10,8},{0x6f5e70,8},{0x6f6350,8},{0x6f6530,8},{0x6f65d0,8},{0x6f6320,8},{0x6f6480,8},{0x6f6510,8},{0x6f6160,8},{0xf7c8e8,20},{0x10b9114,34},{0xf3e940,244},{0x704330,8},{0x705890,8},{0x7058b0,8},{0x7044f0,8},{0x7058f0,8},{0x705360,8},{0x705380,8},{0x704360,8},{0x704370,8},{0x7054d0,8},{0x704f20,8},{0xf7cf20,20},{0x10b9b54,26}};
    for(const auto& r:regions){if(!Detail::Readable(reinterpret_cast<void*>(r.address),r.length))return false;for(size_t i=0;i<r.length;++i)hash=(hash^reinterpret_cast<const unsigned char*>(r.address)[i])*1099511628211ull;}
    return hash==0x8bd81403ee4dee4bull;
}
}
