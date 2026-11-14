#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_ai_profile_codec.h"
#include <fstream>
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value,const char* reason){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}}
void Guards(Crossplay::Edition edition,const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    Check(static_cast<bool>(file),"mapped fixture opens");const auto size=file.tellg();
    Check(size>0 && size<=0x1400000,"mapped fixture bound");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));file.seekg(0);
    Check(static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()),size)),"mapped fixture reads");
    void* region=VirtualAlloc(reinterpret_cast<void*>(0x400000),bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(region==reinterpret_cast<void*>(0x400000),"separate non-executable original-data fixture");
    memcpy(region,bytes.data(),bytes.size());
    Check(CrossplayAIProfile::Methods(edition),"all original method regions match");
    const auto index=static_cast<unsigned>(edition)-1;
    for(const auto& pin:CrossplayAIProfileGuards::Pins[index]) {
        auto* data=reinterpret_cast<unsigned char*>(pin.address);
        for(unsigned offset:{0u,pin.bytes/2,pin.bytes-1}) {
            data[offset]^=0x80;
            Check(!CrossplayAIProfile::Methods(edition),"method/table byte mutation rejected");
            data[offset]^=0x80;Check(CrossplayAIProfile::Methods(edition),"restored method region matches");
        }
        DWORD old=0,unused=0;
        Check(VirtualProtect(data,pin.bytes,PAGE_NOACCESS,&old)!=FALSE,"fixture made unreadable");
        Check(!CrossplayAIProfile::Methods(edition),"unreadable method/table excluded");
        Check(VirtualProtect(data,pin.bytes,old,&unused)!=FALSE,"fixture protection restored");
    }
    Check(VirtualFree(region,0,MEM_RELEASE)!=FALSE,"only owned data fixture freed");
}
void Locks() {
    CRITICAL_SECTION section;InitializeCriticalSection(&section);
    struct Mutex {uintptr_t vt;CRITICAL_SECTION* value;};Mutex mutex{0,&section};
    alignas(4) unsigned char cache[12]{};const auto ptr=reinterpret_cast<uintptr_t>(&mutex);
    memcpy(cache+8,&ptr,4);
    for(unsigned i=0;i<3;++i) {
        mutex.vt=CrossplayAIProfileGuards::MutexTables[i];
        {CrossplayAIProfile::CacheLock lock(static_cast<Crossplay::Edition>(i+1),cache);Check(lock.acquired,"edition-specific original mutex view");}
        ++mutex.vt;
        {CrossplayAIProfile::CacheLock lock(static_cast<Crossplay::Edition>(i+1),cache);Check(!lock.acquired,"wrong mutex table rejected");}
    }
    {CrossplayAIProfile::CacheLock lock(static_cast<Crossplay::Edition>(99),cache);Check(!lock.acquired,"unknown edition excluded");}
    {CrossplayAIProfile::CacheLock lock(Crossplay::Edition::Gold,nullptr);Check(!lock.acquired,"null cache excluded");}
    mutex.vt=CrossplayAIProfileGuards::MutexTables[0];mutex.value=nullptr;
    {CrossplayAIProfile::CacheLock lock(Crossplay::Edition::Gold,cache);Check(!lock.acquired,"null native mutex section excluded");}
    DeleteCriticalSection(&section);
}
}
int main(int argc,char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--locks-only")) {
        Locks();
        std::printf("PASS %u native mutex ABI checks using owned critical sections; no original image mapped.\n",checks);
        return 0;
    }
    Check(argc==4,"three explicitly supplied original mapped-data fixtures");
    Check(!CrossplayAIProfile::Methods(static_cast<Crossplay::Edition>(99)),"unknown method edition excluded");
    for(unsigned i=0;i<3;++i)Guards(static_cast<Crossplay::Edition>(i+1),argv[i+1]);
    Locks();
    std::printf("PASS %u complete AI method/mutation/unreadability and mutex ABI checks; original fixture memory is non-executable. No game crossplay tested.\n",checks);
}
