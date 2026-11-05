#include "neo_fifo_policy.h"
#include <algorithm>
#include <cstdio>
#include <vector>
int main() {
    unsigned checks=0, failures=0;
    auto check=[&](bool value){++checks;if(!value)++failures;};
    using neo_fifo::Intent;using neo_fifo::Key;
    Key old{Intent::Ordinary,250,100,0x10001,0x20001};
    Key young{Intent::Ordinary,1,200,0x10002,0x20002};
    check(neo_fifo::Before(old,young));check(!neo_fifo::Before(young,old));
    young.intent=Intent::Prefer;check(neo_fifo::Before(young,old));
    young.intent=Intent::Defer;check(neo_fifo::Before(old,young));
    auto rebooked=old;rebooked.reservation=0x90001;
    young.intent=Intent::Ordinary;check(neo_fifo::Before(rebooked,young));
    uint32_t normal[8]={0,1,2,3,4,5,6,7},changed[8]={1,0,2,3,4,5,6,7};
    check(neo_fifo::GroupOrder(1,normal,true)==-1);
    check(neo_fifo::GroupOrder(1,changed,false)==-1);
    check(neo_fifo::GroupOrder(1,changed,true)==0);
    check(neo_fifo::GroupOrder(0,changed,true)==1);
    check(neo_fifo::GroupOrder(4,changed,true)==-1);
    changed[7]=6;check(neo_fifo::GroupOrder(1,changed,true)==-1);
    uint32_t permutation[8]={0,1,2,3,4,5,6,7};
    do {
        bool changed_order=false;for(unsigned i=0;i<8;++i)if(permutation[i]!=i)changed_order=true;
        if(!changed_order)continue;
        for(unsigned i=0;i<8;++i)for(unsigned j=i+1;j<8;++j){
            const auto ga=neo_fifo::GroupOrder(permutation[i],permutation,true),gb=neo_fifo::GroupOrder(permutation[j],permutation,true);
            if(ga<0 || gb<0)continue;
            Key a{Intent::Ordinary,90,200,1,1},b{Intent::Ordinary,1,100,2,2};
            a.grouped=b.grouped=true;a.group_order=ga;b.group_order=gb;
            check(neo_fifo::Before(a,b));
        }
    }while(std::next_permutation(permutation,permutation+8));
    neo_fifo::Mark mark{0x10001,100,0x20000,Intent::Prefer};
    check(mark.Matches(0x10001,100,0x20000));
    check(!mark.Matches(0x20001,100,0x20000));
    check(!mark.Matches(0x10001,200,0x20000));
    check(!mark.Matches(0x10001,100,0x30000));
    std::vector<Key> keys;
    for(auto mode:{Intent::Prefer,Intent::Ordinary,Intent::Defer})
        for(int rank:{1,2})for(int birth:{-1,0,10})for(unsigned id:{1u,2u})
            keys.push_back(Key{mode,rank,birth,id,id+10});
    for(const auto& a:keys){
        check(!neo_fifo::Before(a,a));
        for(const auto& b:keys){
            check(!(neo_fifo::Before(a,b)&&neo_fifo::Before(b,a)));
            for(const auto& c:keys)
                if(neo_fifo::Before(a,b)&&neo_fifo::Before(b,c))check(neo_fifo::Before(a,c));
        }
    }
    std::printf("%s %u FIFO policy checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
