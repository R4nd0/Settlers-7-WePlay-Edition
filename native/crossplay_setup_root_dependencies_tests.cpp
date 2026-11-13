#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_setup_root_capture.h"
#include <memory>
#include <stdio.h>
namespace {
unsigned checks=0;
void Require(bool okay,const char* reason){++checks;if(!okay){fprintf(stderr,"FAIL %s\n",reason);exit(1);}}
struct Fixture {
    bool gold;
    std::array<unsigned char,0xa0> root{};
    std::array<unsigned char,0x1ac> profile{};
    std::vector<std::pair<std::unique_ptr<unsigned char[]>,size_t>> owned;
    unsigned char *victory=nullptr,*sector=nullptr,*catalogue=nullptr,*sector_catalogue=nullptr;
    unsigned char *registry=nullptr,*array=nullptr,*buckets=nullptr,*hash_table=nullptr,*placements=nullptr;
    unsigned char* Allocate(size_t size){auto p=std::make_unique<unsigned char[]>(size);auto* at=p.get();owned.emplace_back(std::move(p),size);return at;}
    static void Put(unsigned char* at,uint32_t value){CrossplayChat::Put(at,value);}
    static void Pointer(unsigned char* at,const void* value){Put(at,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));}
    void Text(unsigned char* at,const char* value) {
        const size_t size=strlen(value);auto* bytes=Allocate(size+1);memcpy(bytes,value,size+1);
        Pointer(at,bytes);Put(at+4,static_cast<uint32_t>(size+1));Put(at+8,static_cast<uint32_t>(size));
    }
    static void Vector(unsigned char* at,unsigned char* data,size_t bytes){Pointer(at,data);Pointer(at+4,data+bytes);Pointer(at+8,data+bytes);}
    explicit Fixture(bool value):gold(value) {
        victory=Allocate(0x44);Text(victory,"Rule");Put(victory+0x40,7);
        Vector(root.data()+(gold?0x38:0x34),victory,0x44);
        catalogue=Allocate(gold?0xb08:0xa7c);Pointer(profile.data()+(gold?0x188:0x148),catalogue);
        registry=Allocate(16);array=Allocate(8);auto* name=Allocate(16);Text(name,"Rule");Pointer(array+4,name);
        Pointer(registry,array);Put(registry+8,2);Put(registry+12,2);
        auto* table=catalogue+(gold?0xac0:0xa38);Pointer(table+0x18,registry);
        hash_table=Allocate(8);buckets=Allocate(32);Pointer(hash_table,buckets);Put(hash_table+4,3);Pointer(table+0x1c,hash_table);
        Put(buckets,1);Put(buckets+4,7);
        Pointer(catalogue+(gold?0xb04:0xa78),Allocate(2*(gold?0x98:0xa0)));
        sector=Allocate(gold?0x18:0x14);Put(sector+4,1);placements=Allocate(8);Put(placements,1);Put(placements+4,7);
        Vector(sector+(gold?0xc:8),placements,8);Vector(root.data()+(gold?0x58:0x4c),sector,gold?0x18:0x14);
        sector_catalogue=Allocate(gold?0x20:0x24);Pointer(profile.data()+(gold?0x1a8:0x168),sector_catalogue);
        Pointer(sector_catalogue+(gold?0x1c:0x20),registry);
    }
    bool Capture(Crossplay::Digest& digest){return CrossplaySetupRootDependencies::Capture(root.data(),profile.data(),gold,digest);}
    std::vector<unsigned char> State() const {
        std::vector<unsigned char> bytes(root.begin(),root.end());bytes.insert(bytes.end(),profile.begin(),profile.end());
        for(const auto& value:owned)bytes.insert(bytes.end(),value.first.get(),value.first.get()+value.second);
        return bytes;
    }
};
}
int main() {
    for(bool gold:{false,true}) {
        Crossplay::Digest before{},after{};
        std::array<unsigned char,0xa0> empty{};
        Require(CrossplaySetupRootDependencies::Capture(empty.data(),nullptr,gold,before),"empty rules do not dereference unused globals");
        Fixture valid(gold);const auto state=valid.State();
        Require(valid.Capture(before) && CrossplaySetupCheckpoint::Nonzero(before),"bounded populated rule dependencies");
        Require(valid.Capture(after) && before==after && valid.State()==state,"read-only stable dependency fingerprint");
        Fixture::Put(valid.placements+4,8);Require(valid.Capture(after) && before!=after,"nested sector quantity retained");
        Fixture mask(gold);Fixture::Put(mask.hash_table+4,2);Require(!mask.Capture(after),"non-power-of-two hash table rejected");
        Fixture full(gold);for(unsigned i=0;i<4;++i)Fixture::Put(full.buckets+8*i,1);
        Require(!full.Capture(after),"full probing table rejected before original lookup");
        Fixture foreign(gold);Fixture::Put(foreign.buckets,2);Require(!foreign.Capture(after),"hash bucket outside registry rejected");
        Fixture hole(gold);Fixture::Pointer(hole.array+4,nullptr);Require(!hole.Capture(after),"assigned bucket cannot dereference a null entry");
        Fixture missing(gold);Fixture::Pointer(missing.sector_catalogue+(gold?0x1c:0x20),nullptr);
        Require(!missing.Capture(after),"independent Gold/History sector registry offsets");
        Fixture count(gold);Fixture::Put(count.registry+8,3);Require(!count.Capture(after),"registry count beyond capacity");
        Fixture capacity(gold);Fixture::Put(capacity.registry+12,4097);Require(!capacity.Capture(after),"registry capacity bound");
        Fixture text(gold);Fixture::Put(text.victory+8,513);Require(!text.Capture(after),"rule string bound");
        Fixture overflow(gold);Fixture::Put(overflow.root.data()+(gold?0x3c:0x38),CrossplayChat::Word(overflow.root.data()+(gold?0x38:0x34))+129*0x44);
        Require(!overflow.Capture(after),"victory vector bound and capacity");
        Fixture fallback(gold);Fixture::Put(fallback.sector+4,UINT32_MAX);Fixture::Put(fallback.placements,UINT32_MAX);
        Require(fallback.Capture(after),"original out-of-range sector fallback remains supported");
        Fixture sparse(gold);memset(sparse.root.data()+(gold?0x38:0x34),0,12);Fixture::Pointer(sparse.array+4,nullptr);
        Fixture::Put(sparse.sector+4,0);Fixture::Put(sparse.placements,0);
        Require(sparse.Capture(after),"unused sparse registry entry retained without dereference");
        Fixture::Put(sparse.placements,1);Require(!sparse.Capture(after),"used null sector entry rejected");
        Require(!CrossplaySetupRootDependencies::Capture(valid.root.data(),reinterpret_cast<const unsigned char*>(1),gold,after),"unreadable profile rejected");
        after[0]=1;Require(!CrossplaySetupRootDependencies::Capture(reinterpret_cast<const unsigned char*>(1),nullptr,gold,after)
            && !CrossplaySetupCheckpoint::Nonzero(after),"unreadable root clears output");
    }
    CrossplaySetupRootDependencies::Detail::Inventory bounded;
    bounded.bytes.resize(CrossplaySetupRootDependencies::MaximumBytes);
    Require(!bounded.Append(reinterpret_cast<const void*>(1),1),"dependency budget checked before memory access");
    CrossplayNative::ParticipantEvent binding;CrossplaySetupRootCapture::Snapshot result{};
    for(uint32_t version:{0u,3u,8u,UINT32_MAX}) {
        result.format_version=1;result.complete.bytes=7;
        Require(!CrossplaySetupRootCapture::Capture(Crossplay::Edition::Gold,binding,{},true,result,version)
            && !result.format_version && !result.complete.bytes,"unsupported enclosing format clears output before native calls");
    }
    printf("PASS %u enclosing-rule dependency checks; no original writer, game or world start executed.\n",checks);
}
