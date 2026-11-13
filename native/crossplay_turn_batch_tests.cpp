#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_turn_batch.h"
#include <stdio.h>
#include <stdlib.h>
namespace {
void Check(bool okay,const char* message){if(!okay){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
std::vector<unsigned char> Wire(unsigned count) {
    using namespace CrossplayTurnBatch;
    std::vector<unsigned char> value(BaseWire+count*ItemWire,0);
    CrossplayChat::Put(value.data(),Class);CrossplayChat::Put(value.data()+4,Type);CrossplayChat::Put(value.data()+8,42);
    value[12]=1;CrossplayChat::Put(value.data()+13,UINT32_MAX);CrossplayChat::Put(value.data()+17,count);
    for(unsigned i=0;i<count;++i) {
        auto* item=value.data()+BaseWire+i*ItemWire;CrossplayChat::Put(item,NestedType);CrossplayChat::Put(item+4,42);item[8]=static_cast<unsigned char>(i%2);
        for(unsigned j=0;j<5;++j)CrossplayChat::Put(item+9+j*4,0x80000000u+i*5+j);
    }
    return value;
}
}
int main() {
    using namespace CrossplayTurnBatch;
    Crossplay::Peer host{},guest{};host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
    for(unsigned count:{0u,1u,2u,64u}) {
        const auto original=Wire(count);std::vector<unsigned char> frame,wire;
        Check(Encode(host,1,original,42,frame) && Decode(guest,0,frame,42,0,wire),"bounded original batch round trip");
        auto expected=original;CrossplayChat::Put(expected.data()+8,0);
        for(unsigned i=0;i<count;++i)CrossplayChat::Put(expected.data()+BaseWire+i*ItemWire+4,0);
        Check(wire==expected,"only outer/nested admitted CIDs change; all turn and checksum bits preserved");
        Check(!Decode(guest,1,frame,42,0,wire) && wire.empty(),"batch replay rejected");
        for(size_t i=0;i<Header;++i){auto bad=frame;bad[i]^=1;Check(!Decode(guest,0,bad,42,0,wire),"every authenticated identity/sequence/length header byte bound");}
        for(size_t length=0;length<original.size();++length) {
            const std::vector<unsigned char> short_wire(original.begin(),original.begin()+length);
            Check(!Inspect(short_wire,42),"every truncated batch rejected");
        }
        auto trailing=original;trailing.push_back(0);Check(!Inspect(trailing,42),"trailing batch rejected");
    }
    std::vector<unsigned char> frame,decoded;auto wire=Wire(1);
    Check(!Encode(host,0,wire,42,frame),"zero sequence rejected");
    Check(Encode(host,UINT32_MAX,wire,42,frame) && Decode(guest,UINT32_MAX-1,frame,42,0,decoded),"last sequence accepted");
    Check(!Decode(guest,UINT32_MAX,frame,42,0,decoded),"sequence cannot wrap");
    auto bad=wire;CrossplayChat::Put(bad.data()+17,65);Check(!Inspect(bad,42),"bounded nested count");
    bad=wire;bad[12]=2;Check(!Inspect(bad,42),"invalid outer async rejected");
    bad=wire;bad[BaseWire+8]=2;Check(!Inspect(bad,42),"invalid nested async rejected");
    bad=wire;CrossplayChat::Put(bad.data()+BaseWire,Type);Check(!Inspect(bad,42),"wrong nested type rejected");
    bad=wire;CrossplayChat::Put(bad.data()+BaseWire+4,41);Check(!Inspect(bad,42),"foreign nested CID rejected");
    Check(!Inspect(wire,41) && !Inspect(wire,UINT32_MAX),"outer source remains admitted");
    puts("PASS bounded original turn-batch framing, exact scalar/checksum preservation and admitted CID projection. No game executed.");return 0;
}
