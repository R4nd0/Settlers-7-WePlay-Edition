#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Bounded metadata view of the original version-8 saved player prefix. This is
// the regular binary serializer (node versions and Coll/coll), not the network
// NoOverhead serializer. Never constructs objects or executes save-supplied Lua.
// The caller separately binds the complete archive/file hash and native loader.
namespace CrossplaySavedPlayers {
struct Row {uint32_t profile=0,index=0,controller=0,kind=0,difficulty=0;unsigned peaceful=0;};
struct Snapshot {std::array<Row,25> rows{};size_t bytes=0;uint32_t seed=0;};
namespace Detail {
constexpr size_t MaximumPrefix=1024*1024;
struct Reader {
    const std::vector<unsigned char>& input;size_t at=0;bool okay=true;
    const unsigned char* Take(size_t n){
        if(!okay || at>input.size() || n>input.size()-at || at>MaximumPrefix || n>MaximumPrefix-at){okay=false;return nullptr;}
        const auto* p=input.data()+at;at+=n;return p;
    }
    void Check(bool value){okay=okay&&value;}
    uint32_t Number(){const auto* p=Take(4);return p?uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24:0;}
    unsigned Byte(){const auto* p=Take(1);return p?*p:0;}
    unsigned Boolean(){const auto value=Byte();Check(value<=1);return value;}
    void Tag(const char* text){const auto* p=Take(4);Check(p && !memcmp(p,text,4));}
    void String(){
        Tag("Str_");const auto n=Number();Check(n<=65536);const auto* p=Take(n);
        if(p && n)Check(p[n-1]==0 && !memchr(p,0,n-1));
    }
    void Symbol(){const auto n=Byte();Check(n!=0);const auto* p=Take(n);if(p)for(unsigned i=0;i<n;++i)Check(p[i]>=32 && p[i]<127);}
    void Node(){Check(Number()==1);}
    void PairNode(){Node();Take(8);}
    uint32_t Collection(uint32_t maximum=65536){
        Tag("Coll");Check(Number()==0);const auto n=Number();Check(n<=maximum);return okay?n:0;
    }
    void End(){Tag("coll");}
    uint32_t Scalars(unsigned width,uint32_t maximum=65536){const auto n=Collection(maximum);Take(size_t(n)*width);End();return n;}
    void Bits(size_t n){const auto* p=Take(n);if(p)for(size_t i=0;i<n;++i)Check(p[i]<=1);}
    Row Player(unsigned index){
        Row row;Check(Number()==8);Take(4);row.profile=Number();String();Symbol();Boolean();
        Take(7*4);Boolean();const auto slot=Byte();row.index=Number();
        Check((slot==index && row.index==index) || (slot==255 && row.index==UINT32_MAX));
        Take(4);PairNode();
        const auto trades=Collection(4096);for(uint32_t i=0;i<trades;++i){Node();PairNode();Take(4);}End();
        PairNode();Take(4);PairNode();Take(8);row.controller=Number();row.kind=Number();
        Check(row.controller<=3 && row.kind<=8);Scalars(4);Bits(512);Bits(256);Scalars(4);Scalars(4);
        Take(6*4);Boolean();Boolean();
        const auto flags=Collection(25);for(uint32_t i=0;i<flags;++i)Boolean();End();Boolean();
        const auto allies=Collection(4096);for(uint32_t i=0;i<allies;++i)Check(Byte()<25);End();Check(Byte()<25);Take(4);
        Node();String();String();
        const auto parts=Collection(1024);for(uint32_t i=0;i<parts;++i)String();End();
        const auto parents=Scalars(4,1024),slots=Scalars(4,1024);Check(parts==parents && parts==slots);
        row.difficulty=Number();Check(row.difficulty<=2);row.peaceful=Boolean();return row;
    }
};
}
inline bool Read(const std::vector<unsigned char>& input,Snapshot& output){
    output={};Detail::Reader r{input};r.Tag("bin1");r.Check(r.Number()==1);r.Check(r.Number()==0);
    r.Check(r.Number()==8);r.Check(r.Number()==1);
    // Original Random/Seed node, not a class ID or a constant checkpoint.
    // Gold's original saved reader at 0x8acd25..0x8acd58 reads this uint32.
    const auto seed=r.Number();r.Check(r.Collection(25)==25);Snapshot result;result.seed=seed;
    for(unsigned i=0;i<25 && r.okay;++i)result.rows[i]=r.Player(i);
    r.End();if(!r.okay)return false;result.bytes=r.at;output=result;return true;
}
}
