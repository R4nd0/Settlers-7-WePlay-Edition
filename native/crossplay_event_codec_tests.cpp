// Synthetic ABI and rejection checks; no original game function is called.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_participant.cpp"
#include "crossplay_event_codec.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

extern "C" __declspec(dllexport) uint32_t __cdecl WePlayCodecTablesOracle(uint32_t value) {
    const auto edition=static_cast<Crossplay::Edition>(value);
    return CrossplayNativeCodec::Detail::Tables(CrossplayNativeCodec::Detail::ChatPin(edition),edition)?1u:0u;
}

namespace {
unsigned checks=0;
void Check(bool value,const char* why) {++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
uintptr_t visitor_seen=0,event_seen=0,name_seen=0,ecx_seen=1;
uint32_t flags_seen=1;
__declspec(naked) void Constructor() {
    __asm { mov dword ptr [eax],12345678h }
    __asm { ret }
}
__declspec(naked) void Writer() {
    __asm {
        mov visitor_seen,esi
        mov event_seen,edi
        mov eax,[esp+4]
        mov name_seen,eax
        mov eax,[esp+8]
        mov flags_seen,eax
        ret 8
    }
}
__declspec(naked) void Reader() {
    __asm {
        mov visitor_seen,edi
        mov ecx_seen,ecx
        mov eax,[esp+4]
        mov name_seen,eax
        mov eax,87654321h
        ret 4
    }
}
void GoldAbi(bool reading) {
    auto write=&CrossplayNativeCodec::Detail::GoldWrite;
    auto read=&CrossplayNativeCodec::Detail::GoldRead;
    const uintptr_t entry=reinterpret_cast<uintptr_t>(reading?Reader:Writer);
    uintptr_t before=0,after=0,observed_bx=0,observed_si=0,observed_di=0,result=0;
    if(reading) {
        __asm {
            pushad
            mov ebx,12345678h
            mov esi,34567890h
            mov edi,456789abh
            mov before,esp
            push 789abcdeh
            push 56789abch
            push entry
            call read
            add esp,12
            mov result,eax
            mov after,esp
            mov observed_bx,ebx
            mov observed_si,esi
            mov observed_di,edi
            popad
        }
    } else {
        __asm {
            pushad
            mov ebx,12345678h
            mov esi,34567890h
            mov edi,456789abh
            mov before,esp
            push 789abcdeh
            push 6789abcdh
            push 56789abch
            push entry
            call write
            add esp,16
            mov after,esp
            mov observed_bx,ebx
            mov observed_si,esi
            mov observed_di,edi
            popad
        }
    }
    Check(before==after,"custom Gold codec callee stack cleanup");
    Check(observed_bx==0x12345678 && observed_si==0x34567890 && observed_di==0x456789ab,"codec wrapper retains nonvolatiles");
    Check(visitor_seen==0x56789abc && name_seen==0x789abcde,"native visitor and field-name arguments");
    Check(reading?(result==0x87654321 && !ecx_seen):(event_seen==0x6789abcd && !flags_seen),"native read result/context or write object/flags");
}
}
int main() {
    uint32_t output=0;CrossplayNativeCodec::Detail::GoldCtor(reinterpret_cast<uintptr_t>(Constructor),&output);
    Check(output==0x12345678,"Gold stream constructor uses EAX output");GoldAbi(false);GoldAbi(true);
    using CrossplayNativeCodec::Detail::Readable;
    Check(!Readable(nullptr,1) && !Readable(&output,0) && !Readable(reinterpret_cast<void*>(UINTPTR_MAX-1),4),"native range exclusions");
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Check(pages!=nullptr,"owned guard-page allocation");
    DWORD previous=0;Check(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&previous)!=FALSE,"owned guard-page protection");
    Check(Readable(pages,4096) && !Readable(pages+4094,4) && !Readable(pages+4096,1),"unreadable native spans excluded");
    Check(VirtualFree(pages,0,MEM_RELEASE)!=FALSE,"owned guard-page release");
    CrossplayNative::ParticipantEvent binding;std::vector<unsigned char> wire{1,2,3};
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        Check(!binding.Bind(edition),"test executable cannot acquire native image identity");
        Check(!CrossplayNativeCodec::EncodeChat(edition,binding,0,1,nullptr,wire) && wire.empty(),"unbound encode cannot touch game addresses or retain output");
        Check(!CrossplayNativeCodec::EnqueueChat(edition,binding,0,1,wire),"unbound receive cannot invoke a native factory");
    }
    bool denied=false;std::thread other([&]{denied=!CrossplayNativeCodec::Roster(binding,0,1);});other.join();Check(denied,"foreign-thread binding excluded");
    std::printf("PASS %u native codec ABI/range/ownership rejection checks; no game code executed\n",checks);
}
