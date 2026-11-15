#include "crossplay_neo_agreement.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL NEO agreement line %u\n",__LINE__);return 1;}}while(0)
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    CrossplayNeo::Selection value{};value.features=CrossplayNeo::Fifo;value.enabled=true;value.content[0]=3;value.setup[0]=4;
    CrossplayNeo::Agreement left,right;std::vector<unsigned char> offer_a,offer_b,ack_a,ack_b,unused;
    REQUIRE(left.Bind(a,value));REQUIRE(right.Bind(b,value));REQUIRE(!left.Bind(a,value));
    REQUIRE(left.Offer(offer_a));REQUIRE(right.Offer(offer_b));REQUIRE(!left.Offer(unused));
    REQUIRE(!left.Ready());REQUIRE(left.Consume(offer_b,ack_a));REQUIRE(!left.Ready());
    REQUIRE(right.Consume(offer_a,ack_b));REQUIRE(left.Consume(ack_b,unused));REQUIRE(unused.empty());
    REQUIRE(right.Consume(ack_a,unused));REQUIRE(left.Ready()&&right.Ready());REQUIRE(!left.Consume(ack_b,unused));
    for(size_t at=0;at<CrossplayNeo::FrameSize;++at){
        auto corrupt=offer_b;corrupt[at]^=0x80;
        CrossplayNeo::Agreement candidate;REQUIRE(candidate.Bind(a,value));REQUIRE(candidate.Offer(unused));REQUIRE(!candidate.Consume(corrupt,unused));
    }
    for(size_t n=0;n<CrossplayNeo::FrameSize;++n){auto truncated=offer_b;truncated.resize(n);uint32_t kind=99;CrossplayNeo::Selection output{};REQUIRE(!CrossplayNeo::Decode(a,truncated,kind,output));REQUIRE(kind==99);}
    auto extra=offer_b;extra.push_back(0);uint32_t kind=99;CrossplayNeo::Selection output{};REQUIRE(!CrossplayNeo::Decode(a,extra,kind,output));
    CrossplayNeo::Agreement order;REQUIRE(order.Bind(a,value));REQUIRE(order.Offer(unused));REQUIRE(!order.Consume(ack_b,unused));
    for(unsigned field=0;field<5;++field){
        auto mismatch=value;if(field==0)mismatch.enabled=false;if(field==1)mismatch.features|=CrossplayNeo::Distance;
        if(field==2)mismatch.content[31]^=1;if(field==3)mismatch.setup[31]^=1;if(field==4)mismatch.saved_state[31]^=1;
        std::vector<unsigned char> frame;REQUIRE(CrossplayNeo::Encode(b,1,mismatch,frame));
        CrossplayNeo::Agreement candidate;REQUIRE(candidate.Bind(a,value));REQUIRE(candidate.Offer(unused));REQUIRE(!candidate.Consume(frame,unused));
    }
    for(auto features:{0u,16u,18u}){auto invalid=value;invalid.features=features;REQUIRE(!CrossplayNeo::Valid(invalid));}
    auto all=value;all.features=CrossplayNeo::All;REQUIRE(CrossplayNeo::Valid(all));
    auto commands=value;commands.features|=CrossplayNeo::PriorityCommands;REQUIRE(!CrossplayNeo::Valid(commands));
    commands.features|=CrossplayNeo::Intent;REQUIRE(CrossplayNeo::Valid(commands));
    printf("PASS %u NEO agreement checks: both confirmations, namespace/content/setup/state/variant binding, replay/order/size rejection. Synthetic protocol only.\n",checks);
    return 0;
}
