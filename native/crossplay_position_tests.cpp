#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_position_codec.h"
#include <stdio.h>
using namespace CrossplayPreparationPositions;
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayPositionSetterMethodsOracle(uint32_t edition) {
    return CrossplayNativeCodec::HistoryPositionDetail::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
struct Fixture {
    static constexpr uint32_t Base=0x20000000,Rows=Base+0x1000,H0=Base+0xb000,H1=Base+0xb400,N0=H0+0x20,N1=N0+0x10,A0=H1+0x20;
    std::vector<unsigned char> memory=std::vector<unsigned char>(0x20000,0);
    uint32_t denied=0,reads=0;bool switch_root=false;
    void put(uint32_t at,uint32_t value){memcpy(memory.data()+at-Base,&value,4);}
    void byte(uint32_t at,uint8_t value){memory[at-Base]=value;}
    void node(uint32_t at,uint32_t next,uint32_t previous,uint8_t index){put(at,next);put(at+4,previous);byte(at+8,index);}
    Fixture() {
        byte(Rows+0x8db8,4);put(Rows+0x8dbc,H0);put(Rows+0x8dc4,H1);
        node(H0,N0,N1,0);node(N0,N1,H0,1);node(N1,H0,N0,2);
        node(H1,A0,A0,0);node(A0,H1,H1,3);
        for(uint32_t i=1;i<=3;++i)byte(Rows+i*RowSize+0x58,static_cast<uint8_t>(i));
    }
    bool read(uint32_t at,void* out,size_t size) {
        ++reads;if(reads>300 || at<Base || size>memory.size() || at-Base>memory.size()-size
            || (denied && at<=denied && denied-at<size))return false;
        memcpy(out,memory.data()+at-Base,size);
        if(switch_root && at==Rows+0x8dbc){switch_root=false;put(Rows+0x8dbc,H1);}
        return true;
    }
    bool capture(Snapshot& out,uint32_t at=Rows){return Capture(at,[&](uint32_t address,void* data,size_t size){return read(address,data,size);},out);}
};
int main() {
    unsigned checks=0;auto check=[&](bool okay){++checks;if(!okay){fprintf(stderr,"FAIL position check %u\n",checks);exit(1);}};
    Fixture valid;Snapshot before,after;check(valid.capture(before));check(before.nodes==3 && before.seen[1] && before.seen[2] && before.seen[3]);
    check(Plan(before,1,2,after) && after.positions[1]==2 && after.positions[2]==1 && after.positions[3]==3);
    check(!Equal(before,after));auto expected=before;expected.positions[1]=2;expected.positions[2]=1;check(Equal(expected,after));
    check(Plan(before,1,3,after) && after.positions[1]==3 && after.positions[3]==1 && after.positions[2]==2);
    check(Plan(before,2,4,after) && after.positions[2]==4 && after.positions[1]==1 && after.positions[3]==3);
    check(Plan(before,1,1,after) && Equal(before,after));
    for(uint32_t target:{0u,3u,25u,UINT32_MAX})check(!Plan(before,target,2,after) && !after.rows);
    for(uint32_t castle:{0u,5u,UINT32_MAX})check(!Plan(before,1,castle,after) && !after.rows);
    for(uint32_t address:{0u,1u,0xffffu,0x7fffffffu-0x8dcc+1,UINT32_MAX}) {
        Fixture f;Snapshot out=before;check(!f.capture(out,address) && !out.rows && !out.nodes);
    }
    auto reject=[&](Fixture f){Snapshot out=before;check(!f.capture(out) && !out.rows && !out.nodes && f.reads<=300);};
    for(uint8_t slots:std::array<uint8_t,3>{0,3,5}){Fixture f;f.byte(Fixture::Rows+0x8db8,slots);reject(f);}
    for(uint32_t head:{0u,0xffffu,0x7ffffffcu,Fixture::H1}){Fixture f;f.put(Fixture::Rows+0x8dbc,head);reject(f);}
    for(uint32_t next:{0u,0x7ffffffcu,Fixture::N0,Fixture::H1}){Fixture f;f.put(Fixture::N1,next);reject(f);}
    {Fixture f;f.put(Fixture::N0+4,Fixture::N1);reject(f);}
    {Fixture f;f.put(Fixture::H0+4,Fixture::N0);reject(f);}
    {Fixture f;f.put(Fixture::H1,Fixture::N1);reject(f);}
    {Fixture f;f.put(Fixture::H1,Fixture::H1);f.put(Fixture::H1+4,Fixture::A0);reject(f);}
    {Fixture f;f.put(Fixture::H0,Fixture::H0);f.put(Fixture::H0+4,Fixture::H0);reject(f);}
    for(uint8_t index:std::array<uint8_t,5>{1,5,24,25,255}){Fixture f;f.byte(Fixture::N1+8,index);reject(f);}
    for(uint8_t position:std::array<uint8_t,4>{0,3,5,255}){Fixture f;f.byte(Fixture::Rows+RowSize+0x58,position);reject(f);}
    {Fixture f;f.byte(Fixture::A0+8,0);f.byte(Fixture::Rows+0x58,4);reject(f);}
    {Fixture f;f.switch_root=true;reject(f);}
    for(uint32_t address:{Fixture::Rows+0x8db8,Fixture::Rows+0x8dbc,Fixture::Rows+0x8dc4,
                          Fixture::H0,Fixture::H0+4,Fixture::H1,Fixture::H1+4,
                          Fixture::N0,Fixture::N0+4,Fixture::N0+8,Fixture::N1,Fixture::A0}) {
        Fixture f;f.denied=address;reject(f);
    }
    for(uint32_t i=0;i<Count;++i){Fixture f;f.denied=Fixture::Rows+i*RowSize+0x58;reject(f);}
    {Fixture f;f.byte(Fixture::A0+8,4);f.byte(Fixture::Rows+4*RowSize+0x58,4);Snapshot out;check(f.capture(out));check(Plan(out,1,4,after) && after.positions[4]==1);}
    {Fixture f;f.byte(Fixture::A0+8,0);Snapshot out;check(f.capture(out));}
    for(uint32_t i=0;i<Count;++i){auto changed=before;changed.positions[i]^=0x80;check(!Equal(changed,before));}
    check(!WePlayPositionSetterMethodsOracle(0) && !WePlayPositionSetterMethodsOracle(1) && !WePlayPositionSetterMethodsOracle(4));
    CrossplayNative::ParticipantEvent binding;
    check(!CrossplayNativeCodec::ProjectHistoryCastlePosition(Crossplay::Edition::Gold,binding,{42,43},1,2));
    check(!CrossplayNativeCodec::ProjectHistoryCastlePosition(Crossplay::Edition::UbisoftHistory,binding,{42,43},0,2));
    printf("PASS %u bounded native position-list, swap, ownership and exclusion checks. No game/world executed.\n",checks);return 0;
}
