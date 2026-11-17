#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_host_choices.h"
#include <xmmintrin.h>
#include <float.h>
#include <cstdio>
#include <cstdlib>
namespace {
using namespace CrossplayGoldChoices;
unsigned checks=0,gets=0,destroys=0;NativeString requested{};
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"Host choices failed at check %u\n",checks);std::exit(1);}}
alignas(16) char storage[65536]{};
bool wrong_return=false;
void* __stdcall Get(NativeString* output){
    ++gets;*output=requested;SetLastError(0x12345678);_mm_setcsr(0x3f80);
    __asm fld1
    return wrong_return?nullptr:output;
}
void __fastcall DestroyText(NativeString* text,void*){
    ++destroys;Check(text->data==requested.data);
    *text={};SetLastError(0x87654321);_mm_setcsr(0x5f80);
}
bool Read(std::string& result){return ReadTemplateWith(&Get,reinterpret_cast<Destroy>(&DestroyText),result);}
void Set(const std::string& value){
    Check(value.size()<sizeof(storage));memset(storage,0,sizeof(storage));memcpy(storage,value.data(),value.size());
    requested={storage,static_cast<uint32_t>(value.size()+1),static_cast<uint32_t>(value.size()),0};gets=destroys=0;wrong_return=false;
}
unsigned short Status(){
    unsigned short value=0;
    __asm {
        fnstsw ax
        mov value,ax
    }
    return value;
}
}
extern "C" __declspec(dllexport) int __cdecl ProductionGoldChoicesGuardOracle(unsigned edition){
    return edition==1&&CrossplayGoldChoices::Methods()?1:0;
}
int main(){
    using namespace CrossplayGoldChoices;uint32_t flags=99;
    for(unsigned positions=0;positions<3;++positions)for(unsigned teams=0;teams<3;++teams)for(unsigned coop=0;coop<3;++coop){
        const bool valid=positions<2&&teams<2&&coop<2;
        Check(Flags(positions,teams,coop,flags)==valid);
        Check(flags==(valid?(positions|(teams<<1)|(coop<<2)):0u));
    }
    for(const std::string value:std::vector<std::string>{"", "Variant", "Maps/Variant", "\\Download\\Template", "\\Download\\Template\\Variant",std::string(255,'a')}){
        Set(value);const auto csr=_mm_getcsr();const auto status=Status();SetLastError(1901);std::string result="old";
        Check(Read(result)&&result==value&&gets==1&&destroys==1);
        Check(GetLastError()==1901&&_mm_getcsr()==csr&&Status()==status);
    }
    for(const std::string value:std::vector<std::string>{"../Variant", "C:/Variant", "\\Other\\Variant", "Variant\n", std::string(256,'a')}){
        Set(value);std::string result="old";Check(!Read(result)&&result.empty()&&gets==1&&destroys==1);
    }
    Set(std::string("a\0b",3));std::string result;Check(!Read(result)&&destroys==1);
    Set("Variant");wrong_return=true;Check(!Read(result)&&destroys==0);
    Set("Variant");requested.capacity=requested.length;Check(!Read(result)&&destroys==0);
    Set("Variant");storage[requested.length]='x';Check(!Read(result)&&destroys==0);
    Set("Variant");requested.data=nullptr;Check(!Read(result)&&destroys==0);
    Set("Variant");requested.length=65536;Check(!Read(result)&&destroys==0);
    requested={};gets=destroys=0;wrong_return=false;Check(Read(result)&&result.empty()&&gets==1&&destroys==1);
    gets=destroys=0;Check(!ReadTemplateWith(nullptr,reinterpret_cast<Destroy>(&DestroyText),result)&&gets==0&&destroys==0);
    Check(!ReadTemplateWith(&Get,nullptr,result)&&gets==0&&destroys==0);
    Cache cache{0x20000,"Maps/Map","",7};Check(Same(cache,cache));
    auto changed=cache;changed.address=0;Check(!Same(cache,changed));
    changed=cache;changed.map="Maps/Other";Check(!Same(cache,changed));
    changed=cache;changed.save="SelectedSave";Check(!Same(cache,changed));
    changed=cache;changed.flags=0;Check(!Same(cache,changed));
    CrossplayNative::Participant expected{101,1,201,301,"Host",true,true};
    CrossplayNative::LobbySnapshot snapshot;snapshot.instance=0x30000;snapshot.local_slot=1;snapshot.host=true;snapshot.members.push_back({expected,2});
    Check(Local(snapshot,0x30000,expected));
    for(uintptr_t session:{uintptr_t(0),uintptr_t(0x40000)})Check(!Local(snapshot,session,expected));
    auto altered=snapshot;altered.host=false;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.local_slot=2;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members.push_back(altered.members[0]);Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].status=1;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.local=false;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.host=false;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.slot=2;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.connection=102;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.principal=202;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.station=302;Check(!Local(altered,0x30000,expected));
    altered=snapshot;altered.members[0].player.name="Other";Check(!Local(altered,0x30000,expected));
    std::printf("PASS %u Gold choice checks; typed original getter/destructor ABI modeled, exact cleanup and LastError/FP retained. Native game methods not executed.\n",checks);
    return 0;
}
