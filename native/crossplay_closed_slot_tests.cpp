#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_closed_slot_codec.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::printf("FAIL closed slot %u\n",checks);std::exit(1);}}
}
extern "C" __declspec(dllexport) bool __cdecl WePlayClosedSlotMethodsOracle(unsigned edition){return CrossplayClosedSlot::Methods(static_cast<Crossplay::Edition>(edition));}
int main(){
    Crossplay::Peer source{},target{};source.local_id.fill(17);source.remote_id.fill(34);target.local_id=source.remote_id;target.remote_id=source.local_id;
    {
        namespace C=CrossplayClosedSlot;
        std::vector<unsigned char> closed_event(17),encoded,output;
        CrossplayChat::Put(closed_event.data(),C::Class);CrossplayChat::Put(closed_event.data()+4,C::Type);CrossplayChat::Put(closed_event.data()+8,42);closed_event[12]=1;
        for(uint32_t candidate=0;candidate<9;++candidate) {
            CrossplayChat::Put(closed_event.data()+13,candidate);uint32_t parsed=99;
            Check(C::Wire(closed_event,42,parsed)==(candidate==3 || candidate==4));
            Check(parsed==((candidate==3 || candidate==4)?candidate:0));
        }
        CrossplayChat::Put(closed_event.data()+13,3);
        Check(C::Frame(source,1,closed_event,42,encoded) && C::Receive(target,0,42,7,encoded,output));
        uint32_t parsed=0;Check(C::Wire(output,7,parsed) && parsed==3);
        Check(!C::Receive(target,1,42,7,encoded,output) && output.empty());
        for(size_t i=0;i<encoded.size();++i){auto changed=encoded;changed[i]^=0x80;Check(!C::Receive(target,0,42,7,changed,output) && output.empty());}
        for(size_t i=0;i<encoded.size();++i)Check(!C::Receive(target,0,42,7,std::vector<unsigned char>(encoded.begin(),encoded.begin()+i),output));
        Check(!C::Frame(source,0,closed_event,42,output) && output.empty());
        Check(!C::Frame(source,2,closed_event,42,output) && output.empty());
        CrossplayChat::Put(closed_event.data()+13,4);Check(C::Frame(source,2,closed_event,42,encoded));
        Check(C::Receive(target,1,42,7,encoded,output) && C::Wire(output,7,parsed) && parsed==4);
        Check(!C::Receive(target,0,42,7,encoded,output) && output.empty());
        Check(!C::Receive(target,1,UINT32_MAX,7,encoded,output));
        Check(!C::Receive(target,1,42,UINT32_MAX,encoded,output));
        Check(!C::Methods(static_cast<Crossplay::Edition>(0)) && !C::Methods(static_cast<Crossplay::Edition>(4)));
    }
    std::printf("PASS %u closed-slot framing, ordering and exclusion checks; no game started.\n",checks);
}
