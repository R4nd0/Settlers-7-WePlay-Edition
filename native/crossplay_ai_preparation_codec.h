#pragma once
#include "crossplay_ai_preparation.h"
#include "crossplay_ai_preparation_source.h"
#include "crossplay_closed_preparation.h"
#include "crossplay_setup_signature_codec.h"

// Experimental native primitive, excluded from ordinary adapters. Apply only
// authenticated host records on the bound engine thread. Full original-root
// agreement and the mutual start barrier remain the caller's responsibility.
namespace CrossplayAIPreparation {
inline constexpr uint64_t ProfileCallbackHashes[2]={0xfe97dc182f737e89ull,0xb64bdb6b3adda5cull};
namespace Detail {
using CrossplayNativeCodec::Detail::Readable;
using CrossplayNative::HistoryRoleDetail::Writable;
inline bool Text(const unsigned char* at,std::vector<unsigned char>& bytes) {
 bytes.clear();if(!Readable(at,16))return false;
 const auto* p=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(at));
 const auto cap=CrossplayChat::Word(at+4),length=CrossplayChat::Word(at+8);
 if(!length || length>128 || cap<=length || !Readable(p,length+1) || p[length])return false;
 bytes.assign(p,p+length);return true;
}
inline bool Catalog(uint32_t profile,uint32_t team,const unsigned char* text,Crossplay::Digest& digest) {
 digest={};std::vector<unsigned char> bytes;if(!Text(text,bytes))return false;
 const auto n=bytes.size();bytes.resize(n+8);CrossplayChat::Put(bytes.data()+n,profile);CrossplayChat::Put(bytes.data()+n+4,team);
 return CrossplaySetupSignatureNative::Hash(bytes,digest);
}
inline bool Rows(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,bool host,
                 uint32_t target,unsigned char*& rows,CrossplayAIProfile::ContextSnapshot& context) {
 rows=nullptr;if(target<3 || target>4 || !CrossplayAIProfile::Context(edition,binding,ids,host,context))return false;
 CrossplayNative::LobbySnapshot lobby;
 if(edition==Crossplay::Edition::Gold) {
  // Same actual getter/writer owner and full getter guards as passive observation.
  CrossplayAIPreparationSource::Snapshot probe{};unsigned stage=0;
  if(!CrossplayAIPreparationSource::Capture(edition,probe,stage) || !probe.same)return false;
  const auto* manager=*reinterpret_cast<const unsigned char* const*>(0x10f834c);
  const auto* config=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(manager+((manager[4]-1)&1)*0xd4+0xa4));
  rows=reinterpret_cast<unsigned char*>(CrossplayChat::Word(config+4));
 } else if(!CrossplayNativeCodec::ReadyStateDetail::Preparation(edition,binding,ids,lobby,rows) || lobby.instance!=context.session)return false;
 const size_t size=edition==Crossplay::Edition::Gold?0x5c4:0x5a8;const auto* row=rows+target*size;
 return Readable(row,size) && CrossplayChat::Word(row+0x60)==1;
}
struct Pin {uintptr_t profile,team,difficulty,peaceful,slot,kind,name,text,callback;};
inline bool Layout(Crossplay::Edition edition,Pin& p) {
 if(edition==Crossplay::Edition::UbisoftHistory)p={0x546440,0x546470,0x5862a0,0x5862b0,0x5862e0,0x546480,0x546450,0x4e29f0,0x5494b0};
 else if(edition==Crossplay::Edition::SteamHistory)p={0x545a90,0x545ac0,0x585d60,0x585d70,0x585da0,0x545ad0,0x545aa0,0x4e2880,0x548b00};else return false;
 return true;
}
inline bool Methods(Crossplay::Edition edition,Pin& p) {
 if(!Layout(edition,p))return false;
 const unsigned char code[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x89,0x41,0,0x5d,0xc2,0x04,0};
 const uintptr_t funcs[]={p.profile,p.team,p.difficulty,p.slot,p.kind};const unsigned char offsets[]={0x18,0x30,0x6c,0x5c,0x64};
 for(unsigned i=0;i<5;++i) {
  unsigned char expected[sizeof(code)]{};memcpy(expected,code,sizeof(code));expected[8]=offsets[i];
  if(!Readable(reinterpret_cast<void*>(funcs[i]),sizeof(code)) || memcmp(reinterpret_cast<void*>(funcs[i]),expected,sizeof(code)))return false;
 }
 const unsigned char peaceful[]={0x55,0x8b,0xec,0x8a,0x45,0x08,0x88,0x41,0x70,0x5d,0xc2,0x04,0};
 if(!Readable(reinterpret_cast<void*>(p.peaceful),sizeof(peaceful)) || memcmp(reinterpret_cast<void*>(p.peaceful),peaceful,sizeof(peaceful)))return false;
 CrossplayClosedPreparation::Pin strings{};
 if(!CrossplayClosedPreparation::Layout(edition,strings) || !CrossplayClosedPreparation::Methods(edition,strings)
  || strings.name!=p.name || strings.text!=p.text)return false;
 // Complete original profile callback pins the catalog lookup/name/team layout.
 const unsigned index=static_cast<unsigned>(edition)-2;
 const uint64_t expected=ProfileCallbackHashes[index];
 const size_t length=0x17f;const auto* bytes=reinterpret_cast<const unsigned char*>(p.callback);
 if(!Readable(bytes,length))return false;uint64_t hash=14695981039346656037ull;
 for(size_t i=0;i<length;++i)hash=(hash^bytes[i])*1099511628211ull;
 return hash==expected;
}
inline bool Definition(Crossplay::Edition edition,uint32_t profile,const unsigned char*& globals,const unsigned char*& text,uint32_t& team) {
 globals=text=nullptr;team=0;if(profile<1 || profile>255)return false;
 const uintptr_t pointer=edition==Crossplay::Edition::SteamHistory?0x13a2304:0x13a5214;
 if(!Readable(reinterpret_cast<void*>(pointer),4))return false;globals=*reinterpret_cast<const unsigned char* const*>(pointer);
 if(!Readable(globals,0x188))return false;
 const auto* profiles=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(globals+0x184));
 if(!Readable(profiles,8))return false;const auto first=CrossplayChat::Word(profiles),last=CrossplayChat::Word(profiles+4);
 if(last<first || (last-first)%4 || last-first>256*4 || !Readable(reinterpret_cast<void*>(first),last-first))return false;
 bool found=false;for(auto at=first;at<last;at+=4) {
  const auto* value=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(reinterpret_cast<const unsigned char*>(at)));
  if(!Readable(value,4))return false;if(CrossplayChat::Word(value)==profile) {if(found)return false;found=true;}
 }
 if(!found)return false;const auto* db=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(globals+0x148));
 if(!Readable(db,0xa2c))return false;const auto start=CrossplayChat::Word(db+0xa28);
 const auto* definition=reinterpret_cast<const unsigned char*>(start+profile*0x98);
 if(start>UINT32_MAX-profile*0x98-0x98 || !Readable(definition,0x98))return false;
 text=definition+0x50;team=CrossplayChat::Word(definition+0x70);std::vector<unsigned char> name;return Text(text,name);
}
inline bool Preserved(const unsigned char* before,const unsigned char* after) {
 for(size_t i=0;i<0x5a8;++i) {
  if((i>=0x18 && i<0x2c) || (i>=0x30 && i<0x34) || (i>=0x5c && i<0x60)
   || (i>=0x64 && i<0x68) || (i>=0x6c && i<0x71))continue;
  if(before[i]!=after[i])return false;
 }return true;
}
}
inline bool Capture(const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,uint32_t slot,uint32_t expected,Value& out) {
 out={};unsigned char* rows=nullptr;CrossplayAIProfile::ContextSnapshot context{};
 if(!Detail::Rows(Crossplay::Edition::Gold,binding,ids,true,slot,rows,context))return false;
 const auto* row=rows+slot*0x5c4;
 Value value{slot,CrossplayChat::Word(row+0x18),CrossplayChat::Word(row+0x6c),row[0x70],{}};
 if(value.profile!=expected || CrossplayChat::Word(row+0x5c)!=slot || CrossplayChat::Word(row+0x64)!=2
  || !Detail::Catalog(value.profile,CrossplayChat::Word(row+0x30),row+0x1c,value.catalog) || !Valid(value))return false;
 CrossplayAIProfile::ContextSnapshot after{};unsigned char* next=nullptr;
 if(!Detail::Rows(Crossplay::Edition::Gold,binding,ids,true,slot,next,after) || next!=rows || !CrossplayAIProfile::Same(context,after))return false;
 out=value;return true;
}
inline bool Apply(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,const Value& value,unsigned& stage) {
 stage=1;Detail::Pin pin{};if(!Valid(value) || !Detail::Methods(edition,pin))return false;
 unsigned char* rows=nullptr;CrossplayAIProfile::ContextSnapshot context{};stage=2;
 if(!Detail::Rows(edition,binding,ids,false,value.slot,rows,context))return false;
 auto* row=rows+value.slot*0x5a8;
 if(CrossplayChat::Word(row+0x5c)!=UINT32_MAX || CrossplayChat::Word(row+0x64)!=1 || !Detail::Writable(row,0x5a8))return false;
 const unsigned char* globals=nullptr;const unsigned char* text=nullptr;uint32_t team=0;Crossplay::Digest digest{};stage=3;
 if(!Detail::Definition(edition,value.profile,globals,text,team) || !Detail::Catalog(value.profile,team,text,digest) || digest!=value.catalog)return false;
 const std::vector<unsigned char> before(rows,rows+25*0x5a8);std::vector<unsigned char> name;stage=4;
 if(!Detail::Text(text,name))return false;
 using Set=void (__thiscall*)(void*,uint32_t);
 reinterpret_cast<Set>(pin.kind)(row,2);reinterpret_cast<Set>(pin.profile)(row,value.profile);
 reinterpret_cast<void (__thiscall*)(void*,const void*)>(pin.name)(row,text);
 reinterpret_cast<Set>(pin.team)(row,team);reinterpret_cast<Set>(pin.slot)(row,value.slot);
 reinterpret_cast<Set>(pin.difficulty)(row,value.difficulty);
 reinterpret_cast<void (__thiscall*)(void*,bool)>(pin.peaceful)(row,value.peaceful!=0);
 stage=5;std::vector<unsigned char> observed;unsigned char* next=nullptr;CrossplayAIProfile::ContextSnapshot after{};
 const uintptr_t pointer=edition==Crossplay::Edition::SteamHistory?0x13a2304:0x13a5214;
 if(*reinterpret_cast<const unsigned char* const*>(pointer)!=globals || !Detail::Rows(edition,binding,ids,false,value.slot,next,after)
  || next!=rows || !CrossplayAIProfile::Same(context,after) || !Detail::Methods(edition,pin)
  || !Detail::Text(row+0x1c,observed) || observed!=name || CrossplayChat::Word(row+0x18)!=value.profile
  || CrossplayChat::Word(row+0x30)!=team || CrossplayChat::Word(row+0x5c)!=value.slot || CrossplayChat::Word(row+0x64)!=2
  || CrossplayChat::Word(row+0x6c)!=value.difficulty || row[0x70]!=value.peaceful)return false;
 for(unsigned i=0;i<25;++i) {
  if(i==value.slot) {if(!Detail::Preserved(before.data()+i*0x5a8,rows+i*0x5a8))return false;}
  else if(memcmp(before.data()+i*0x5a8,rows+i*0x5a8,0x5a8))return false;
 }
 stage=0;return true;
}
// Scoped original-root eligibility, bound to both actual AI records.
// It grants no world permission: native checkpoints, full roots, reader and mutual start receipts remain required.
inline bool Readback(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,bool host,const Value& value) {
 if(!Valid(value))return false;unsigned char* rows=nullptr;CrossplayAIProfile::ContextSnapshot context{};
 if(!Detail::Rows(edition,binding,ids,host,value.slot,rows,context))return false;
 const auto* row=rows+value.slot*(edition==Crossplay::Edition::Gold?0x5c4:0x5a8);Crossplay::Digest catalog{};
 if(CrossplayChat::Word(row+0x18)!=value.profile || CrossplayChat::Word(row+0x5c)!=value.slot || CrossplayChat::Word(row+0x64)!=2
  || CrossplayChat::Word(row+0x6c)!=value.difficulty || row[0x70]!=value.peaceful
  || !Detail::Catalog(value.profile,CrossplayChat::Word(row+0x30),row+0x1c,catalog) || catalog!=value.catalog)return false;
 CrossplayAIProfile::CacheLock lock(edition,reinterpret_cast<unsigned char*>(context.cache));if(!lock.acquired)return false;
 const auto* cpu=reinterpret_cast<const unsigned char*>(context.slots)+(value.slot-1)*0x18;
 return CrossplayChat::Word(cpu)==4 && cpu[4]==1 && CrossplayChat::Word(cpu+8)==value.slot && !cpu[12]
  && !CrossplayChat::Word(cpu+16) && !CrossplayChat::Word(cpu+20);
}
class PreparationScope;
inline thread_local const PreparationScope* active_read_scope=nullptr;
class PreparationScope {
 Crossplay::Edition edition{};CrossplaySessionConfig::Connections ids{};bool host=false;DWORD owner=0;
 std::array<Value,2> values{};const CrossplayNative::ParticipantEvent* binding=nullptr;
 bool valid=false;
public:
 PreparationScope(Crossplay::Edition e,const CrossplayNative::ParticipantEvent& b,CrossplaySessionConfig::Connections i,bool h,const std::array<Value,2>& v)
  :edition(e),ids(i),host(h),owner(GetCurrentThreadId()),values(v),binding(&b) {
  if(active_read_scope || values[0].slot!=3 || values[1].slot!=4 || values[0].profile==values[1].profile
   || !Readback(e,b,i,h,values[0]) || !Readback(e,b,i,h,values[1]))return;
  valid=true;active_read_scope=this;
 }
 PreparationScope(const PreparationScope&)=delete;PreparationScope& operator=(const PreparationScope&)=delete;
 ~PreparationScope(){if(active_read_scope==this)active_read_scope=nullptr;}
 bool Matches(Crossplay::Edition e,const CrossplayNative::ParticipantEvent& b,CrossplaySessionConfig::Connections i,bool h) const {
  return valid && owner==GetCurrentThreadId() && edition==e && binding==&b && ids.host==i.host && ids.guest==i.guest && host==h
   && Readback(e,b,i,h,values[0]) && Readback(e,b,i,h,values[1]);
 }
};
inline bool EligibleScope(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,CrossplaySessionConfig::Connections ids,bool host) {
 return active_read_scope && active_read_scope->Matches(edition,binding,ids,host);
}

}
