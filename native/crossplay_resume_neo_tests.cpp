#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_resume_neo.h"
#include "crossplay_resume_admission.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL saved-NEO line %u\n",__LINE__);return 1;}}while(0)
    namespace N=CrossplayResumeNeo;namespace A=CrossplayResumeAdmission;
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    CrossplayResumeTransfer::Checkpoint cp{};cp.content[0]=3;cp.setup[0]=4;cp.file[0]=5;cp.logic_turn=300;cp.replay_turn=301;cp.bytes=24722247;cp.features=19;cp.enabled=true;
    const std::string serialized="return{{[\"version\"]=1,[\"payload\"]=\"NEO2\\\n301\\\n300\\\n2\\\n65576,41,-1\\\n65607,66,-1\\\n\",},}";
    const unsigned char known[]={0x46,0x4b,0xa8,0xe2,0x59,0x3e,0x08,0x73,0x97,0x94,0x71,0xc8,0xe9,0x2d,0x40,0x40,0x24,0xe9,0xb9,0x8f,0x71,0x28,0x38,0xa0,0x4c,0xd7,0x5a,0x06,0x6b,0xec,0xfe,0x27};
    memcpy(cp.neo.data(),known,32);std::string payload;REQUIRE(N::Inspect(cp,serialized,payload));
    REQUIRE(payload=="NEO2\n301\n300\n2\n65576,41,-1\n65607,66,-1\n");
    std::vector<unsigned char> frame;REQUIRE(N::Encode(a,cp,1,0,serialized,frame));N::Receiver receiver;REQUIRE(receiver.Bind(cp));REQUIRE(receiver.Accept(b,frame,payload));REQUIRE(receiver.Complete());REQUIRE(!receiver.Accept(b,frame,payload));
    for(size_t at=0;at<frame.size();++at){auto bad=frame;bad[at]^=0x80;N::Receiver rejected;REQUIRE(rejected.Bind(cp));
        // An increased declared total can be a valid unfinished fragment. It
        // must expose no payload, and replaying the original first frame fails.
        const bool accepted=rejected.Accept(b,bad,payload);REQUIRE(!accepted || !rejected.Complete());REQUIRE(payload.empty());REQUIRE(!rejected.Accept(b,frame,payload));}
    auto wrong=cp;wrong.logic_turn=299;wrong.replay_turn=300;REQUIRE(!N::Inspect(wrong,serialized,payload));wrong=cp;wrong.enabled=false;REQUIRE(!N::Inspect(wrong,serialized,payload));
    const std::string executable="return(os.execute('never execute save metadata'))";
    wrong=cp;REQUIRE(N::Hash(reinterpret_cast<const unsigned char*>(executable.data()),executable.size(),wrong.neo));REQUIRE(!N::Inspect(wrong,executable,payload));
    neo_logistics::SavedState large;large.enabled=true;large.turn=301;large.logic_turn=300;
    for(uint32_t id=65537;id<71537;++id)large.marks.push_back({id,41,neo_fifo::Intent::Defer});
    const auto text=neo_logistics::Encode(large);std::string pickle="return{{[\"version\"]=1,[\"payload\"]=\"";
    for(char c:text){if(c=='\n')pickle+="\\n";else pickle+=c;}pickle+="\",},}";
    REQUIRE(pickle.size()>2*N::MaximumChunk);wrong=cp;REQUIRE(N::Hash(reinterpret_cast<const unsigned char*>(pickle.data()),pickle.size(),wrong.neo));REQUIRE(N::Inspect(wrong,pickle,payload));
    N::Receiver fragmented;REQUIRE(fragmented.Bind(wrong));uint32_t sequence=0;
    for(uint32_t offset=0;offset<pickle.size();offset+=static_cast<uint32_t>(N::MaximumChunk)){
        REQUIRE(N::Encode(a,wrong,++sequence,offset,pickle,frame));
        if(sequence==1){N::Receiver reordered;REQUIRE(reordered.Bind(wrong));auto bad=frame;CrossplayChat::Put(bad.data()+196,1);REQUIRE(!reordered.Accept(b,bad,payload));}
        REQUIRE(fragmented.Accept(b,frame,payload));
        if(!fragmented.Complete())REQUIRE(payload.empty());
    }
    REQUIRE(fragmented.Complete());REQUIRE(payload==text);
    A::Kind kind{};REQUIRE(A::Encode(a,cp,A::Kind::Prepared,frame));REQUIRE(A::Decode(b,cp,frame,kind));REQUIRE(kind==A::Kind::Prepared);
    for(size_t at=0;at<frame.size();++at){auto bad=frame;bad[at]^=0x80;REQUIRE(!A::Decode(b,cp,bad,kind));}
    REQUIRE(A::Encode(a,cp,A::Kind::Queued,frame));REQUIRE(A::Decode(b,cp,frame,kind));REQUIRE(kind==A::Kind::Queued);REQUIRE(!A::Decode(a,cp,frame,kind));
    const CrossplaySessionConfig::Connections ids{0x0c000001,0x0c000002};
    std::vector<unsigned char> slots(8*0x18);
    for(size_t i=0;i<8;++i){auto* row=slots.data()+i*0x18;CrossplayChat::Put(row,i<2?3:0);CrossplayChat::Put(row+8,i<2?static_cast<uint32_t>(i+1):UINT32_MAX);}
    REQUIRE(A::SavedSlots(slots.data(),slots.size(),ids,false));REQUIRE(!A::SavedSlots(slots.data(),slots.size(),ids,true));
    for(size_t i=0;i<4;++i){auto* row=slots.data()+i*0x18;row[4]=1;CrossplayChat::Put(row,i<2?3:4);CrossplayChat::Put(row+8,static_cast<uint32_t>(i+1));}
    CrossplayChat::Put(slots.data()+20,0x0e000001);
    REQUIRE(A::SavedSlots(slots.data(),slots.size(),ids,true));REQUIRE(!A::SavedSlots(slots.data(),slots.size(),ids,false));
    for(size_t i=0;i<8;++i){auto bad=slots;CrossplayChat::Put(bad.data()+i*0x18+16,0x1234);REQUIRE(!A::SavedSlots(bad.data(),bad.size(),ids,true));}
    for(size_t i=2;i<8;++i){auto bad=slots;CrossplayChat::Put(bad.data()+i*0x18+20,1);REQUIRE(!A::SavedSlots(bad.data(),bad.size(),ids,true));}
    for(size_t i=0;i<4;++i){auto bad=slots;bad[i*0x18+4]=0;REQUIRE(!A::SavedSlots(bad.data(),bad.size(),ids,true));}
    REQUIRE(!A::SavedSlots(nullptr,slots.size(),ids,true));REQUIRE(!A::SavedSlots(slots.data(),slots.size()-1,ids,true));
    REQUIRE(!A::SavedSlots(slots.data(),slots.size(),{1,1},true));
    printf("PASS %u saved-NEO/admission checks: exact serialized digest, NEO2 logic/replay/intent preservation, bounded fragments, saved native cache shapes, no Lua execution or native Ready generation.\n",checks);return 0;
}
