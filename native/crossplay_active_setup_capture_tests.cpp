#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_active_setup_capture.h"
#include <memory>
#include <stdio.h>
namespace {
unsigned checks=0;
void Require(bool okay,const char* reason) {++checks;if(!okay){fprintf(stderr,"FAIL %s\n",reason);exit(1);}}
struct Fixture {
    bool gold;size_t stride;std::vector<unsigned char> rows;
    std::vector<std::unique_ptr<unsigned char[]>> owned;
    unsigned char *registry=nullptr,*symbols=nullptr,*array=nullptr;
    std::array<unsigned char*,75> heads{};
    explicit Fixture(bool value):gold(value),stride(value?0x5c4:0x5a8),rows(value?0x9079:0x8db9) {
        registry=Allocate(16);symbols=Allocate(32);array=Allocate(8);
        Pointer(registry,array);Pointer(registry+4,Allocate(12));Word(registry+8,2);Word(registry+12,2);
        Pointer(array,symbols);Pointer(array+4,symbols+16);
        Text(symbols+16,"Synthetic Color");
        Pointer(rows.data()+(gold?0x9024:0x8d68)+0x18,registry);
        const size_t offsets[]={gold?0x4e8u:0x4e0u,gold?0x4f4u:0x4e8u,gold?0x56cu:0x554u};
        for(size_t i=0;i<25;++i)for(size_t n=0;n<3;++n) {
            auto* head=Allocate(8);Pointer(head,head);Pointer(head+4,head);
            Pointer(Row(i)+offsets[n],head);heads[i*3+n]=head;
        }
    }
    unsigned char* Allocate(size_t bytes) {
        auto memory=std::make_unique<unsigned char[]>(bytes);auto* pointer=memory.get();
        owned.push_back(std::move(memory));return pointer;
    }
    unsigned char* Row(size_t slot){return rows.data()+slot*stride;}
    static void Word(unsigned char* at,uint32_t value){CrossplayChat::Put(at,value);}
    static void Pointer(unsigned char* at,const void* value){Word(at,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));}
    void Text(unsigned char* object,const char* value) {
        const size_t n=strlen(value);auto* text=Allocate(n+1);memcpy(text,value,n+1);
        Pointer(object,text);Word(object+4,static_cast<uint32_t>(n+1));Word(object+8,static_cast<uint32_t>(n));
    }
    void Vector(unsigned char* header,size_t bytes) {
        auto* data=Allocate(bytes);Pointer(header,data);Pointer(header+4,data+bytes);Pointer(header+8,data+bytes);
    }
    unsigned char* Allies(size_t slot,uint32_t count) {
        auto* head=heads[slot*3+2];auto* previous=head;
        for(uint32_t i=0;i<count;++i) {
            auto* node=Allocate(12);Pointer(node+4,previous);Pointer(previous,node);node[8]=static_cast<unsigned char>(i%25);previous=node;
        }
        Pointer(previous,head);Pointer(head+4,previous);Word(Row(slot)+(gold?0x56c:0x554)+4,count);return previous;
    }
    bool Capture(Crossplay::Digest& digest){return CrossplayActiveSetupCapture::Detail::Dependencies(rows.data(),gold,digest);}
};
}
int main() {
    using namespace CrossplayActiveSetupCapture;
    for(bool gold:{false,true}) {
        Fixture empty(gold);Crossplay::Digest before{},after{};
        Require(empty.Capture(before) && CrossplaySetupCheckpoint::Nonzero(before),"valid 25-row dependencies");
        Require(empty.Capture(after) && before==after,"stable dependency hash");
        Fixture color(gold);Require(color.Capture(before),"initial symbolic table capture");Fixture::Word(color.Row(1)+0x2c,1);
        Require(color.Capture(after) && after!=before,"selected symbolic color retained");
        Fixture bad_color(gold);Fixture::Word(bad_color.Row(1)+0x2c,2);after=before;
        Require(!bad_color.Capture(after) && !CrossplaySetupCheckpoint::Nonzero(after),"out of range color clears digest");
        for(uint32_t count:{0u,4097u,UINT32_MAX}) {
            Fixture bad(gold);Fixture::Word(bad.registry+8,count);
            Require(!bad.Capture(after),"symbol count bounds");
        }
        Fixture capacity(gold);Fixture::Word(capacity.registry+12,1);
        Require(!capacity.Capture(after),"symbol capacity bound");
        Fixture opaque(gold);Require(opaque.Capture(before),"owned symbol allocator identity");
        Fixture::Word(opaque.registry+4,UINT32_MAX);
        Require(opaque.Capture(after) && before!=after,"opaque pool pointer retained without dereference");
        Fixture::Word(opaque.registry+12,1);
        Require(!opaque.Capture(after),"opaque pool pointer never substitutes for capacity");
        for(uint32_t value:{0u,4097u,UINT32_MAX}) {
            Fixture bad(gold);Fixture::Word(bad.registry+12,value);
            Require(!bad.Capture(after),"true symbol capacity bounds");
        }
        Fixture missing(gold);Fixture::Pointer(missing.array,reinterpret_cast<void*>(1));
        Require(!missing.Capture(after),"inaccessible selected symbol");
        Fixture text(gold);text.Text(text.Row(1)+0x1c,"Synthetic Player");
        Require(text.Capture(before),"owned player string");
        auto* data=reinterpret_cast<unsigned char*>(CrossplayChat::Word(text.Row(1)+0x1c));data[0]^=1;
        Require(text.Capture(after) && after!=before,"pointed player text mutation retained");
        data[strlen("Synthetic Player")]=1;
        Require(!text.Capture(after),"unterminated string rejected");
        Fixture too_long(gold);Fixture::Word(too_long.Row(1)+0x24,513);
        Require(!too_long.Capture(after),"string length bound");
        Fixture null_text(gold);Fixture::Word(null_text.Row(1)+0x20,1);
        Require(!null_text.Capture(after),"null string with capacity rejected");
        Fixture rewards(gold);const size_t reward_header=gold?0x78:0x74;
        rewards.Vector(rewards.Row(1)+reward_header,8);
        Require(rewards.Capture(before),"valid pointed vector");
        auto* reward_data=reinterpret_cast<unsigned char*>(CrossplayChat::Word(rewards.Row(1)+reward_header));reward_data[0]=7;
        Require(rewards.Capture(after) && before!=after,"pointed vector contents retained");
        Fixture::Pointer(rewards.Row(1)+reward_header+4,reward_data+7);
        Require(!rewards.Capture(after),"misaligned vector span rejected");
        Fixture large_vector(gold);Fixture::Word(large_vector.Row(1)+reward_header+4,513*4);
        Fixture::Word(large_vector.Row(1)+reward_header+8,513*4);
        Require(!large_vector.Capture(after),"vector element count bound");
        Fixture list(gold);const size_t list_header=gold?0x4e8:0x4e0;
        auto* head=list.heads[3];auto* node=list.Allocate(12);
        Fixture::Pointer(head,node);Fixture::Pointer(head+4,node);Fixture::Pointer(node,head);Fixture::Pointer(node+4,head);
        Fixture::Word(list.Row(1)+list_header+4,1);Fixture::Word(node+8,7);
        Require(list.Capture(before),"valid native linked list");
        node[8]=8;Require(list.Capture(after) && after!=before,"pointed list value retained");
        Fixture::Pointer(node,node);Require(!list.Capture(after),"list cycle rejected");
        Fixture::Pointer(node,head);Fixture::Pointer(node+4,node);
        Require(!list.Capture(after),"list backlink rejected");
        Fixture::Pointer(node+4,head);Fixture::Word(list.Row(1)+list_header+4,2);
        Require(!list.Capture(after),"list count mismatch rejected");
        Fixture::Word(list.Row(1)+list_header+4,129);Require(!list.Capture(after),"list count bound");
        Fixture allies(gold);auto* last_ally=allies.Allies(21,26);
        Require(allies.Capture(before),"26 native ally links including repeated values retained");
        last_ally[8]=7;Require(allies.Capture(after) && before!=after,"last ally value retained beyond row count");
        Fixture bounded_allies(gold);bounded_allies.Allies(24,128);
        Require(bounded_allies.Capture(before),"bounded long ally list");
        Fixture::Word(bounded_allies.Row(24)+(gold?0x56c:0x554)+4,129);after=before;
        Require(!bounded_allies.Capture(after) && !CrossplaySetupCheckpoint::Nonzero(after),"ally link count bound clears digest");
        Fixture castle(gold);auto* design=castle.Row(1)+(gold?0x574:0x55c);
        castle.Vector(design+0x24,16);Require(!castle.Capture(after),"castle arrays must have equal counts");
        castle.Vector(design+(gold?0x34:0x30),4);castle.Vector(design+(gold?0x44:0x3c),4);
        auto* part=reinterpret_cast<unsigned char*>(CrossplayChat::Word(design+0x24));castle.Text(part,"Synthetic Part");
        Require(castle.Capture(before),"nested Castle Forge part string");
        auto* part_text=reinterpret_cast<unsigned char*>(CrossplayChat::Word(part));part_text[0]^=1;
        Require(castle.Capture(after) && after!=before,"nested part text mutation retained");
    }
    std::vector<unsigned char> bytes(MaximumDependencyBytes);
    const unsigned char one=1;
    Require(!Detail::Append(bytes,&one,1) && bytes.size()==MaximumDependencyBytes,"total dependency budget");
    Require(!Detail::Append(bytes,&one,SIZE_MAX),"overflow length rejected");
    CrossplayNative::ParticipantEvent binding;Snapshot result{};result.rows[0].bytes=17;
    Require(!Capture(static_cast<Crossplay::Edition>(0),binding,{},false,result) && !result.rows[0].bytes,"unsupported capture clears result");
    CrossplaySetupCheckpoint::Snapshot a{},b{};
    Require(!Detail::SameOwner(a,b),"empty owner snapshots never match");
    a.thread=b.thread=1;a.session=b.session=2;a.preparation=b.preparation=3;a.cache=b.cache=4;a.local[0]=b.local[0]=1;
    Require(Detail::SameOwner(a,b),"matching owned local snapshot");
    for(size_t field=0;field<5;++field) {
        auto changed=b;
        if(field==0)++changed.thread;else if(field==1)++changed.session;else if(field==2)++changed.preparation;
        else if(field==3)++changed.cache;else changed.local[0]^=1;
        Require(!Detail::SameOwner(a,changed),"owner or storage mutation rejected");
    }
    printf("PASS %u active-setup dependency checks; no original writer or live game executed.\n",checks);return 0;
}
