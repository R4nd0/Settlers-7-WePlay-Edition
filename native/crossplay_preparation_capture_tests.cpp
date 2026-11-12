#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_preparation_capture.h"
#include <memory>
#include <stdio.h>
namespace {
unsigned checks=0;
void Require(bool value,const char* reason){++checks;if(!value){fprintf(stderr,"FAIL %s\n",reason);exit(1);}}
struct Fixture {
    bool gold;std::vector<unsigned char> rows;
    std::vector<std::unique_ptr<unsigned char[]>> owned;
    std::array<unsigned char*,2> heads{};
    explicit Fixture(bool value):gold(value),rows(value?0x9094:0x8dcc) {
        rows[gold?0x9078:0x8db8]=4;
        for(size_t list=0;list<2;++list) {
            heads[list]=Allocate(12);Pointer(heads[list],heads[list]);Pointer(heads[list]+4,heads[list]);
            Pointer(Header(list),heads[list]);
        }
    }
    unsigned char* Allocate(size_t size){auto p=std::make_unique<unsigned char[]>(size);auto* v=p.get();owned.push_back(std::move(p));return v;}
    unsigned char* Header(size_t list){return rows.data()+(gold?(list?0x908c:0x9080):(list?0x8dc4:0x8dbc));}
    static void Word(unsigned char* at,uint32_t value){CrossplayChat::Put(at,value);}
    static void Pointer(unsigned char* at,const void* value){Word(at,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));}
    unsigned char* Add(size_t list,unsigned char index) {
        auto* node=Allocate(12);auto* head=heads[list];auto* tail=reinterpret_cast<unsigned char*>(CrossplayChat::Word(head+4));
        Pointer(node,head);Pointer(node+4,tail);node[8]=index;Pointer(tail,node);Pointer(head+4,node);
        Word(Header(list)+4,CrossplayChat::Word(Header(list)+4)+1);return node;
    }
    bool Capture(Crossplay::Digest& value){return CrossplayPreparationCapture::Detail::Positions(rows.data(),gold,value);}
};
}
int main() {
    for(bool gold:{false,true}) {
        Crossplay::Digest before{},after{};Fixture empty(gold);
        Require(empty.Capture(before) && CrossplaySetupCheckpoint::Nonzero(before),"valid owned position headers");
        Require(empty.Capture(after) && before==after,"stable position fingerprint");
        empty.rows[gold?0x9078:0x8db8]=3;after=before;
        Require(!empty.Capture(after) && !CrossplaySetupCheckpoint::Nonzero(after),"wrong Monhaim slot bound clears output");
        Fixture lists(gold);lists.Add(0,1);auto* node=lists.Add(0,2);lists.Add(1,3);lists.Add(1,24);
        Require(lists.Capture(before),"active and inactive lists include highest row");
        node[8]=4;Require(lists.Capture(after) && after!=before,"pointed position value mutation retained");
        node[8]=25;Require(!lists.Capture(after),"out of range row rejected");
        node[8]=1;Require(!lists.Capture(after),"duplicate row in one list rejected");
        node[8]=3;Require(!lists.Capture(after),"duplicate row across lists rejected");
        node[8]=2;Fixture::Pointer(node,node);Require(!lists.Capture(after),"position cycle rejected");
        Fixture link(gold);auto* bad=link.Add(0,1);Fixture::Pointer(bad+4,bad);
        Require(!link.Capture(after),"bad position backlink rejected");
        Fixture count(gold);count.Add(0,1);Fixture::Word(count.Header(0)+4,2);
        Require(!count.Capture(after),"position count mismatch rejected");
        for(uint32_t value:{26u,UINT32_MAX}) {
            Fixture bound(gold);Fixture::Word(bound.Header(0)+4,value);
            Require(!bound.Capture(after),"position count bound");
        }
        Fixture missing(gold);Fixture::Pointer(missing.Header(0),reinterpret_cast<void*>(1));
        Require(!missing.Capture(after),"inaccessible position head rejected");
        Fixture alias(gold);Fixture::Pointer(alias.Header(1),alias.heads[0]);
        Require(!alias.Capture(after),"aliased list heads rejected");
        Fixture header(gold);Require(header.Capture(before),"initial position header");
        header.rows[(gold?0x9078:0x8db8)+1]=1;
        Require(header.Capture(after) && before!=after,"position header padding retained");
        if(gold) {
            header.rows[0x907c]=7;
            Require(header.Capture(before) && before!=after,"Gold iterator proxy retained");
        }
    }
    CrossplayNative::ParticipantEvent binding;CrossplayPreparationCapture::Snapshot out{};
    out.complete.bytes=17;out.positions[0]=1;out.players.rows[24].bytes=9;
    Require(!CrossplayPreparationCapture::Capture(static_cast<Crossplay::Edition>(0),binding,{},false,out)
        && !out.complete.bytes && !out.players.rows[24].bytes && !CrossplaySetupCheckpoint::Nonzero(out.positions),"failed complete capture clears all output");
    for(uint32_t version:{0u,2u,7u,9u,UINT32_MAX}) {
        out.format_version=8;out.complete.bytes=17;
        Require(!CrossplayPreparationCapture::Capture(Crossplay::Edition::Gold,binding,{},true,out,nullptr,version)
            && !out.format_version && !out.complete.bytes,"unsupported live preparation format clears output");
    }
    Crossplay::Digest digest{};digest[0]=1;
    Require(!CrossplayPreparationCapture::Detail::Positions(reinterpret_cast<const unsigned char*>(1),true,digest)
        && !CrossplaySetupCheckpoint::Nonzero(digest),"inaccessible preparation clears output");
    printf("PASS %u whole-preparation dependency checks; no original writer or live game executed.\n",checks);return 0;
}
