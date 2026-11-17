#include "crossplay_saved_session_config.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL saved config line %u\n",__LINE__);return 1;}}while(0)
    namespace R=CrossplaySavedSessionConfig;
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    CrossplayResumeTransfer::Checkpoint checkpoint{};checkpoint.content[0]=3;checkpoint.setup[0]=4;checkpoint.file[0]=5;checkpoint.neo[0]=6;
    checkpoint.logic_turn=300;checkpoint.replay_turn=301;checkpoint.bytes=4097;checkpoint.features=19;checkpoint.enabled=true;
    const R::Connections source{42,43},target{81,82};
    const std::string map="Maps\\Skirmish\\s04_m06_Monhaim_Valley",save="s1009";
    std::vector<unsigned char> wire(13),frame,decoded;
    CrossplayChat::Put(wire.data(),CrossplaySessionConfig::Class);CrossplayChat::Put(wire.data()+4,CrossplaySessionConfig::Type);
    CrossplayChat::Put(wire.data()+8,source.host);wire[12]=1;
    auto string=[&](const std::string& value){
        const auto at=wire.size();wire.resize(at+8);memcpy(wire.data()+at,"Str_",4);
        CrossplayChat::Put(wire.data()+at+4,static_cast<uint32_t>(value.size()+1));
        wire.insert(wire.end(),value.begin(),value.end());wire.push_back(0);
    };
    string(map);string(save);auto at=wire.size();wire.resize(at+4+8*11);CrossplayChat::Put(wire.data()+at,8);at+=4;
    for(unsigned i=0;i<8;++i){
        auto* row=wire.data()+at+i*11;row[0]=i<4?1:0;
        CrossplayChat::Put(row+1,i<4?i+1:UINT32_MAX);row[5]=0;
        CrossplayChat::Put(row+6,i==0?source.host:i==1?source.guest:0);row[10]=i<2?4:i<4?2:0;
    }
    R::Layout layout;REQUIRE(R::Inspect(wire,source,map,save,layout));
    REQUIRE(!CrossplaySessionConfig::Inspect(wire,source,layout));
    REQUIRE(R::Encode(a,checkpoint,source,map,save,wire,frame));
    REQUIRE(R::Decode(b,checkpoint,source,target,map,save,frame,decoded));
    REQUIRE(R::Inspect(decoded,target,map,save,layout));
    auto expected=wire;CrossplayChat::Put(expected.data()+8,target.host);
    CrossplayChat::Put(expected.data()+layout.slots_at+6,target.host);
    CrossplayChat::Put(expected.data()+layout.slots_at+11+6,target.guest);REQUIRE(decoded==expected);
    for(size_t offset=0;offset<frame.size();++offset){
        auto bad=frame;bad[offset]^=0x80;REQUIRE(!R::Decode(b,checkpoint,source,target,map,save,bad,decoded));REQUIRE(decoded.empty());
    }
    for(size_t length=0;length<frame.size();++length){
        auto bad=frame;bad.resize(length);REQUIRE(!R::Decode(b,checkpoint,source,target,map,save,bad,decoded));
    }
    auto extra=frame;extra.push_back(0);REQUIRE(!R::Decode(b,checkpoint,source,target,map,save,extra,decoded));
    REQUIRE(!R::Decode(a,checkpoint,source,target,map,save,frame,decoded));
    REQUIRE(!R::Decode(b,checkpoint,source,target,map,"s1000",frame,decoded));
    REQUIRE(!R::Decode(b,checkpoint,source,target,"Maps/Other",save,frame,decoded));
    REQUIRE(!R::Decode(b,checkpoint,source,{81,81},map,save,frame,decoded));
    auto invalid=checkpoint;invalid.file[1]=1;REQUIRE(!R::Decode(b,invalid,source,target,map,save,frame,decoded));
    REQUIRE(R::Inspect(wire,source,map,save,layout));auto snapshot=wire;
    CrossplayChat::Put(snapshot.data()+layout.slots_at+6,0);CrossplayChat::Put(snapshot.data()+layout.slots_at+11+6,0);
    REQUIRE(!R::Inspect(snapshot,source,map,save,layout));
    REQUIRE(R::BindSource(snapshot,source,map,save,decoded));REQUIRE(decoded==wire);
    // A fresh owner learns metadata from the authenticated saved envelope,
    // rather than embedding a previous test archive/checkpoint in its binary.
    b.edition=Crossplay::Edition::Gold;
    CrossplayResumeTransfer::Checkpoint discovered{};std::string token;
    REQUIRE(R::Discover(b,checkpoint.content,source,target,map,frame,discovered,token,decoded));
    REQUIRE(token==save && discovered.logic_turn==300 && discovered.replay_turn==301
        && discovered.bytes==4097 && discovered.enabled && decoded==expected);
    unsigned char first[148]{},second[148]{};
    CrossplayResumeTransfer::PutCheckpoint(first,checkpoint);CrossplayResumeTransfer::PutCheckpoint(second,discovered);
    REQUIRE(!memcmp(first,second,sizeof(first)));
    for(size_t length=0;length<frame.size();++length){
        auto bad=frame;bad.resize(length);
        REQUIRE(!R::Discover(b,checkpoint.content,source,target,map,bad,discovered,token,decoded));
        REQUIRE(token.empty() && decoded.empty() && !CrossplayResumeTransfer::Valid(discovered));
    }
    for(size_t offset=0;offset<40;++offset){
        auto bad=frame;bad[offset]^=0x80;
        REQUIRE(!R::Discover(b,checkpoint.content,source,target,map,bad,discovered,token,decoded));
    }
    for(auto offset:{168u,172u,176u,180u,184u,188u,192u}){
        auto bad=frame;CrossplayChat::Put(bad.data()+offset,UINT32_MAX);
        REQUIRE(!R::Discover(b,checkpoint.content,source,target,map,bad,discovered,token,decoded));
        REQUIRE(token.empty() && decoded.empty());
    }
    auto other_content=checkpoint.content;other_content[0]^=1;
    REQUIRE(!R::Discover(b,other_content,source,target,map,frame,discovered,token,decoded));
    REQUIRE(!R::Discover(b,checkpoint.content,source,target,"Maps/Other",frame,discovered,token,decoded));
    b.edition=Crossplay::Edition::SteamHistory;
    REQUIRE(!R::Discover(b,checkpoint.content,source,target,map,frame,discovered,token,decoded));
    b.edition=Crossplay::Edition::Gold;
    // A different valid advertisement is metadata only; the later file digest
    // must prove it. Never silently keep the previous owner's checkpoint.
    auto next=checkpoint;next.logic_turn=700;next.replay_turn=701;next.file[1]=9;next.neo[2]=10;
    REQUIRE(R::Encode(a,next,source,map,"s1000",wire,frame)==false); // Original native token still s1009.
    REQUIRE(R::Encode(a,next,source,map,save,wire,frame));
    REQUIRE(R::Discover(b,checkpoint.content,source,target,map,frame,discovered,token,decoded));
    REQUIRE(discovered.logic_turn==700 && discovered.file==next.file && discovered.neo==next.neo);
    printf("PASS %u saved configuration checks: checkpoint/map/cache-token/peer binding, original Ready and AI bytes preserved, three routing CIDs projected; synthetic transport only, no file admission or reload claim.\n",checks);
    return 0;
}
