#pragma once
#include "crossplay_logistics_active.h"

// Read-only NEO metadata export from an already admitted, begun native world.
// This does not authorize a save, select an archive, acknowledge file receipt or
// restore a world. Those original operations retain their separate controller.
namespace CrossplayNeoSnapshot {
inline bool LogicTurn(const std::string& command,uint32_t& turn){
    turn=0;constexpr char prefix[]="save-snapshot:";constexpr size_t size=sizeof(prefix)-1;
    if(command.size()<=size || command.size()>size+10 || command.compare(0,size,prefix)
        || (command.size()>size+1 && command[size]=='0'))return false;
    uint32_t value=0;
    for(size_t i=size;i<command.size();++i){
        const unsigned char digit=static_cast<unsigned char>(command[i]);
        if(digit<'0' || digit>'9' || value>(UINT32_MAX-1-(digit-'0'))/10)return false;
        value=value*10+(digit-'0');
    }
    turn=value;return true;
}
inline bool Clock(CrossplayNativeClock::Snapshot& output){
#ifdef S7_CROSSPLAY_LOGISTICS_TESTING
    if(CrossplayLogisticsActive::test_clock)return CrossplayLogisticsActive::test_clock(output);
#endif
    const auto pin=CrossplayNativeClock::Pin(CrossplayLogisticsActive::edition);
    return CrossplayNativeClock::Guard(pin) && CrossplayNativeClock::Read(pin,output);
}
inline bool Scope(Crossplay::Edition edition,DWORD owner){
    using namespace CrossplayLogisticsActive;
    const auto number=static_cast<unsigned>(edition);
    return number>=1 && number<=3 && owner && owner==GetCurrentThreadId()
        && installed && begun && CrossplayLogisticsActive::owner==owner
        && CrossplayLogisticsActive::edition==edition && ready && ready()
        && (!resume_armed || resume_restored) && ledger.enabled==selected;
}
inline bool Capture(Crossplay::Edition edition,DWORD owner,uint32_t logic_turn,std::string& output){
    CrossplayLogisticsBinding::ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    output.clear();using namespace CrossplayLogisticsActive;
    CrossplayNativeClock::Snapshot before{},after{};CrossplayLogisticsBinding::World world{},observed{};
    if(logic_turn==UINT32_MAX || !Scope(edition,owner) || !Clock(before)
        || !before.root || before.turn!=logic_turn+1 || !Advance()
        || !CrossplayLogisticsBinding::CaptureWorld(world) || !ledger.Same(world))return false;
    const bool enabled=selected;neo_logistics::SavedState state;
    if(!ledger.Snapshot(world,before.turn,state) || state.enabled!=enabled)return false;
    state.logic_turn=logic_turn;auto serialized=neo_logistics::Encode(state);
    // Allocation/serialization must not lend a previous world's ledger to a
    // different session, turn, native clock or currently selected NEO mode.
    if(serialized.empty() || !Scope(edition,owner) || selected!=enabled || !Clock(after)
        || after.root!=before.root || after.turn!=before.turn || after.current!=before.current || after.pending!=before.pending
        || !CrossplayLogisticsBinding::CaptureWorld(observed) || !ledger.Same(observed)
        || observed.logic!=world.logic || observed.main!=world.main || observed.priority!=world.priority || observed.transfers!=world.transfers
        || ledger.turn!=before.turn)return false;
    output=std::move(serialized);return true;
}
}
