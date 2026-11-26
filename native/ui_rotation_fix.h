#pragma once
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <string>
#include <intrin.h>

namespace UiRotationFix {
struct Config {DWORD size=12,version=3,enabled=0;};
static_assert(sizeof(Config)==12,"UI diagnostic configuration ABI");
struct Stats {
    DWORD requested=0,enabled=0,unsafe=0,pending_refs=0,retained_programs=0;
    std::uint64_t draws=0,matched=0,applications=0,restorations=0,failed_draws=0;
    std::uint64_t context_rejections=0,query_failures=0,shader_failures=0,set_failures=0,restore_failures=0,exceptions=0,creations=0;
    std::uint64_t frame=0,frame_applications=0,frame_restorations=0,frame_failed_draws=0;
};
struct Transaction {IDirect3DVertexShader9* original=nullptr;bool applied=false;};
void Initialize(DWORD width,DWORD height) noexcept;
bool Configure(const Config& config) noexcept;
bool Begin(IDirect3DDevice9* device,Transaction& transaction) noexcept;
bool End(IDirect3DDevice9* device,Transaction& transaction,HRESULT result) noexcept;
bool Reset(IDirect3DDevice9* device) noexcept;
bool Safe() noexcept;
bool Enabled() noexcept;
void EndFrame(std::uint64_t frame) noexcept;
Stats Read() noexcept;
std::string Json();
struct CallState {
    alignas(16) std::array<unsigned char,512> floating{};DWORD error=0;
    void save(){error=GetLastError();_fxsave(floating.data());}
    void restore(){_fxrstor(floating.data());SetLastError(error);}
};
template<class Draw> HRESULT Execute(IDirect3DDevice9* device,Draw&& draw) {
    CallState input;input.save();Transaction transaction{};
    if(!Begin(device,transaction)){input.restore();return D3DERR_DEVICELOST;}
    input.restore();HRESULT result=E_FAIL;
    try {result=draw();}
    catch(...){CallState original;original.save();End(device,transaction,E_FAIL);original.restore();throw;}
    CallState original;original.save();const bool safe=End(device,transaction,result);original.restore();
    return safe?result:D3DERR_DEVICELOST;
}
}
