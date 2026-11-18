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
struct Row {uint32_t profile=0,slot=0,controller=0,kind=0,difficulty=0,peaceful=0,team=0;};
struct Snapshot {std::array<Row,25> getter{},writer{};bool same=false;};
struct View {
 uintptr_t pointer=0;
 const unsigned char* manager=nullptr;
 const unsigned char* buffer=nullptr;
 const unsigned char* getter=nullptr;
 const unsigned char* writer=nullptr;
 const unsigned char* getrows=nullptr;
 unsigned char* rows=nullptr;
 size_t getter_offset=0,writer_offset=0,row_size=0;
 unsigned mode=0;
 bool Stable() const {
  using CrossplayNativeCodec::Detail::Readable;
  return Readable(reinterpret_cast<const void*>(pointer),4) && Readable(manager,5)
   && *reinterpret_cast<const unsigned char* const*>(pointer)==manager && manager[4]==mode
   && Readable(buffer,writer_offset+4) && Readable(getter,4) && Readable(writer,8)
   && CrossplayChat::Word(buffer+getter_offset)==reinterpret_cast<uintptr_t>(getter)
   && CrossplayChat::Word(buffer+writer_offset)==reinterpret_cast<uintptr_t>(writer)
   && CrossplayChat::Word(getter)==reinterpret_cast<uintptr_t>(getrows)
   && CrossplayChat::Word(writer)==reinterpret_cast<uintptr_t>(rows)
   && CrossplayChat::Word(writer+4)==reinterpret_cast<uintptr_t>(rows);
 }
};
inline bool CaptureView(Crossplay::Edition edition,unsigned capacity,View& output,unsigned& stage) {
 using namespace CrossplayNativeCodec;output={};stage=1;
 const unsigned index=static_cast<unsigned>(edition)-1;
 if(index>=3 || capacity<2 || capacity>CrossplaySessionConfig::Slots)return false;
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
 auto* writerrows=reinterpret_cast<unsigned char*>(CrossplayChat::Word(writer+4));
 if(CrossplayChat::Word(writer)!=reinterpret_cast<uintptr_t>(writerrows))return false;
 stage=4;const size_t count_at=gold?0x9078:0x8db8;
 if(!Detail::Readable(getrows,count_at+1) || !Detail::Readable(writerrows,count_at+1)
  || getrows[count_at]!=capacity || writerrows[count_at]!=capacity)return false;
 View result{pointer,manager,buffer,getter,writer,getrows,writerrows,
  gold?0xa0u:0x94u,gold?0xa4u:0x98u,row_size,flag};
 stage=5;if(!result.Stable())return false;
 output=result;stage=0;return true;
}
inline Row ReadRow(const View& view,unsigned slot,bool writer) {
 const auto* data=(writer?view.rows:view.getrows)+slot*view.row_size;
 return {CrossplayChat::Word(data+0x18),CrossplayChat::Word(data+0x5c),CrossplayChat::Word(data+0x60),
  CrossplayChat::Word(data+0x64),CrossplayChat::Word(data+0x6c),data[0x70],data[0x68]};
}
inline bool Capture(Crossplay::Edition edition,Snapshot& output,unsigned& stage) {
 output={};View view;
 if(!CaptureView(edition,4,view,stage))return false;
 Snapshot result{};result.same=view.getrows==view.rows;
 for(unsigned group=0;group<2;++group)for(unsigned i=0;i<25;++i) {
  (group?result.writer:result.getter)[i]=ReadRow(view,i,group!=0);
 }
 stage=5;
 if(!view.Stable())return false;
 output=result;stage=0;return true;
}
}
