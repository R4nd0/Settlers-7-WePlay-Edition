#include "crossplay_configuration_receipt.h"
#include "crossplay_configuration_readiness.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){printf("FAIL configuration receipt check %u\n",checks);std::exit(1);}}
void Word(std::vector<unsigned char>& bytes,uint32_t value){const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);}
void Text(std::vector<unsigned char>& bytes,const std::string& value){
    bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));
    if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
}
std::vector<unsigned char> Wire(unsigned flags=0){
    std::vector<unsigned char> result;Word(result,CrossplaySessionConfig::Class);Word(result,CrossplaySessionConfig::Type);Word(result,1);result.push_back(1);
    Text(result,"Maps/ActualSelection");Text(result,"");Word(result,8);
    for(unsigned i=0;i<8;++i){
        result.push_back(i<2?static_cast<unsigned char>((flags>>i)&1):0);Word(result,i<2?i+1:UINT32_MAX);
        result.push_back(0);Word(result,i<2?(i?0u:1u):0u);result.push_back(i<2?3u:5u);
    }
    return result;
}
std::vector<unsigned char> RoomWire(const CrossplaySessionConfig::RoomConnections& ids,unsigned ready_flag){
    auto result=Wire();
    CrossplaySessionConfig::Layout layout;
    Check(CrossplaySessionConfig::ReadLayout(result,1,layout));
    CrossplayChat::Put(result.data()+8,ids.host);
    for(size_t i=0;i<CrossplaySessionConfig::Slots;++i){
        auto* row=result.data()+layout.slots_at+i*CrossplaySessionConfig::SlotBytes;
        const auto cid=ids.stations[i];
        const bool human=cid!=UINT32_MAX;
        const bool computer=!human && i<ids.capacity;
        row[0]=static_cast<unsigned char>(computer || (human && cid==ids.host && ready_flag));
        CrossplayChat::Put(row+1,human || computer?static_cast<uint32_t>(i+1):UINT32_MAX);
        CrossplayChat::Put(row+6,human?cid:0);
        row[10]=static_cast<unsigned char>(human?3:computer?4:5);
    }
    return result;
}
}
int main(){
    using namespace CrossplayConfigurationReceipt;
    const Interval own{7,0x10000,0x20000};const CrossplaySessionConfig::Connections ids{1,0};const auto wire=Wire();
    Receipt initial;Check(initial.Observe(own,wire,0)==Observation::Invalid);
    for(unsigned flags=0;flags<4;++flags){
        const auto selected=Wire(flags);Receipt receipt;
        Check(receipt.Arm(own,ids,"Maps/ActualSelection",selected,100));
        Check(!receipt.ConsumedIn(own,101));
        Check(receipt.Observe(own,selected,101)==Observation::Pending); // Cache already matches, but no consumption.
        Check(receipt.Consumed(own,selected,102));
        Check(receipt.ConsumedIn(own,102)&&receipt.ConsumedIn(own,10100));
        Check(!receipt.ConsumedIn(own,99)&&!receipt.ConsumedIn(own,10101));
        for(unsigned which=0;which<3;++which){
            auto stale=own;if(which==0)++stale.owner;if(which==1)++stale.session;if(which==2)++stale.cache;
            Check(!receipt.ConsumedIn(stale,103));
        }
        Check(receipt.Observe(own,selected,103)==Observation::Applied);
        Check(receipt.Observe(own,Wire(flags^1),104)==Observation::Pending); // Real readiness changed; do not mask it.
        Check(receipt.Observe(own,selected,105)==Observation::Applied);
        Check(!receipt.Consumed(own,selected,106)); // A repeated notification cannot advance an old transaction.
        Check(receipt.Observe(own,selected,107)==Observation::Invalid);
    }
    for(unsigned which=0;which<3;++which){
        auto stale=own;if(which==0)++stale.owner;if(which==1)++stale.session;if(which==2)++stale.cache;
        Receipt receipt;Check(receipt.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(!receipt.Consumed(stale,wire,101));Check(!receipt.Active());
        Receipt other;Check(other.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(other.Observe(stale,wire,101)==Observation::Invalid);
    }
    for(uint64_t now:{uint64_t(99),uint64_t(10101)}){
        Receipt receipt;Check(receipt.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(!receipt.Consumed(own,wire,now));
        Receipt other;Check(other.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(other.Observe(own,wire,now)==Observation::Invalid);
    }
    Receipt exact;Check(exact.Arm(own,ids,"Maps/ActualSelection",wire,100));
    Check(exact.Consumed(own,wire,10100)&&exact.Observe(own,wire,10100)==Observation::Applied);
    for(auto invalid:std::vector<Interval>{{0,own.session,own.cache},{own.owner,0,own.cache},{own.owner,own.session,0}}){
        Receipt receipt;Check(!receipt.Arm(invalid,ids,"Maps/ActualSelection",wire,100));
    }
    Receipt wrong_map;Check(!wrong_map.Arm(own,ids,"Maps/OtherSelection",wire,100));
    Receipt wrong_ids;Check(!wrong_ids.Arm(own,{0,1},"Maps/ActualSelection",wire,100));
    Receipt repeated;Check(repeated.Arm(own,ids,"Maps/ActualSelection",wire,100));Check(!repeated.Arm(own,ids,"Maps/ActualSelection",wire,100));
    for(unsigned flags=0;flags<4;++flags){
        using CrossplayConfigurationReadiness::Difference;
        const auto selected=Wire(flags);CrossplayReadyState::Snapshot ready;
        CrossplaySessionConfig::Layout layout;Check(CrossplaySessionConfig::Inspect(selected,ids,layout));
        Check(CrossplayConfigurationReadiness::Compare(selected,selected,ids,ready)==Difference::Equal&&!ready.revision);
        auto other=selected;other[layout.slots_at]^=1;
        Check(CrossplayConfigurationReadiness::Compare(selected,other,ids,ready)==Difference::HostOnly
            &&ready.revision==1&&ready.host==(flags&1)&&ready.guest==((flags>>1)&1));
        for(size_t at=0;at<selected.size();++at){
            if(at==layout.slots_at)continue;
            other=selected;other[at]^=1;
            Check(CrossplayConfigurationReadiness::Compare(selected,other,ids,ready)==Difference::Invalid&&!ready.revision);
        }
        Check(CrossplayConfigurationReadiness::Compare(selected,selected,{0,1},ready)==Difference::Invalid);
        other=selected;other[layout.slots_at]=2;
        Check(CrossplayConfigurationReadiness::Compare(selected,other,ids,ready)==Difference::Invalid);
    }
    for(unsigned char capacity=2;capacity<=CrossplaySessionConfig::Slots;++capacity){
        for(size_t host=0;host<capacity;++host){
            CrossplaySessionConfig::RoomConnections room;
            room.capacity=capacity;
            room.host=0;
            room.stations[host]=room.host;
            room.stations[(host+1)%capacity]=101;
            if(capacity>=3)room.stations[(host+2)%capacity]=303;
            using CrossplayConfigurationReadiness::CompareRoom;
            using CrossplayConfigurationReadiness::Difference;
            CrossplayConfigurationReadiness::RoomHostReady ready;
            for(unsigned flag:{0u,1u}){
                const auto original=RoomWire(room,flag);
                CrossplaySessionConfig::Layout layout;
                Check(CrossplaySessionConfig::InspectRoom(original,room,layout));
                const size_t ready_at=layout.slots_at+host*CrossplaySessionConfig::SlotBytes;
                Check(CompareRoom(original,original,room,ready)==Difference::Equal && !ready.player_slot);
                for(size_t at=0;at<original.size();++at){
                    auto readback=original;readback[at]^=1;
                    const auto difference=CompareRoom(original,readback,room,ready);
                    if(at==ready_at)Check(difference==Difference::HostOnly && ready.player_slot==host+1 && ready.value==flag);
                    else Check(difference==Difference::Invalid && !ready.player_slot);
                }
            }
        }
    }
    // The original History cache retains zero station placeholders. Canonical
    // routing comparison still needs the exact consumed event and must retain
    // every non-routing field, including actual Ready/privacy and slot states.
    for(auto connections:std::vector<CrossplaySessionConfig::Connections>{{1,0},{0,1},{101,102}}){
        for(unsigned flags=0;flags<4;++flags){
            auto cache=Wire(flags);CrossplaySessionConfig::Layout layout;
            Check(CrossplaySessionConfig::Inspect(cache,ids,layout));
            CrossplayChat::Put(cache.data()+8,connections.host);
            CrossplayChat::Put(cache.data()+layout.slots_at+6,0);
            CrossplayChat::Put(cache.data()+layout.slots_at+CrossplaySessionConfig::SlotBytes+6,0);
            auto expected=cache;
            CrossplayChat::Put(expected.data()+layout.slots_at+6,connections.host);
            CrossplayChat::Put(expected.data()+layout.slots_at+CrossplaySessionConfig::SlotBytes+6,connections.guest);
            std::vector<unsigned char> bound;
            Check(CrossplaySessionConfig::BindSource(cache,connections,bound)&&bound==expected);
            Receipt receipt;Check(receipt.Arm(own,connections,"Maps/ActualSelection",expected,100));
            Check(receipt.Observe(own,bound,101)==Observation::Pending);
            Check(receipt.Consumed(own,expected,102));
            Check(receipt.Observe(own,bound,103)==Observation::Applied);
            auto changed=cache;changed[layout.slots_at]^=1;
            Check(CrossplaySessionConfig::BindSource(changed,connections,bound));
            Check(receipt.Observe(own,bound,104)==Observation::Pending);
            changed=cache;changed[layout.slots_at+5]=1;
            Check(CrossplaySessionConfig::BindSource(changed,connections,bound));
            Check(receipt.Observe(own,bound,105)==Observation::Pending);
            changed=cache;CrossplayChat::Put(changed.data()+layout.slots_at+6,UINT32_MAX);
            Check(!CrossplaySessionConfig::BindSource(changed,connections,bound)&&bound.empty());
            changed=cache;CrossplayChat::Put(changed.data()+layout.slots_at+1,2);
            Check(!CrossplaySessionConfig::BindSource(changed,connections,bound)&&bound.empty());
        }
    }
    for(size_t at=0;at<wire.size();++at){
        auto altered=wire;altered[at]^=0x80;Receipt receipt;
        Check(receipt.Arm(own,ids,"Maps/ActualSelection",wire,100));Check(!receipt.Consumed(own,altered,101));
    }
    printf("PASS %u configuration consumption receipt checks; matching cache without original consumption remains pending; owner/session/cache/bytes/deadline bound.\n",checks);
}
