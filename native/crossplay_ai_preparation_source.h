#pragma once
#include "crossplay_ai_profile_codec.h"
#include <array>
// Read-only, fully guarded original getter/writer preparation views.
// No event, state write or world permission is supplied.
namespace CrossplayAIPreparationSource {
struct Region {uintptr_t at;unsigned bytes;uint64_t hash;};
inline constexpr Region Getters[3][3]={
 {{0x5b2d50,126,0x35b8936d2fa26b3dull},{0x5b2830,108,0x85b435f40d902a7ull},{0x5b2950,108,0x3a95e7130a546045ull}},
 {{0x5473a0,162,0xef904f50ffb72976ull},{0x546cc0,166,0xf24a9753a7063331ull},{0x546e90,152,0x3372386830fee125ull}},
 {{0x5469f0,162,0x4e01d71d0cbc7a6full},{0x546310,166,0xc74c8b64910edbull},{0x5464e0,152,0xf0e429b172aabeaaull}},
};
struct Row {uint32_t profile=0,slot=0,controller=0,kind=0,difficulty=0,peaceful=0;};
struct Snapshot {std::array<Row,25> getter{},writer{};bool same=false;};
inline bool Capture(Crossplay::Edition edition,Snapshot& output,unsigned& stage) {
 using namespace CrossplayNativeCodec;output={};stage=1;
 const unsigned index=static_cast<unsigned>(edition)-1;if(index>=3)return false;
 for(const auto& p:Getters[index]) {
  const auto* bytes=reinterpret_cast<const unsigned char*>(p.at);if(!Detail::Readable(bytes,p.bytes))return false;
  uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<p.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
  if(hash!=p.hash)return false;
 }
 const bool gold=index==0,steam=index==2;
 const uintptr_t pointer=gold?0x10f834c:steam?0x13a2300:0x13a5218;
 const uintptr_t type=gold?0xf51af0:steam?0x113939c:0x113ae54;
 const size_t stride=gold?0xd4:0xc4,row_size=gold?0x5c4:0x5a8;
 stage=2;if(!Detail::Readable(reinterpret_cast<void*>(pointer),4))return false;
 const auto* manager=*reinterpret_cast<const unsigned char* const*>(pointer);
 if(!Detail::Readable(manager,8+2*stride) || CrossplayChat::Word(manager)!=type || manager[4]>1)return false;
 const unsigned flag=manager[4];const auto* buffer=manager+((flag-1)&1)*stride;
 const auto* getter=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(buffer+(gold?0xa0:0x94)));
 const auto* writer=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(buffer+(gold?0xa4:0x98)));
 stage=3;if(!Detail::Readable(getter,4) || !Detail::Readable(writer,8))return false;
 const auto* getrows=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(getter));
 const auto* writerrows=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(writer+4));
 if(CrossplayChat::Word(writer)!=reinterpret_cast<uintptr_t>(writerrows))return false;
 stage=4;const size_t count_at=gold?0x9078:0x8db8;
 if(!Detail::Readable(getrows,count_at+1) || !Detail::Readable(writerrows,count_at+1)
  || getrows[count_at]!=4 || writerrows[count_at]!=4)return false;
 Snapshot result{};result.same=getrows==writerrows;
 for(unsigned group=0;group<2;++group)for(unsigned i=0;i<25;++i) {
  const auto* data=(group?writerrows:getrows)+i*row_size;
  Row value{CrossplayChat::Word(data+0x18),CrossplayChat::Word(data+0x5c),CrossplayChat::Word(data+0x60),
   CrossplayChat::Word(data+0x64),CrossplayChat::Word(data+0x6c),static_cast<uint32_t>(data[0x70])};
  (group?result.writer:result.getter)[i]=value;
 }
 stage=5;
 if(*reinterpret_cast<const unsigned char* const*>(pointer)!=manager || manager[4]!=flag
  || CrossplayChat::Word(buffer+(gold?0xa0:0x94))!=reinterpret_cast<uintptr_t>(getter)
  || CrossplayChat::Word(buffer+(gold?0xa4:0x98))!=reinterpret_cast<uintptr_t>(writer))return false;
 output=result;stage=0;return true;
}
}
