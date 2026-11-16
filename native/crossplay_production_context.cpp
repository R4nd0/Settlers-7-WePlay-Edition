#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "crossplay_production_context.h"
namespace CrossplayProduction {
namespace {
volatile LONG entered=0;
DWORD owner=0;
Launch launch;
}
bool PrepareLaunch(Crossplay::Edition edition,const Crossplay::Digest& verified_executable,
                   const std::wstring& actual_profile){
    if(InterlockedCompareExchange(&entered,1,0))return false;
    Launch next;
    if(!ReadLaunch(edition,verified_executable,actual_profile,next))return false;
    launch=std::move(next);owner=GetCurrentThreadId();return true;
}
const Launch* CurrentLaunch(){return owner && owner==GetCurrentThreadId()?&launch:nullptr;}
}
