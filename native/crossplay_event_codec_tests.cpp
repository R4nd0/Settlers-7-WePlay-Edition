// Synthetic ABI and rejection checks; no original game function is called.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_participant.cpp"
#include "crossplay_event_codec.h"
#include "crossplay_history_role.h"
#include "crossplay_session_config_codec.h"
#include "crossplay_ready_codec.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

extern "C" __declspec(dllexport) uint32_t __cdecl WePlayCodecTablesOracle(uint32_t value) {
    const auto edition=static_cast<Crossplay::Edition>(value);
    return CrossplayNativeCodec::Detail::Tables(CrossplayNativeCodec::Detail::ChatPin(edition),edition)?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayHistoryRoleMethodsOracle(uint32_t value) {
    return CrossplayNative::HistoryRoleDetail::Methods(static_cast<Crossplay::Edition>(value))?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlaySessionConfigMethodsOracle(uint32_t value) {
    return CrossplayNativeCodec::SessionConfigDetail::Methods(static_cast<Crossplay::Edition>(value))?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayReadyMethodsOracle(uint32_t value) {
    return CrossplayNativeCodec::ReadyDetail::Methods(static_cast<Crossplay::Edition>(value))?1u:0u;
}
extern "C" __declspec(dllexport) void* __cdecl WePlayGoldConfigCtorOracle(uintptr_t address,const void* map,void* output,uint32_t source,const void* save,const void* slots) {
    return CrossplayNativeCodec::SessionConfigDetail::GoldConstruct(address,map,output,source,save,slots);
}

namespace {
unsigned checks=0;
void Check(bool value,const char* why) {++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
uintptr_t visitor_seen=0,event_seen=0,name_seen=0,ecx_seen=1;
uint32_t flags_seen=1;
uintptr_t config_args[5]{};
__declspec(naked) void ConfigConstructor() {
    __asm {
        mov config_args,ecx
        mov eax,[esp+4]
        mov config_args+4,eax
        mov edx,[esp+8]
        mov config_args+8,edx
        mov edx,[esp+12]
        mov config_args+12,edx
        mov edx,[esp+16]
        mov config_args+16,edx
        ret 16
    }
}
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
void ReadySourcePolicy(Crossplay::Edition edition) {
    const auto pin=CrossplayNativeCodec::ReadyDetail::Pin(edition);unsigned char object[0x18]{};
    memcpy(object,&pin.event_vt,4);CrossplayChat::Put(object+4,CrossplayReady::Type);
    object[12]=1;CrossplayChat::Put(object+0x10,2);
    auto source=[&]{return CrossplayNativeCodec::ReadyDetail::Source(pin,object,0);};
    Check(source(),"actual zero History CID and native slot 2 allowed for Ready request");
    object[0x14]=1;Check(source(),"actual request header is independent of unused native tail");
    object[0x14]=255;Check(source(),"uninitialized History ReadyState is never read or serialized");object[0x14]=0;
    CrossplayChat::Put(object+0x10,UINT32_MAX);Check(source(),"uninitialized History NetworkSlot is never interpreted as a target");CrossplayChat::Put(object+0x10,2);
    CrossplayChat::Put(object+8,42);Check(!source(),"foreign source CID excluded before serializer");CrossplayChat::Put(object+8,0);
    object[12]=0;Check(!source(),"wrong native queue excluded");object[12]=1;
    object[4]^=1;Check(!source(),"wrong native type excluded");object[4]^=1;
    object[0]^=1;Check(!source(),"foreign event vtable excluded");object[0]^=1;
    Check(!CrossplayNativeCodec::ReadyDetail::Source(pin,nullptr,0),"unreadable native request excluded");
    Check(!CrossplayNativeCodec::ReadyDetail::Source(pin,object,UINT32_MAX),"sentinel guest excluded");
}
void ConfigSourcePolicy(Crossplay::Edition edition) {
    struct NativeString {const char* data;uint32_t capacity,length,extra;};
    struct Slot {uint32_t state;unsigned char ready,pad1[3];uint32_t slot;unsigned char privacy,pad2[3];uint32_t station,extra;};
    static_assert(sizeof(Slot)==24 && sizeof(NativeString)==16,"source policy fixture layout");
    const auto pin=CrossplayNativeCodec::SessionConfigDetail::Pin(edition);
    const CrossplaySessionConfig::Connections ids{42,0};
    unsigned char object[0x40]{};Slot slots[8]{};
    for(unsigned i=0;i<8;++i){slots[i].state=i<2?4u:0u;slots[i].slot=i<2?i+1:UINT32_MAX;}
    slots[0].ready=1;
    const char map[]="Maps/Skirmish/Monhaim";
    NativeString native_map{map,sizeof(map),sizeof(map)-1,0},save{};
    memcpy(object,&pin.event_vt,4);CrossplayChat::Put(object+4,CrossplaySessionConfig::Type);CrossplayChat::Put(object+8,ids.host);object[12]=1;
    memcpy(object+0x10,&native_map,16);memcpy(object+0x20,&save,16);
    uintptr_t vector[]={reinterpret_cast<uintptr_t>(slots),reinterpret_cast<uintptr_t>(slots)+sizeof(slots),reinterpret_cast<uintptr_t>(slots)+sizeof(slots)};
    const size_t offset=pin.gold?0x34:0x30;memcpy(object+offset,vector,12);
    auto source=[&]{return CrossplayNativeCodec::SessionConfigDetail::Source(pin,object,ids);};
    Check(source(),"bounded native source retains actual Ready with empty unused slots");
    slots[0].ready=2;Check(!source(),"nonboolean native Ready rejected before serializer");slots[0].ready=1;
    slots[1].station=1;Check(!source(),"unmapped nonzero Station rejected before serializer");slots[1].station=0;
    slots[2].state=4;Check(!source(),"unsupported third native participant rejected");slots[2].state=0;
    slots[1].slot=1;Check(!source(),"duplicate native network slot rejected");slots[1].slot=2;
    slots[1].state=3;Check(source(),"original intermediate confirmation state retained");slots[1].state=4;
    slots[2].state=2;slots[2].ready=1;slots[2].slot=3;Check(source(),"original AI slot retained");
    slots[2]={};slots[2].slot=UINT32_MAX;slots[3].state=5;Check(source(),"original closed slot retained");slots[3].state=0;
    vector[1]=vector[0]+sizeof(slots)-1;memcpy(object+offset,vector,12);Check(!source(),"partial native slot vector rejected");
    vector[1]=vector[0]+sizeof(slots);vector[2]=vector[0]+3*sizeof(slots);memcpy(object+offset,vector,12);Check(!source(),"unbounded native capacity rejected");
    vector[2]=vector[1];memcpy(object+offset,vector,12);
    native_map.length=UINT32_MAX;memcpy(object+0x10,&native_map,16);Check(!source(),"unbounded source string rejected before native writer");
    native_map.length=sizeof(map)-1;native_map.capacity=1;memcpy(object+0x10,&native_map,16);Check(!source(),"short native string capacity rejected");
    native_map.capacity=sizeof(map);memcpy(object+0x10,&native_map,16);
    save=native_map;memcpy(object+0x20,&save,16);Check(!source(),"resume save path cannot use fresh config route");
    save={};memcpy(object+0x20,&save,16);Check(source(),"source fixture restored without game calls");
}
}
int main() {
    uint32_t output=0;CrossplayNativeCodec::Detail::GoldCtor(reinterpret_cast<uintptr_t>(Constructor),&output);
    Check(output==0x12345678,"Gold stream constructor uses EAX output");GoldAbi(false);GoldAbi(true);
    auto* config_result=CrossplayNativeCodec::SessionConfigDetail::GoldConstruct(reinterpret_cast<uintptr_t>(ConfigConstructor),reinterpret_cast<void*>(11),reinterpret_cast<void*>(22),33,reinterpret_cast<void*>(44),reinterpret_cast<void*>(55));
    Check(config_result==reinterpret_cast<void*>(22) && config_args[0]==11 && config_args[1]==22 && config_args[2]==33
        && config_args[3]==44 && config_args[4]==55,"Gold configuration constructor retains custom ECX/stack ABI and original output");
    using CrossplayNativeCodec::Detail::Readable;
    Check(!Readable(nullptr,1) && !Readable(&output,0) && !Readable(reinterpret_cast<void*>(UINTPTR_MAX-1),4),"native range exclusions");
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Check(pages!=nullptr,"owned guard-page allocation");
    DWORD previous=0;Check(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&previous)!=FALSE,"owned guard-page protection");
    Check(Readable(pages,4096) && !Readable(pages+4094,4) && !Readable(pages+4096,1),"unreadable native spans excluded");
    using CrossplayNative::HistoryRoleDetail::Writable;
    Check(Writable(pages,4) && !Writable(pages+4096,4),"role setter requires writable owned memory");
    Check(VirtualProtect(pages,4096,PAGE_READONLY,&previous)!=FALSE,"owned read-only page protection");
    Check(Readable(pages,4) && !Writable(pages,4),"read-only native fields cannot reach role setter");
    Check(!Writable(nullptr,4) && !Writable(reinterpret_cast<void*>(UINTPTR_MAX-1),4),"role write bounds reject null and wrapping span");
    Check(VirtualFree(pages,0,MEM_RELEASE)!=FALSE,"owned guard-page release");
    CrossplayNative::ParticipantEvent binding;std::vector<unsigned char> wire{1,2,3};
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        ConfigSourcePolicy(edition);
        ReadySourcePolicy(edition);
        Check(!binding.Bind(edition),"test executable cannot acquire native image identity");
        Check(!CrossplayNativeCodec::EncodeChat(edition,binding,0,1,nullptr,wire) && wire.empty(),"unbound encode cannot touch game addresses or retain output");
        Check(!CrossplayNativeCodec::EnqueueChat(edition,binding,0,1,wire),"unbound receive cannot invoke a native factory");
        const CrossplaySessionConfig::Connections ids{1,0};
        Check(!CrossplayNativeCodec::EncodeSessionConfig(edition,binding,ids,nullptr,wire) && wire.empty(),"unbound config cannot serialize a foreign object");
        Check(!CrossplayNativeCodec::EnqueueSessionConfig(edition,binding,ids,wire),"unbound config cannot enter a native queue");
        Check(!CrossplayNativeCodec::CaptureGoldSessionConfig(binding,ids,wire) && wire.empty(),"unbound initial snapshot cannot access or construct native configuration");
        Check(!CrossplayNativeCodec::EncodeReadyRequest(edition,binding,ids,nullptr,wire) && wire.empty(),"unbound Ready source cannot serialize an object");
        CrossplayNativeCodec::PendingReadyToggle pending;
        Check(!CrossplayNativeCodec::EnqueueReadyToggle(edition,binding,ids,wire,pending) && !pending.configuration,"unbound Ready receiver cannot invoke native factory");
        Check(CrossplayNativeCodec::ObserveReadyToggle(binding,pending)==CrossplayNativeCodec::ReadyObservation::Invalid,"empty Ready observation cannot acknowledge a request");
    }
    bool denied=false;std::thread other([&]{denied=!CrossplayNativeCodec::Roster(binding,0,1);});other.join();Check(denied,"foreign-thread binding excluded");
    std::printf("PASS %u native codec ABI/range/ownership rejection checks; no game code executed\n",checks);
}
