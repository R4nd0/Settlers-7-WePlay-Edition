#include "neo_logistics_state.h"
#include <cstdio>
int main() {
    unsigned checks=0,failures=0;auto check=[&](bool v){++checks;if(!v)++failures;};
    using namespace neo_logistics;
    SavedState state,decoded;check(Decode("NEO0\n",decoded)&&!decoded.enabled);
    state.enabled=true;state.turn=60000;
    state.marks={{65537,100,neo_fifo::Intent::Prefer},{65538,200,neo_fifo::Intent::Defer}};
    auto encoded=Encode(state);check(Decode(encoded,decoded));check(decoded.enabled&&decoded.turn==60000&&decoded.marks.size()==2);
    check(decoded.marks[0].id==65537&&decoded.marks[1].intent==neo_fifo::Intent::Defer);
    auto invalid=state;invalid.marks[0].created=60001;check(Encode(invalid).empty());
    invalid=state;invalid.marks[1].id=131073;check(Encode(invalid).empty());
    for(const auto& bad:std::vector<std::string>{"","NEO0","NEO0\nx","NEO1\n01\n0\n","NEO1\n-1\n0\n",
        "NEO1\n0\n65537\n","NEO1\n4\n1\n1,5,1\n","NEO1\n4\n1\n1,0,0\n",
        "NEO1\n4\n1\n4294967296,0,1\n","NEO1\n4\n1\n1,-0,1\n",
        "NEO1\n4\n2\n1,0,1\n65537,0,-1\n",encoded+"x",encoded.substr(0,encoded.size()-1),std::string(MaximumStateBytes+1,'x')})check(!Decode(bad,decoded));
    state.marks.clear();state.turn=100;
    for(unsigned i=0;i<65536;++i)state.marks.push_back({0x10000u+i,static_cast<int32_t>(i%101),i%2?neo_fifo::Intent::Prefer:neo_fifo::Intent::Defer});
    encoded=Encode(state);check(!encoded.empty()&&encoded.size()<MaximumStateBytes);check(Decode(encoded,decoded)&&decoded.marks.size()==65536);
    for(unsigned i=0;i<65536;++i)check(decoded.marks[i].id==state.marks[i].id&&decoded.marks[i].created==state.marks[i].created&&decoded.marks[i].intent==state.marks[i].intent);
    std::printf("%s %u NEO saved-state checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
