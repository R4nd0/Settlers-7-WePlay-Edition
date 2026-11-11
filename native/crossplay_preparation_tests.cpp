#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_preparation.h"
#include <map>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Check(bool okay,const char* why) {++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
struct Fixture {
    std::map<uint32_t,unsigned char> memory;
    CrossplayNative::HistoryPreparationDetail::Layout pin{};
    CrossplayNative::LobbySnapshot snapshot;
    static constexpr uint32_t manager=0x200000,configuration=0x210000,rows=0x220000,backend=0x240000,session=0x250000;
    uint32_t pointer_offset=0;
    template<class T> void Put(uint32_t at,T value) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(size_t i=0;i<sizeof(value);++i)memory[at+static_cast<uint32_t>(i)]=bytes[i];
    }
    Fixture(Crossplay::Edition edition,uint8_t mode) {
        Check(CrossplayNative::HistoryPreparationDetail::LayoutFor(edition,pin),"known History layout");
        snapshot.instance=0x260000;snapshot.host=false;snapshot.local_slot=2;
        for(uint32_t slot=1;slot<=2;++slot) {
            CrossplayNative::LobbyMember member{};member.status=2;
            member.player.connection=slot+100;member.player.slot=slot;member.player.principal=slot+200;
            member.player.host=slot==1;member.player.local=slot==2;member.player.name=slot==1?"Host":"Guest";
            snapshot.members.push_back(member);
        }
        pointer_offset=((static_cast<uint32_t>(mode)-1)&1)*0xc4+0x98;
        Put(pin.manager_pointer,manager);Put(manager,pin.manager_type);Put(manager+4,mode);Put(manager+pointer_offset,configuration);
        Put(configuration,rows);Put(configuration+4,rows);Put<uint8_t>(rows+0x8db8,4);
        for(uint32_t slot=1;slot<=2;++slot) {
            const auto& p=snapshot.members[slot-1].player;const uint32_t row=rows+slot*0x5a8,name=0x230000+slot*0x100;
            Put(row+0x18,p.principal);Put(row+0x5c,slot);Put<uint32_t>(row+0x60,3);Put<uint32_t>(row+0x64,1);
            Put(row+0x1c,name);Put<uint32_t>(row+0x20,static_cast<uint32_t>(p.name.size()+1));Put<uint32_t>(row+0x24,static_cast<uint32_t>(p.name.size()));
            for(size_t i=0;i<=p.name.size();++i)Put<char>(name+static_cast<uint32_t>(i),p.name.c_str()[i]);
        }
        Put(pin.backend_pointer,backend);Put(backend+0xa4c,session);Put(session,pin.session_type);
        Put(session+0x20,snapshot.members[1].player.connection);Put(session+0x24,snapshot.members[0].player.connection);
    }
    bool Read(uint32_t at,void* out,size_t size) const {
        for(size_t i=0;i<size;++i) {
            auto found=memory.find(at+static_cast<uint32_t>(i));if(found==memory.end())return false;
            static_cast<unsigned char*>(out)[i]=found->second;
        }
        return true;
    }
    bool Match() const {return CrossplayNative::HistoryPreparationDetail::Matches(pin,snapshot,[&](uint32_t a,void* o,size_t n){return Read(a,o,n);});}
};
}
int main() {
    using Crossplay::Edition;
    CrossplayNative::HistoryPreparationDetail::Layout invalid{};
    Check(!CrossplayNative::HistoryPreparationDetail::LayoutFor(Edition::Gold,invalid),"Gold cannot use History preparation addresses");
    Check(!CrossplayNative::HistoryPreparationDetail::LayoutFor(static_cast<Edition>(99),invalid),"unknown edition excluded");
    for(auto edition:{Edition::UbisoftHistory,Edition::SteamHistory})for(uint8_t mode:{uint8_t(0),uint8_t(1)}) {
        Fixture base(edition,mode);Check(base.Match(),"exact original guest preparation accepted in either buffer mode");
        // Every byte used by the accepted snapshot is required, including the
        // complete native strings and their terminators.
        for(const auto& entry:base.memory) {
            Fixture missing=base;missing.memory.erase(entry.first);Check(!missing.Match(),"unreadable required byte rejected");
        }
        const uint32_t addresses[]={base.pin.manager_pointer,Fixture::manager,Fixture::manager+base.pointer_offset,
            Fixture::configuration,Fixture::configuration+4,base.pin.backend_pointer,Fixture::backend+0xa4c,Fixture::session,
            Fixture::session+0x20,Fixture::session+0x24};
        for(uint32_t address:addresses){Fixture changed=base;changed.Put<uint32_t>(address,0xffffffff);Check(!changed.Match(),"replaced or wrapping ownership root rejected");}
        for(uint32_t slot=1;slot<=2;++slot)for(uint32_t offset:{0x18u,0x5cu,0x60u,0x64u,0x1cu,0x20u,0x24u}) {
            Fixture changed=base;changed.Put<uint32_t>(Fixture::rows+slot*0x5a8+offset,0);Check(!changed.Match(),"ID, slot, state, kind and string mismatch rejected");
        }
        Fixture changed=base;changed.Put<uint8_t>(Fixture::manager+4,2);Check(!changed.Match(),"invalid buffer selector rejected");
        changed=base;changed.Put<uint8_t>(Fixture::rows+0x8db8,8);Check(!changed.Match(),"unqualified slot count rejected");
        changed=base;changed.memory[0x230100]^=1;Check(!changed.Match(),"different native row name rejected");
        changed=base;changed.Put<uint32_t>(Fixture::session+0x24,base.snapshot.members[1].player.connection);Check(!changed.Match(),"backend retaining local host ownership rejected");
        changed=base;changed.snapshot.host=true;Check(!changed.Match(),"member-list host excluded");
        changed=base;changed.snapshot.local_slot=1;Check(!changed.Match(),"wrong local slot excluded");
        changed=base;changed.snapshot.members[0].status=1;Check(!changed.Match(),"unconfirmed member excluded");
        changed=base;changed.snapshot.members[0].player.local=true;Check(!changed.Match(),"two local members excluded");
        changed=base;changed.snapshot.members[0].player.host=false;Check(!changed.Match(),"missing foreign host excluded");
        changed=base;changed.snapshot.members[0].player.slot=2;Check(!changed.Match(),"duplicate slot excluded");
        changed=base;changed.snapshot.members[0].player.principal=changed.snapshot.members[1].player.principal;Check(!changed.Match(),"duplicate effective IDs excluded");
        changed=base;changed.snapshot.members[1].player.name.clear();Check(!changed.Match(),"empty unqualified UI name excluded");
        changed=base;changed.snapshot.members[1].player.principal=0;changed.Put<uint32_t>(Fixture::rows+2*0x5a8+0x18,2);Check(changed.Match(),"actual zero principal uses native assigned-slot fallback");
        unsigned seen=0;
        Check(!CrossplayNative::HistoryPreparationDetail::Matches(base.pin,base.snapshot,[&](uint32_t at,void* out,size_t size){
            if(at==base.pin.manager_pointer && ++seen==2)return false;return base.Read(at,out,size);
        }),"ownership root changing before completion excluded");
    }
    std::printf("PASS %u History preparation ownership/read-only checks; no game code executed\n",checks);
}
