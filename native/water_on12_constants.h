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
template<class Device> bool Same(Device* device,UINT start,const float* data,UINT count) {
    if(!Valid(start,data,count))return false;
    std::array<float,64> current; // Get fills exactly the compared bytes.
    if(FAILED(device->GetVertexShaderConstantF(start,current.data(),count)))return false;
    return Equal(current.data(),data,static_cast<size_t>(count)*16);
}
}
