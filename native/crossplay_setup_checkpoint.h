#pragma once
#include "crossplay_setup_signature.h"

// Diagnostic agreement on the original rules/template writers. The opaque
// local snapshot detects changes within one process; it is never compared
// across editions. This protocol cannot produce a GameStart::Agreement and
// does not qualify active player options, content, world or turn compatibility.
// Frames must arrive through the existing authenticated channel.
namespace CrossplaySetupCheckpoint {
using Nonce=Crossplay::Id;
constexpr size_t FrameSize=120;
constexpr uint64_t Lifetime=10000;
enum class Phase {New,OfferPending,AwaitOffer,AckPending,AwaitAck,ConfirmPending,AwaitConfirm,Confirmed,Failed};
struct Snapshot {
    CrossplaySetupSignature::Signature writers{};
    Crossplay::Digest local{};
    uint64_t session=0,preparation=0,cache=0;
    uint32_t thread=0;
};
template<size_t N> inline bool Nonzero(const std::array<unsigned char,N>& bytes) {
    return std::any_of(bytes.begin(),bytes.end(),[](unsigned char c){return c!=0;});
}
inline bool Valid(const Snapshot& value) {
    return value.thread && value.session && value.preparation && value.cache
        && Nonzero(value.local) && CrossplaySetupSignature::Valid(value.writers);
}
inline bool Equal(const Snapshot& a,const Snapshot& b) {
    return Valid(a) && Valid(b) && a.thread==b.thread && a.session==b.session
        && a.preparation==b.preparation && a.cache==b.cache && a.local==b.local
        && CrossplaySetupSignature::Equal(a.writers,b.writers);
}
class Checkpoint {
    Phase phase=Phase::New;
    Crossplay::Peer peer{};
    Snapshot snapshot{};
    Nonce own{},host_nonce{},guest_nonce{};
    uint32_t revision=0;
    uint64_t started=0,last=0;
    bool host=false,issued=false;
    bool Fail(){phase=Phase::Failed;return false;}
    bool Fresh(const Snapshot& current,uint64_t now) {
        if(phase==Phase::New || phase==Phase::Failed || now<last || now<started
            || now-started>Lifetime || !Equal(snapshot,current))return Fail();
        last=now;return true;
    }
    void Frame(uint32_t kind,std::vector<unsigned char>& output) const {
        const unsigned char magic[8]={'S','7','C','K','P','T',0,1};
        output.assign(FrameSize,0);memcpy(output.data(),magic,8);
        memcpy(output.data()+8,peer.local_id.data(),16);memcpy(output.data()+24,peer.remote_id.data(),16);
        CrossplayChat::Put(output.data()+40,kind);CrossplayChat::Put(output.data()+44,revision);
        CrossplayChat::Put(output.data()+48,snapshot.writers.rules);CrossplayChat::Put(output.data()+52,snapshot.writers.players);
        memcpy(output.data()+56,snapshot.writers.digest.data(),32);
        memcpy(output.data()+88,host_nonce.data(),16);memcpy(output.data()+104,guest_nonce.data(),16);
    }
public:
    Checkpoint()=default;
    Checkpoint(const Checkpoint&)=delete;
    Checkpoint& operator=(const Checkpoint&)=delete;
    void Invalidate(){Fail();}
    Phase GetPhase() const{return phase;}
    bool Bind(const Crossplay::Peer& other,bool is_host,uint32_t generation,
              const Snapshot& captured,const Nonce& challenge,uint64_t now) {
        if(phase!=Phase::New || !generation || !Valid(captured) || !Nonzero(challenge)
            || !Nonzero(other.local_id) || !Nonzero(other.remote_id) || other.local_id==other.remote_id)return Fail();
        peer=other;host=is_host;revision=generation;snapshot=captured;own=challenge;
        started=last=now;phase=host?Phase::OfferPending:Phase::AwaitOffer;
        if(host)host_nonce=own;
        return true;
    }
    bool Offer(const Snapshot& current,uint64_t now,std::vector<unsigned char>& output) {
        output.clear();if(!Fresh(current,now) || !host || phase!=Phase::OfferPending || issued)return Fail();
        Frame(1,output);issued=true;return true;
    }
    // Call exactly once after Channel::Send accepted the pending output. A
    // failed send must invalidate the checkpoint, never advance its phase.
    bool Sent(const Snapshot& current,uint64_t now) {
        if(!Fresh(current,now))return false;
        if(!issued)return Fail();issued=false;
        if(phase==Phase::OfferPending && host){phase=Phase::AwaitAck;return true;}
        if(phase==Phase::AckPending && !host){phase=Phase::AwaitConfirm;return true;}
        if(phase==Phase::ConfirmPending && host){phase=Phase::Confirmed;return true;}
        return Fail();
    }
    bool Receive(const Snapshot& current,uint64_t now,const std::vector<unsigned char>& input,
                 std::vector<unsigned char>& output) {
        output.clear();const unsigned char magic[8]={'S','7','C','K','P','T',0,1};
        if(!Fresh(current,now) || input.size()!=FrameSize || memcmp(input.data(),magic,8)
            || memcmp(input.data()+8,peer.remote_id.data(),16) || memcmp(input.data()+24,peer.local_id.data(),16)
            || CrossplayChat::Word(input.data()+44)!=revision
            || CrossplayChat::Word(input.data()+48)!=snapshot.writers.rules
            || CrossplayChat::Word(input.data()+52)!=snapshot.writers.players
            || memcmp(input.data()+56,snapshot.writers.digest.data(),32))return Fail();
        Nonce incoming_host{},incoming_guest{};
        memcpy(incoming_host.data(),input.data()+88,16);memcpy(incoming_guest.data(),input.data()+104,16);
        const uint32_t kind=CrossplayChat::Word(input.data()+40);
        if(!host && phase==Phase::AwaitOffer && kind==1 && Nonzero(incoming_host)
            && incoming_host!=own && !Nonzero(incoming_guest)) {
            host_nonce=incoming_host;guest_nonce=own;phase=Phase::AckPending;Frame(2,output);issued=true;return true;
        }
        if(host && phase==Phase::AwaitAck && kind==2 && incoming_host==own
            && Nonzero(incoming_guest) && incoming_guest!=own) {
            guest_nonce=incoming_guest;phase=Phase::ConfirmPending;Frame(3,output);issued=true;return true;
        }
        if(!host && phase==Phase::AwaitConfirm && kind==3
            && incoming_host==host_nonce && incoming_guest==own) {
            phase=Phase::Confirmed;return true;
        }
        return Fail();
    }
    bool Confirmed(const Snapshot& current,uint64_t now) {
        return Fresh(current,now) && phase==Phase::Confirmed;
    }
};
}
