#include "crossplay_setup_root_stream.h"
#include <cstdio>
#include <cstdlib>

namespace Root=CrossplaySetupRootStream;
unsigned checks=0;
void Require(bool okay,const char* what){++checks;if(!okay){fprintf(stderr,"FAIL %s\n",what);exit(1);}}
Crossplay::Peer Sender(){Crossplay::Peer p{};p.edition=Crossplay::Edition::Gold;p.local_id[0]=1;p.remote_id[0]=2;return p;}
Crossplay::Peer Recipient(){auto p=Sender();std::swap(p.local_id,p.remote_id);p.edition=Crossplay::Edition::UbisoftHistory;return p;}
std::vector<unsigned char> Bytes(size_t size){std::vector<unsigned char> v(size);for(size_t i=0;i<size;++i)v[i]=static_cast<unsigned char>(31*i+7);return v;}
int main(){
    for(uint32_t version:{1u,2u})for(size_t size:{size_t(1),size_t(16383),size_t(16384),size_t(16385),size_t(33211),size_t(65536),Root::MaximumBytes}){
        const auto source=Bytes(size);std::vector<std::vector<unsigned char>> frames;Root::Descriptor descriptor{};
        Require(Root::Encode(Sender(),17,version,source,frames,&descriptor),"encode bounded original-byte envelope");
        Require(descriptor.bytes==size && descriptor.revision==17 && descriptor.format==version,"descriptor labels full stream");
        Require(frames.size()==(size+Root::ChunkBytes-1)/Root::ChunkBytes,"canonical fragment count");
        Root::Receiver receiver;Root::Complete result{};Require(receiver.Bind(Recipient(),descriptor),"bind expected checkpoint and peer");
        for(size_t i=0;i<frames.size();++i){
            result.bytes={9};Require(!receiver.Take(result) && result.bytes.empty(),"no partial native input exposed");
            Require(frames[i].size()<=Crossplay::Channel::MaximumPayload,"each chunk fits authenticated channel");
            Require(receiver.Accept(frames[i]),"receive ordered complete fragments");
            Require(receiver.GetState()==(i+1==frames.size()?Root::Receiver::State::Complete:Root::Receiver::State::Receiving),"complete only after final digest verification");
        }
        Require(receiver.Take(result) && result.bytes==source && result.descriptor.digest==descriptor.digest,"one complete byte-exact transfer");
        Require(!receiver.Take(result) && result.bytes.empty(),"single consumption");
        Require(!receiver.Bind(Recipient(),descriptor),"no reuse across ownership intervals");
    }
    std::vector<std::vector<unsigned char>> frames;Root::Descriptor descriptor{};const auto source=Bytes(33211);
    Require(Root::Encode(Sender(),9,2,source,frames,&descriptor),"multi-chunk mutation fixture");
    const auto rejected=[&](const std::vector<unsigned char>& frame){Root::Receiver receiver;Root::Complete result{};
        Require(receiver.Bind(Recipient(),descriptor),"mutation fixture bind");
        Require(!receiver.Accept(frame) && receiver.GetState()==Root::Receiver::State::Failed,"invalid frame consumes interval");
        Require(!receiver.Accept(frames[0]) && !receiver.Take(result) && result.bytes.empty(),"failure cannot expose or restart partial bytes");};
    for(size_t i=0;i<Root::Header;++i){auto bad=frames[0];bad[i]^=1;rejected(bad);}
    for(size_t length:{size_t(0),size_t(7),Root::Header-1,Root::Header,frames[0].size()-1}){
        auto bad=frames[0];bad.resize(length);rejected(bad);
    }
    auto extended=frames[0];extended.push_back(0);rejected(extended);
    rejected(frames[1]); // Ordered Channel cannot legitimately deliver a gap.
    {Root::Receiver receiver;Require(receiver.Bind(Recipient(),descriptor) && receiver.Accept(frames[0]),"duplicate fixture");
        Require(!receiver.Accept(frames[0]),"duplicate offset rejected");}
    {Root::Receiver receiver;Require(receiver.Bind(Recipient(),descriptor),"hash mutation bind");
        for(size_t i=0;i<frames.size();++i){auto frame=frames[i];if(i==0)frame.back()^=1;
            Require(receiver.Accept(frame)==(i+1!=frames.size()),"full SHA256 rejects payload mutation");}
        Root::Complete result{};Require(!receiver.Take(result),"mutated bytes never become native input");}
    {Root::Receiver receiver;Require(receiver.Bind(Recipient(),descriptor),"cross-transfer fixture");
        auto other=frames[0];CrossplayChat::Put(other.data()+40,10);Require(!receiver.Accept(other),"different setup revision rejected");}
    {Root::Receiver receiver;Require(receiver.Bind(Sender(),descriptor),"wrong-direction fixture");Require(!receiver.Accept(frames[0]),"sender and receiver IDs are directional");}
    {Root::Receiver receiver;Require(!receiver.Accept(frames[0]) && !receiver.Bind(Recipient(),descriptor),"unbound receive cannot later acquire an interval");}
    for(unsigned variant=0;variant<5;++variant){auto bad=descriptor;
        if(variant==0)bad.revision=0;else if(variant==1)bad.format=3;else if(variant==2)bad.bytes=0;
        else if(variant==3)bad.bytes=static_cast<uint32_t>(Root::MaximumBytes+1);else bad.digest={};
        Root::Receiver receiver;Require(!receiver.Bind(Recipient(),bad),"invalid descriptor rejected before allocation");}
    for(unsigned variant=0;variant<6;++variant){auto peer=Sender();auto data=source;uint32_t revision=9,format=2;
        if(variant==0)revision=0;else if(variant==1)format=3;else if(variant==2)data.clear();
        else if(variant==3)data.resize(Root::MaximumBytes+1);else if(variant==4)peer.local_id={};else peer.remote_id=peer.local_id;
        frames={{1}};descriptor.revision=99;
        Require(!Root::Encode(peer,revision,format,data,frames,&descriptor) && frames.empty() && !descriptor.revision,"invalid encoding clears outputs");}
    printf("PASS %u bounded setup-stream framing and SHA256 checks; no native reader or world invoked.\n",checks);
}
