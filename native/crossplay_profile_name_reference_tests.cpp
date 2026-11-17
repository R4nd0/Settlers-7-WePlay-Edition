#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_profile_name_reference.h"
#include <cstdio>
#include <cstdlib>
int main(){
    using namespace CrossplayProfileNameReference;unsigned checks=0;
    auto check=[&](bool value){++checks;if(!value){printf("FAIL native ProfileName reference %u\n",checks);std::exit(1);}};
    unsigned char table[0x1c]{};uint32_t registry[4]{},symbol[3]{0,0,1};uintptr_t entries[3]{0,reinterpret_cast<uintptr_t>(symbol),0};
    CrossplayChat::Put(table+0x18,reinterpret_cast<uintptr_t>(registry));registry[0]=reinterpret_cast<uintptr_t>(entries);registry[2]=registry[3]=3;
    uintptr_t index=0,entry=0;check(InTable(table,1,index,entry));check(index==reinterpret_cast<uintptr_t>(registry)&&entry==reinterpret_cast<uintptr_t>(symbol));
    for(uint32_t invalid:{0u,2u,3u,UINT32_MAX}){check(!InTable(table,invalid,index,entry));check(!index&&!entry);}
    for(unsigned which=0;which<6;++which){
        auto saved=registry[2];auto capacity=registry[3];auto used=symbol[2];auto address=entries[1];
        if(which==0)registry[2]=0;if(which==1)registry[2]=4097;if(which==2)registry[3]=2;
        if(which==3)registry[3]=4097;if(which==4)symbol[2]=0;if(which==5)entries[1]=0;
        check(!InTable(table,1,index,entry));registry[2]=saved;registry[3]=capacity;symbol[2]=used;entries[1]=address;
    }
    check(!InTable(nullptr,1,index,entry));check(!Valid(Crossplay::Edition::UbisoftHistory,1));
    check(!Valid(Crossplay::Edition::Gold,UINT32_MAX));check(!Valid(Crossplay::Edition::SteamHistory,UINT32_MAX));
    check(KnownOrUnassigned(Crossplay::Edition::Gold,0));check(!Valid(Crossplay::Edition::Gold,0));
    check(!KnownOrUnassigned(Crossplay::Edition::Gold,UINT32_MAX));
    check(!KnownOrUnassigned(Crossplay::Edition::UbisoftHistory,0));
    struct Text {const char* data;uint32_t capacity,length,extra;};
    const Text human{"Human",6,5,0},other{"Opponent",9,8,0};
    entries[1]=reinterpret_cast<uintptr_t>(&human);entries[2]=reinterpret_cast<uintptr_t>(&other);
    std::string name;uint32_t resolved=99;
    check(NameInTable(table,1,name)&&name=="Human");check(ResolveInTable(table,"Human",resolved)&&resolved==1);
    check(ResolveInTable(table,"Opponent",resolved)&&resolved==2);
    check(!ResolveInTable(table,"Missing",resolved)&&!resolved);
    entries[2]=entries[1];check(!ResolveInTable(table,"Human",resolved)&&!resolved);entries[2]=reinterpret_cast<uintptr_t>(&other);
    check(!Key("")&&!Key("Bad\nKey")&&!Key(std::string(129,'x')));check(!NameInTable(table,0,name)&&name.empty());
    printf("PASS %u native ProfileName registry scope/range/live-entry checks; authored only, no game identifier created.\n",checks);
}
