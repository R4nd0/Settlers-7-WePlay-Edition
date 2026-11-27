// Bounded, bit-exact constant tracking. Shadow users must intercept every
// mutation and invalidate on StateBlock::Apply, Reset and VP-mode changes.
// Forward every setter while recording a state block or outside its owner.
#pragma once
#include <windows.h>
#include <array>
#include <cstring>

namespace WaterOn12Constants {
inline bool Valid(UINT start,const float* data,UINT count){return data&&count&&count<=16&&start<256&&count<=256-start;}
inline bool Equal(const void* a,const void* b,size_t bytes) {
    __try{return std::memcmp(a,b,bytes)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool Copy(void* destination,const void* source,size_t bytes) {
    __try{std::memcpy(destination,source,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct Hint {
    std::array<float,1024> values{};
    std::array<unsigned char,256> known{};
    bool Candidate(UINT start,const float* data,UINT count)const {
        if(!Valid(start,data,count))return false;
        for(UINT i=0;i<count;++i)if(!known[start+i])return false;
        return Equal(values.data()+start*4,data,static_cast<size_t>(count)*16);
    }
    void Remember(UINT start,const float* data,UINT count) {
        // A forwarded large upload may overlap previously remembered registers.
        if(!Valid(start,data,count)){Clear();return;}
        for(UINT i=0;i<count;++i)known[start+i]=0;
        if(Copy(values.data()+start*4,data,static_cast<size_t>(count)*16))
            for(UINT i=0;i<count;++i)known[start+i]=1;
    }
    void Clear(){known.fill(0);}
};
// Full VS palette: large uploads preserve knowledge of disjoint registers.
// Eight integer masks avoid scanning one validity byte per register.
struct Palette {
    std::array<float,1024> values{};
    std::array<DWORD,8> known{};
    static bool ValidRange(UINT start,const float* data,UINT count){return data&&count&&start<256&&count<=256-start;}
    static DWORD Mask(UINT first,UINT count){return (0xffffffffu>>(32-count))<<first;}
    bool Candidate(UINT start,const float* data,UINT count)const {
        if(!ValidRange(start,data,count))return false;
        for(UINT at=start,left=count;left;){const UINT n=(left<32-at%32)?left:32-at%32;
            const DWORD mask=Mask(at%32,n);if((known[at/32]&mask)!=mask)return false;at+=n;left-=n;}
        return Equal(values.data()+start*4,data,static_cast<size_t>(count)*16);
    }
    void Mark(UINT start,UINT count,bool value){
        for(UINT at=start,left=count;left;){const UINT n=(left<32-at%32)?left:32-at%32;
            const DWORD mask=Mask(at%32,n);if(value)known[at/32]|=mask;else known[at/32]&=~mask;at+=n;left-=n;}
    }
    void Remember(UINT start,const float* data,UINT count){
        if(!ValidRange(start,data,count)){Clear();return;}
        Mark(start,count,false);
        if(Copy(values.data()+start*4,data,static_cast<size_t>(count)*16))Mark(start,count,true);
    }
    void Clear(){known.fill(0);}
};
template<class Device> bool Same(Device* device,UINT start,const float* data,UINT count) {
    if(!Valid(start,data,count))return false;
    std::array<float,64> current; // Get fills exactly the compared bytes.
    if(FAILED(device->GetVertexShaderConstantF(start,current.data(),count)))return false;
    return Equal(current.data(),data,static_cast<size_t>(count)*16);
}
}
