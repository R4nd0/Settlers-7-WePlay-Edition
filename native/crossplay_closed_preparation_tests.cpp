#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_closed_preparation.h"
#include <stdio.h>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayClosedPreparationMethodsOracle(uint32_t value) {
    CrossplayClosedPreparation::Pin pin{};const auto edition=static_cast<Crossplay::Edition>(value);
    return CrossplayClosedPreparation::Layout(edition,pin) && CrossplayClosedPreparation::Methods(edition,pin)?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayClosedPreparationApplyOracle(uint32_t value,void* row,uint32_t target) {
    CrossplayClosedPreparation::Pin pin{};const auto edition=static_cast<Crossplay::Edition>(value);
    return CrossplayClosedPreparation::Layout(edition,pin)
        && CrossplayClosedPreparation::Detail::Apply(edition,pin,static_cast<unsigned char*>(row),target)?1u:0u;
}
int main() {
    using namespace CrossplayClosedPreparation;
    unsigned checks=0;
    const auto check=[&checks](bool okay){++checks;if(!okay){fprintf(stderr,"FAIL closed preparation check %u\n",checks);exit(1);}};
    std::array<unsigned char,RowSize> row{};char name[]="Former AI";
    CrossplayChat::Put(row.data()+0x18,29);CrossplayChat::Put(row.data()+0x5c,UINT32_MAX);
    CrossplayChat::Put(row.data()+0x60,1);CrossplayChat::Put(row.data()+0x64,1);
    const Detail::Text text{name,sizeof(name),sizeof(name)-1,7};memcpy(row.data()+0x1c,&text,sizeof(text));
    for(uint32_t target:{0u,1u,2u,3u,4u,5u,25u,UINT32_MAX})check(Detail::Eligible(row.data(),target)==(target==3 || target==4));
    auto changed=row;CrossplayChat::Put(changed.data()+0x5c,1);check(!Detail::Eligible(changed.data(),3));
    changed=row;CrossplayChat::Put(changed.data()+0x18,0);check(!Detail::Eligible(changed.data(),3));
    changed=row;CrossplayChat::Put(changed.data()+0x18,UINT32_MAX);check(!Detail::Eligible(changed.data(),3));
    changed=row;CrossplayChat::Put(changed.data()+0x24,65);check(!Detail::Eligible(changed.data(),3));
    changed=row;CrossplayChat::Put(changed.data()+0x20,1);check(!Detail::Eligible(changed.data(),3));
    changed=row;CrossplayChat::Put(changed.data()+0x60,3);check(!Detail::Eligible(changed.data(),3));
    changed=row;CrossplayChat::Put(changed.data()+0x64,2);check(!Detail::Eligible(changed.data(),3));
    for(size_t i=0;i<row.size();++i) {
        changed=row;changed[i]^=1;check(Detail::Preserved(row.data(),changed.data())==((i>=0x1c && i<0x2c) || (i>=0x5c && i<0x60) || (i>=0x64 && i<0x68)));
    }
    CrossplayNative::ParticipantEvent binding;
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory})
        check(!Project(edition,binding,{},3));
    printf("PASS %u closed-preparation ownership/range/preservation checks; no original game code executed.\n",checks);
}
