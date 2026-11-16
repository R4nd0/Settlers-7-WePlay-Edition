#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
// Production adapters require the existing suspended-entry bootstrap. A plain
// host must not load either DLL and reach game calls or mixed-mode fallback.
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    for(int i=1;i<argc;++i){
        SetLastError(ERROR_SUCCESS);const auto module=LoadLibraryW(argv[i]);const auto error=GetLastError();
        if(module || error!=ERROR_DLL_INIT_FAILED){std::printf("FAIL production direct-load rejection %d error %lu\n",i,error);return 1;}
    }
    std::puts("PASS both normal production adapters reject direct loading before game/session initialization.");
    return 0;
}
