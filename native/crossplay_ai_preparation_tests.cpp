#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_ai_preparation_codec.h"
#include <cstdio>
#include <cstdlib>
unsigned checks=0;
void Check(bool okay){++checks;if(!okay){printf("FAIL AI preparation %u\n",checks);exit(1);}}
int main() {
 using namespace CrossplayAIPreparation;
 CrossplayNative::ParticipantEvent unbound;
 Check(!EligibleScope(Crossplay::Edition::Gold,unbound,{42,77},true));
 {
  std::array<Value,2> invalid{};
  PreparationScope scope(Crossplay::Edition::Gold,unbound,{42,77},true,invalid);
  Check(!EligibleScope(Crossplay::Edition::Gold,unbound,{42,77},true));
 }
 Check(!active_read_scope);
 Crossplay::Peer host{},guest{};host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
 Value v{3,29,2,0,{}};v.catalog[0]=1;Value result{};std::vector<unsigned char> frame;
 Check(Frame(host,1,v,frame) && frame.size()==92 && Receive(guest,0,frame,result) && result.profile==29);
 for(size_t i=0;i<60;++i){auto bad=frame;bad[i]^=0x80;
  if(i>=48 && i<52 || i>=52 && i<56 || i>=56 && i<60)continue;
  Check(!Receive(guest,0,bad,result));}
 Check(!Receive(guest,1,frame,result));Check(!Receive(host,0,frame,result));
 for(size_t n=0;n<92;++n){auto bad=frame;bad.resize(n);Check(!Receive(guest,0,bad,result));}
 auto bad=frame;bad.push_back(0);Check(!Receive(guest,0,bad,result));
 for(uint32_t field:{44u,48u,52u,56u})for(uint32_t n:{256u,UINT32_MAX}){
  bad=frame;CrossplayChat::Put(bad.data()+field,n);Check(!Receive(guest,0,bad,result));}
 bad=frame;memset(bad.data()+60,0,32);Check(!Receive(guest,0,bad,result));
 v.slot=4;v.profile=24;Check(Frame(host,2,v,frame) && Receive(guest,1,frame,result));Check(!Frame(host,1,v,frame));
 unsigned char before[0x5a8]{},after[0x5a8]{};
 for(size_t i=0;i<sizeof(before);++i){memset(after,0,sizeof(after));after[i]=1;
  const bool allowed=(i>=0x18 && i<0x2c) || (i>=0x30 && i<0x34) || (i>=0x5c && i<0x60)
   || (i>=0x64 && i<0x68) || (i>=0x6c && i<0x71);
  Check(Detail::Preserved(before,after)==allowed);}
 printf("PASS %u AI preparation framing/namespace/order/field-boundary/preservation checks; synthetic only.\n",checks);
}
