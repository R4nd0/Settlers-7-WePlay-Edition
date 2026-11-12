#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_setup_signature_codec.h"
#include <stdio.h>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlaySetupMethodsOracle(uint32_t value) {
    CrossplaySetupSignatureNative::Pin pin{};
    return CrossplaySetupSignatureNative::Layout(static_cast<Crossplay::Edition>(value),pin) && CrossplaySetupSignatureNative::Methods(pin)?1u:0u;
}
extern "C" __declspec(dllexport) void __cdecl WePlaySetupRulesOracle(uint32_t value,void* root,void* visitor) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplaySetupSignatureNative::Pin pin{};
    if(CrossplaySetupSignatureNative::Layout(edition,pin))CrossplaySetupSignatureNative::Rules(edition,pin,root,visitor);
}
extern "C" __declspec(dllexport) void __cdecl WePlaySetupPlayersOracle(uint32_t value,void* root,void* visitor) {
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplaySetupSignatureNative::Pin pin{};
    if(CrossplaySetupSignatureNative::Layout(edition,pin))CrossplaySetupSignatureNative::Players(edition,pin,root,visitor);
}
namespace {
unsigned checks=0;
void Require(bool okay,const char* reason) {++checks;if(!okay){fprintf(stderr,"FAIL %s\n",reason);exit(1);}}
}
int main() {
    using namespace CrossplaySetupSignature;
    Crossplay::Peer sender{},receiver{};
    for(unsigned i=0;i<16;++i){sender.local_id[i]=static_cast<unsigned char>(i+1);sender.remote_id[i]=static_cast<unsigned char>(i+33);}
    receiver.local_id=sender.remote_id;receiver.remote_id=sender.local_id;
    Signature value{};value.rules=2152;value.players=41;value.digest[0]=7;
    std::vector<unsigned char> frame;Signature decoded{};
    Require(Encode(sender,value,frame) && frame.size()==FrameSize,"bounded frame");
    Require(Decode(receiver,frame,decoded) && Equal(value,decoded),"reciprocal native signature");
    Require(!Decode(sender,frame,decoded) && !Valid(decoded),"reflected frame");
    for(size_t n=0;n<FrameSize+2;++n) {
        auto bad=frame;bad.resize(n);decoded=value;
        Require(n==FrameSize?Decode(receiver,bad,decoded):!Decode(receiver,bad,decoded) && !Valid(decoded),"exact frame size");
    }
    for(size_t n=0;n<40;++n) {
        auto bad=frame;bad[n]^=1;decoded=value;
        Require(!Decode(receiver,bad,decoded) && !Valid(decoded),"magic and reciprocal peer binding");
    }
    for(unsigned offset:{40u,44u})for(uint32_t n:{0u,1u,65536u,65537u,UINT32_MAX}) {
        auto candidate=value;if(offset==40)candidate.rules=n;else candidate.players=n;
        auto bad=frame;CrossplayChat::Put(bad.data()+offset,n);
        const bool valid=n && n<=MaximumWriterBytes;
        Require(Decode(receiver,bad,decoded)==valid,"reader writer-size bounds");
        auto output=frame;Require(Encode(sender,candidate,output)==valid && (valid || output.empty()),"writer size and failure clearing");
    }
    auto zero=frame;memset(zero.data()+48,0,32);decoded=value;
    Require(!Decode(receiver,zero,decoded) && !Valid(decoded),"zero digest excluded");
    for(size_t n=48;n<FrameSize;++n) {
        auto changed=frame;changed[n]^=128;
        Require(Decode(receiver,changed,decoded) && !Equal(value,decoded),"different signatures remain different");
    }
    auto changed=value;changed.rules++;Require(!Equal(value,changed),"rule length differs");
    changed=value;changed.players++;Require(!Equal(value,changed),"template-player length differs");
    changed={};Require(!Equal(changed,changed),"empty signatures never agree");
    Crossplay::Digest digest{};
    const unsigned char abc[]={ 'a','b','c' };
    const unsigned char expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Require(CrossplaySetupSignatureNative::Hash(std::vector<unsigned char>(abc,abc+3),digest)
        && !memcmp(digest.data(),expected,32),"SHA256 known vector");
    Require(!CrossplaySetupSignatureNative::Hash({},digest) && digest==Crossplay::Digest{},"empty input clears digest");
    Require(!CrossplaySetupSignatureNative::Hash(std::vector<unsigned char>(2*65536+2049),digest),"hash input bound");
    Require(!WePlaySetupMethodsOracle(0) && !WePlaySetupMethodsOracle(4) && !WePlaySetupMethodsOracle(UINT32_MAX),"unsupported native profiles");
    CrossplayNative::ParticipantEvent binding;decoded=value;
    Require(!CrossplaySetupSignatureNative::Capture(static_cast<Crossplay::Edition>(0),binding,{},false,decoded,nullptr)
        && !Valid(decoded),"unbound capture clears result");
    printf("PASS %u setup-signature checks; no native game or start executed.\n",checks);return 0;
}
