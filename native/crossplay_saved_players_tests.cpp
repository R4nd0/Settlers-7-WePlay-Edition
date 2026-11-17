#include "crossplay_saved_players.h"
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
unsigned checks=0;void Check(bool value){++checks;if(!value){printf("FAIL saved players %u\n",checks);exit(1);}}
using Bytes=std::vector<unsigned char>;
void Number(Bytes& b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(static_cast<unsigned char>(n>>(i*8)));}
void Tag(Bytes& b,const char* s){b.insert(b.end(),s,s+4);}
void String(Bytes& b,const std::string& s){Tag(b,"Str_");Number(b,s.empty()?0:static_cast<uint32_t>(s.size()+1));b.insert(b.end(),s.begin(),s.end());if(!s.empty())b.push_back(0);}
void Begin(Bytes& b,unsigned n=0){Tag(b,"Coll");Number(b,0);Number(b,n);}
void End(Bytes& b){Tag(b,"coll");}
void Empty(Bytes& b){Begin(b);End(b);}
void Pair(Bytes& b){Number(b,1);Number(b,0);Number(b,0);}
struct Fixture {Bytes bytes;std::array<size_t,25> rows{},states{},types{},difficulties{},peaceful{},allies{};};
Fixture Saved(){
 Fixture f;auto& b=f.bytes;Tag(b,"bin1");for(auto n:{1u,0u,8u,1u,0xe85752e9u})Number(b,n);Begin(b,25);
 for(unsigned i=0;i<25;++i){
  f.rows[i]=b.size();Number(b,8);Number(b,0);Number(b,i==3?29:i==4?24:i);
  String(b,"");b.push_back(3);b.insert(b.end(),{'R','e','d'});b.push_back(0);
  for(unsigned j=0;j<7;++j)Number(b,0);
  b.push_back(i>=1 && i<=4?1:0);b.push_back(i>=1 && i<=4?static_cast<unsigned char>(i):255);
  Number(b,i>=1 && i<=4?i:UINT32_MAX);Number(b,0);Pair(b);Empty(b);Pair(b);Number(b,0);Pair(b);Number(b,0);Number(b,0);
  f.states[i]=b.size();Number(b,i==1 || i==2?3:i==3 || i==4?1:0);f.types[i]=b.size();Number(b,i==1 || i==2?1:i==3 || i==4?2:i>=21?8:4);
  Empty(b);b.insert(b.end(),768,0);Empty(b);Empty(b);for(unsigned j=0;j<6;++j)Number(b,0);
  b.push_back(0);b.push_back(0);Empty(b);b.push_back(0);
  f.allies[i]=b.size();Begin(b,i>=21?26:0);if(i>=21)for(unsigned j=0;j<26;++j)b.push_back(static_cast<unsigned char>(j%25));End(b);
  b.push_back(0);Number(b,0);Number(b,1);String(b,"");String(b,"");Empty(b);Empty(b);Empty(b);
  f.difficulties[i]=b.size();Number(b,i==3?0:i==4?2:1);f.peaceful[i]=b.size();b.push_back(i==4?1:0);
 }
 End(b);return f;
}
void Put(Bytes& b,size_t at,uint32_t n){for(unsigned i=0;i<4;++i)b[at+i]=static_cast<unsigned char>(n>>(i*8));}
}
int main(){
 const auto f=Saved();CrossplaySavedPlayers::Snapshot result;
 Check(CrossplaySavedPlayers::Read(f.bytes,result));Check(result.bytes==f.bytes.size());
 Check(result.rows[3].profile==29 && result.rows[3].kind==2 && result.rows[3].controller==1 && result.rows[3].difficulty==0);
 Check(result.rows[4].profile==24 && result.rows[4].index==4 && result.rows[4].difficulty==2 && result.rows[4].peaceful==1);
 for(unsigned i=21;i<25;++i)Check(result.rows[i].kind==8 && !result.rows[i].controller && result.rows[i].index==UINT32_MAX);
 for(size_t n=0;n<f.bytes.size();++n){Bytes cut(f.bytes.begin(),f.bytes.begin()+n);Check(!CrossplaySavedPlayers::Read(cut,result) && !result.bytes);}
 for(size_t at:{size_t(0),size_t(4),size_t(12),size_t(24),size_t(28),size_t(32),f.rows[3],f.allies[21]}){
  auto bad=f.bytes;Put(bad,at,UINT32_MAX);Check(!CrossplaySavedPlayers::Read(bad,result));
 }
 auto bad=f.bytes;Put(bad,f.allies[21]+8,4097);Check(!CrossplaySavedPlayers::Read(bad,result));
 bad=f.bytes;Put(bad,f.difficulties[3],3);Check(!CrossplaySavedPlayers::Read(bad,result));
 bad=f.bytes;bad[f.peaceful[4]]=2;Check(!CrossplaySavedPlayers::Read(bad,result));
 bad=f.bytes;Put(bad,f.states[3],4);Check(!CrossplaySavedPlayers::Read(bad,result));
 bad=f.bytes;Put(bad,f.types[3],9);Check(!CrossplaySavedPlayers::Read(bad,result));
 auto extended=f.bytes;extended.insert(extended.end(),128,0x5a);Check(CrossplaySavedPlayers::Read(extended,result) && result.bytes==f.bytes.size());
 for(auto seed:{0u,1u,0x3a1a96b8u,UINT32_MAX}){auto seeded=f.bytes;Put(seeded,20,seed);Check(CrossplaySavedPlayers::Read(seeded,result) && result.seed==seed);}
 printf("PASS %u saved-player prefix checks: regular serializer, 25 rows, special actors, long allies, all truncations and malformed fields. Authored metadata only; no game world permission.\n",checks);
}
