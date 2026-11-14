#pragma once
#include "crossplay_game_start.h"

// Opt-in diagnostic rendezvous, not a content-compatibility certificate.
// Both callers must have checked their actual content contract, received the
// identical fresh native setup, and retained original byte/object/thread guards.
// Frames are accepted only from Channel::Receive (authenticated and sequenced).
// "queued" means the original local send returned, never world loaded or Ready.
namespace CrossplayFreshStart {
constexpr uint64_t Lifetime=10000;
constexpr size_t ReceiptSize=112;
inline constexpr unsigned char ReadyMagic[8]={'S','7','W','R','E','D',0,1};
inline constexpr unsigned char QueuedMagic[8]={'S','7','W','Q','U','E',0,1};
enum class Phase {New,ReadyPending,AwaitPeer,Agreed,StartPending,AwaitQueued,GuestAttempt,HostAttempt,Complete,Failed};
class Barrier {
    Phase phase=Phase::New;
    Crossplay::Peer peer{};
    CrossplayGameStart::Agreement agreement{};
    Crossplay::Digest content{};
    uint64_t started=0,last=0;
    bool host=false,issued=false;
    bool Fail(){phase=Phase::Failed;return false;}
    template<size_t N> static bool Nonzero(const std::array<unsigned char,N>& value) {
        return std::any_of(value.begin(),value.end(),[](unsigned char x){return x!=0;});
    }
    bool Fresh(uint64_t now) {
        if(phase==Phase::New || phase==Phase::Failed || now<last || now<started || now-started>Lifetime)return Fail();
        last=now;return true;
    }
    void Receipt(const unsigned char* magic,std::vector<unsigned char>& output) const {
        output.assign(ReceiptSize,0);memcpy(output.data(),magic,8);
        memcpy(output.data()+8,peer.local_id.data(),16);memcpy(output.data()+24,peer.remote_id.data(),16);
        CrossplayChat::Put(output.data()+40,agreement.revision);
        memcpy(output.data()+44,agreement.setup.data(),32);memcpy(output.data()+76,content.data(),32);
        CrossplayChat::Put(output.data()+108,host?1u:2u);
    }
    bool Inspect(const unsigned char* magic,const std::vector<unsigned char>& input) const {
        return input.size()==ReceiptSize && !memcmp(input.data(),magic,8)
            && !memcmp(input.data()+8,peer.remote_id.data(),16) && !memcmp(input.data()+24,peer.local_id.data(),16)
            && CrossplayChat::Word(input.data()+40)==agreement.revision
            && !memcmp(input.data()+44,agreement.setup.data(),32) && !memcmp(input.data()+76,content.data(),32)
            && CrossplayChat::Word(input.data()+108)==(host?2u:1u);
    }
public:
    Barrier()=default;
    Barrier(const Barrier&)=delete;
    Barrier& operator=(const Barrier&)=delete;
    Phase GetPhase() const{return phase;}
    // Authenticated gameplay events can arrive while the original local start
    // is still pending. This permits bounded buffering only; the caller must
    // retain its native owner guards and wait for local enqueue before dispatch.
    bool CanBufferWorldEvents(uint64_t now) {
        if(phase!=Phase::AwaitQueued && phase!=Phase::GuestAttempt
            && phase!=Phase::HostAttempt && phase!=Phase::Complete)return false;
        return Fresh(now);
    }
    void Invalidate(){Fail();}
    bool Bind(const Crossplay::Peer& other,bool is_host,const CrossplayGameStart::Agreement& setup,
              const Crossplay::Digest& actual_content_contract,uint64_t now) {
        if(phase!=Phase::New || !CrossplayGameStart::Valid(setup) || !Nonzero(actual_content_contract)
            || !Nonzero(other.local_id) || !Nonzero(other.remote_id) || other.local_id==other.remote_id)return Fail();
        peer=other;host=is_host;agreement=setup;content=actual_content_contract;started=last=now;
        phase=Phase::ReadyPending;return true;
    }
    bool Ready(uint64_t now,std::vector<unsigned char>& output) {
        output.clear();if(!Fresh(now) || phase!=Phase::ReadyPending || issued)return Fail();
        Receipt(ReadyMagic,output);issued=true;return true;
    }
    bool Sent(uint64_t now) {
        if(!Fresh(now) || !issued)return Fail();issued=false;
        if(phase==Phase::ReadyPending){phase=Phase::AwaitPeer;return true;}
        if(host && phase==Phase::StartPending){phase=Phase::AwaitQueued;return true;}
        if(!host && phase==Phase::GuestAttempt){phase=Phase::Complete;return true;}
        return Fail();
    }
    bool ReceiveReady(uint64_t now,const std::vector<unsigned char>& input) {
        if(!Fresh(now) || phase!=Phase::AwaitPeer || !Inspect(ReadyMagic,input))return Fail();
        phase=Phase::Agreed;return true;
    }
    bool Start(uint64_t now,const std::vector<unsigned char>& captured,uint32_t source_host,std::vector<unsigned char>& output) {
        output.clear();if(!Fresh(now) || !host || phase!=Phase::Agreed || issued
            || !CrossplayGameStart::Encode(peer,agreement,captured,source_host,output))return Fail();
        phase=Phase::StartPending;issued=true;return true;
    }
    bool GuestAttempt(uint64_t now,const std::vector<unsigned char>& input,uint32_t source_host,uint32_t local_host,
                      std::vector<unsigned char>& wire) {
        wire.clear();if(!Fresh(now) || host || phase!=Phase::Agreed)return Fail();
        // Irreversible before returning a wire to a caller: no native retry.
        phase=Phase::GuestAttempt;
        if(!CrossplayGameStart::Decode(peer,agreement,input,source_host,local_host,wire))return Fail();
        return true;
    }
    bool GuestQueued(uint64_t now,bool original_send_returned,std::vector<unsigned char>& output) {
        output.clear();if(!Fresh(now) || host || phase!=Phase::GuestAttempt || issued || !original_send_returned)return Fail();
        Receipt(QueuedMagic,output);issued=true;return true;
    }
    bool HostAttempt(uint64_t now,const std::vector<unsigned char>& input) {
        if(!Fresh(now) || !host || phase!=Phase::AwaitQueued || !Inspect(QueuedMagic,input))return Fail();
        phase=Phase::HostAttempt;return true;
    }
    bool HostQueued(uint64_t now,bool original_send_returned) {
        if(!Fresh(now) || !host || phase!=Phase::HostAttempt || !original_send_returned)return Fail();
        phase=Phase::Complete;return true;
    }
};
}
