#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_human_profile_selection.h"
#include <cstdio>
#include <cstdlib>
extern "C" __declspec(dllexport) uint32_t __cdecl HumanProfileSelectionGuardOracle(){return CrossplayHumanProfileSelection::Methods()?1u:0u;}
int main(){
    using namespace CrossplayHumanProfileSelection;unsigned checks=0;
    auto check=[&](bool value){++checks;if(!value){printf("FAIL human profile selection %u\n",checks);std::exit(1);}};
    Crossplay::Peer a,b;a.local_id.fill(0x11);a.remote_id.fill(0x22);b.local_id=a.remote_id;b.remote_id=a.local_id;
    const Names expected{"SourceHuman","SourceGuest"};Names actual;std::vector<unsigned char> frame;
    for(uint32_t revision:{1u,2u}){
        check(Frame(a,revision,expected,frame));check(Receive(b,revision,frame,actual)&&actual==expected);
        for(size_t i=0;i<frame.size();++i){
            auto changed=frame;changed[i]^=0x80;
            check(!Receive(b,revision,changed,actual));
        }
        auto changed=frame;changed.push_back(0);check(!Receive(b,revision,changed,actual));
        check(!Receive(a,revision,frame,actual));check(!Receive(b,3-revision,frame,actual));
    }
    check(!Frame(a,0,expected,frame));check(!Frame(a,3,expected,frame));
    auto bad=expected;bad[0]="";check(!Frame(a,1,bad,frame));bad=expected;bad[1]="Bad\nKey";check(!Frame(a,1,bad,frame));
    std::vector<unsigned char> rows(25*0x5c4),original=rows;
    for(size_t offset:{size_t(0x5c4+0x18),size_t(2*0x5c4+0x1b)}){rows[offset]^=1;check(Preserved(original.data(),rows.data(),rows.size()));rows[offset]^=1;}
    for(size_t offset:{size_t(0x18),size_t(0x5c4+0x1c),size_t(2*0x5c4+0x5c),size_t(0x5c4+0x64),size_t(0x5c4+0x68),size_t(0x5c4+0x574)}){
        rows[offset]^=1;check(!Preserved(original.data(),rows.data(),rows.size()));rows[offset]^=1;}
    CrossplayNative::ParticipantEvent binding;CrossplayConfigurationReceipt::Interval interval;CrossplayConfigurationReceipt::Receipt receipt;
    SetLastError(1885);check(!ApplyGold(binding,{1,2},expected,interval,receipt));check(GetLastError()==1885);
    std::vector<unsigned char> history_rows(0x8dc0),history_before=history_rows;
    for(size_t offset:{size_t(0x5a8+0x18),size_t(2*0x5a8+0x1b)}){history_rows[offset]^=1;check(HistoryPreserved(history_before.data(),history_rows.data(),history_rows.size()));history_rows[offset]^=1;}
    for(size_t offset:{size_t(0x18),size_t(0x5a8+0x1c),size_t(2*0x5a8+0x5c),size_t(0x5a8+0x64),size_t(0x5a8+0x68),size_t(0x5a8+0x55c)}){
        history_rows[offset]^=1;check(!HistoryPreserved(history_before.data(),history_rows.data(),history_rows.size()));history_rows[offset]^=1;}
    CrossplayNative::LobbyAdmission admission;CrossplayNative::HistoryClaimDetail::Interval selected;
    check(!RestoreHistorySelection(binding,admission,selected,expected,GetTickCount64()));check(GetLastError()==1885);
    printf("PASS %u authenticated human-profile key frame/scope/preservation checks; authored only, no native selection or world qualified.\n",checks);
}
