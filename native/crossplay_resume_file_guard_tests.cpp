#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_resume_file_codec.h"
#include <cstdio>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayResumeOwnershipOracle(uint32_t edition){
    const uintptr_t send=edition==2?0xedb300:edition==3?0xeda9f0:0xa4e430;
    return CrossplayNativeCodec::ResumeFileDetail::OwnershipMethods(static_cast<Crossplay::Edition>(edition),send)?1u:0u;
}
int main(){std::puts("Offline original-image file-ownership guard fixture only.");return 2;}
