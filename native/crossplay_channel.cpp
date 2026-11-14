#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include "crossplay_channel.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>

namespace Crossplay {
namespace {
constexpr size_t Header=40, Tag=32;
constexpr unsigned short Hello=1, Connected=2, Event=3;
bool Nonzero(const unsigned char* data,size_t size) {
    unsigned char value=0;for(size_t i=0;i<size;++i)value|=data[i];return value!=0;
}
void Put(std::vector<unsigned char>& out,uint64_t value,size_t size) {
    for(size_t i=0;i<size;++i)out.push_back(static_cast<unsigned char>(value>>(8*i)));
}
uint64_t Get(const unsigned char* data,size_t size) {
    uint64_t value=0;for(size_t i=0;i<size;++i)value|=static_cast<uint64_t>(data[i])<<(8*i);return value;
}
bool Identity(Edition edition,const Id& value) {
    if(edition==Edition::Gold)return Get(value.data(),4)>0 && Get(value.data(),4)<=0x7fffffffu
        && !Nonzero(value.data()+4,12);
    return (edition==Edition::UbisoftHistory || edition==Edition::SteamHistory)
        && Nonzero(value.data(),value.size());
}
}
struct Channel::Impl {
    Config config{};Peer peer{};State state=State::New;
    DWORD owner=GetCurrentThreadId();SOCKET socket=INVALID_SOCKET;
    bool winsock=false;BCRYPT_ALG_HANDLE algorithm=nullptr;
    uint64_t sent=0,received=0;ULONGLONG started=0;
    std::vector<unsigned char> input,output;
    size_t output_at=0,incoming_bytes=0;
    std::deque<std::vector<unsigned char>> incoming;
    bool Owned() const {return GetCurrentThreadId()==owner;}
    void Close() {
        if(socket!=INVALID_SOCKET){closesocket(socket);socket=INVALID_SOCKET;}
        if(algorithm){BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;}
        if(winsock){WSACleanup();winsock=false;}
        SecureZeroMemory(config.key.data(),config.key.size());
        input.clear();output.clear();incoming.clear();incoming_bytes=0;output_at=0;
        SecureZeroMemory(&peer,sizeof(peer));state=State::Closed;
    }
    bool Mac(const unsigned char* data,size_t length,Digest& tag) {
        if(!algorithm || length>Channel::MaximumPayload+Header)return false;
        BCRYPT_HASH_HANDLE hash=nullptr;
        bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,config.key.data(),
            static_cast<ULONG>(config.key.size()),0)>=0;
        if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(data),static_cast<ULONG>(length),0)>=0
            && BCryptFinishHash(hash,tag.data(),static_cast<ULONG>(tag.size()),0)>=0;
        if(hash)BCryptDestroyHash(hash);
        return valid;
    }
    bool Queue(unsigned short kind,const std::vector<unsigned char>& payload) {
        if(payload.size()>Channel::MaximumPayload || sent==std::numeric_limits<uint64_t>::max())return false;
        if(output_at){output.erase(output.begin(),output.begin()+output_at);output_at=0;}
        if(output.size()+Header+payload.size()+Tag>Channel::MaximumBuffered)return false;
        std::vector<unsigned char> frame{'W','P','X','1'};
        Put(frame,1,2);Put(frame,kind,2);
        frame.insert(frame.end(),config.session.begin(),config.session.end());
        Put(frame,sent+1,8);frame.push_back(config.slot);
        frame.push_back(kind==Hello?0:static_cast<unsigned char>(3-config.slot));
        Put(frame,0,2);Put(frame,payload.size(),4);
        frame.insert(frame.end(),payload.begin(),payload.end());
        Digest tag{};if(!Mac(frame.data(),frame.size(),tag))return false;
        frame.insert(frame.end(),tag.begin(),tag.end());output.insert(output.end(),frame.begin(),frame.end());
        ++sent;return true;
    }
    bool Accept() {
        size_t consumed=0;
        for(unsigned int count=0;count<64 && input.size()-consumed>=Header;++count) {
            const auto* p=input.data()+consumed;
            const auto kind=Get(p+6,2),sequence=Get(p+24,8),length=Get(p+36,4);
            if(memcmp(p,"WPX1",4) || Get(p+4,2)!=1 || memcmp(p+8,config.session.data(),16)
                || received==std::numeric_limits<uint64_t>::max() || sequence!=received+1
                || p[33]!=config.slot || Get(p+34,2)!=0 || length>Channel::MaximumPayload)return false;
            const size_t total=Header+static_cast<size_t>(length)+Tag;
            if(input.size()-consumed<total)break;
            Digest tag{};if(!Mac(p,Header+static_cast<size_t>(length),tag))return false;
            unsigned char difference=0;
            for(size_t i=0;i<Tag;++i)difference|=tag[i]^p[Header+length+i];
            if(difference)return false;
            const auto* body=p+Header;
            if(kind==Connected) {
                if(state!=State::Authenticating || p[32]!=0 || length!=84
                    || body[0]!=static_cast<unsigned char>(config.expected_peer)
                    || body[1]!=3-config.slot || body[2] || body[3])return false;
                Peer candidate{};candidate.edition=static_cast<Edition>(body[0]);
                memcpy(candidate.local_id.data(),body+4,16);memcpy(candidate.remote_id.data(),body+20,16);
                memcpy(candidate.native_identity.data(),body+36,16);
                memcpy(candidate.peer_in_local_namespace.data(),body+52,16);
                memcpy(candidate.local_in_peer_namespace.data(),body+68,16);
                if(!Nonzero(candidate.local_id.data(),16) || !Nonzero(candidate.remote_id.data(),16)
                    || candidate.local_id==candidate.remote_id || !Identity(candidate.edition,candidate.native_identity)
                    || !Identity(config.edition,candidate.peer_in_local_namespace)
                    || candidate.peer_in_local_namespace==config.native_identity
                    || !Identity(candidate.edition,candidate.local_in_peer_namespace)
                    || candidate.local_in_peer_namespace==candidate.native_identity)return false;
                peer=candidate;state=State::Connected;
            } else if(kind==Event) {
                if((state!=State::Connected && state!=State::Draining) || p[32]!=3-config.slot || !length
                    || incoming.size()>=4096 || incoming_bytes+length>Channel::MaximumBuffered)return false;
                incoming.emplace_back(body,body+length);incoming_bytes+=static_cast<size_t>(length);
            } else return false;
            ++received;consumed+=total;
        }
        if(consumed)input.erase(input.begin(),input.begin()+consumed);
        return true;
    }
    bool Poll() {
        if(!Owned() || state==State::New || state==State::Closed)return false;
        if(state==State::Draining) {
            if(!Accept() || !CompleteEofInput())return false;
            return !incoming.empty() || !input.empty();
        }
        if(state!=State::Connected && GetTickCount64()-started>60000)return false;
        if(state==State::Connecting) {
            fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(socket,&writes);FD_SET(socket,&errors);
            timeval timeout{};const int ready=select(0,nullptr,&writes,&errors,&timeout);
            if(ready==SOCKET_ERROR || FD_ISSET(socket,&errors))return false;
            if(!FD_ISSET(socket,&writes))return true;
            int error=0,size=sizeof(error);
            if(getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size) || error)return false;
            std::vector<unsigned char> hello{static_cast<unsigned char>(config.edition),
                static_cast<unsigned char>(config.expected_peer),0,0};
            hello.insert(hello.end(),config.executable.begin(),config.executable.end());
            hello.insert(hello.end(),config.content.begin(),config.content.end());
            hello.insert(hello.end(),config.native_identity.begin(),config.native_identity.end());
            if(!Queue(Hello,hello))return false;
            state=State::Authenticating;
        }
        for(unsigned int n=0;n<32 && output_at<output.size();++n) {
            const size_t length=std::min<size_t>(16384,output.size()-output_at);
            const int written=send(socket,reinterpret_cast<const char*>(output.data()+output_at),static_cast<int>(length),0);
            if(written==SOCKET_ERROR){if(WSAGetLastError()==WSAEWOULDBLOCK)break;return false;}
            if(written<=0)return false;output_at+=static_cast<size_t>(written);
        }
        if(output_at==output.size()){output.clear();output_at=0;}
        std::array<unsigned char,16384> buffer{};
        for(unsigned int n=0;n<32;++n) {
            const int length=recv(socket,reinterpret_cast<char*>(buffer.data()),static_cast<int>(buffer.size()),0);
            if(length==SOCKET_ERROR){if(WSAGetLastError()==WSAEWOULDBLOCK)break;return false;}
            if(!length) {
                if(state!=State::Connected)return false;
                closesocket(socket);socket=INVALID_SOCKET;state=State::Draining;break;
            }
            if(length<0 || input.size()+static_cast<size_t>(length)>Channel::MaximumBuffered)return false;
            input.insert(input.end(),buffer.begin(),buffer.begin()+length);
            if(!Accept())return false;
        }
        return Accept() && (state!=State::Draining || CompleteEofInput());
    }
    bool CompleteEofInput() const {
        // A bounded Accept may leave additional complete frames for the next
        // owner tick. EOF only rejects a truncated next frame, not that backlog.
        return input.empty() || (input.size()>=Header
            && input.size()>=Header+Get(input.data()+36,4)+Tag);
    }
};
Channel::Channel():impl(new Impl){}
Channel::~Channel(){impl->Close();}
bool Channel::Start(const Config& config) {
    if(!impl->Owned() || impl->state!=State::New || !config.port || (config.slot!=1 && config.slot!=2)
        || !Identity(config.edition,config.native_identity)
        || (config.expected_peer!=Edition::Gold && config.expected_peer!=Edition::UbisoftHistory
            && config.expected_peer!=Edition::SteamHistory)
        || ((config.edition==Edition::Gold)==(config.expected_peer==Edition::Gold))
        || !Nonzero(config.session.data(),16) || !Nonzero(config.key.data(),32)
        || !Nonzero(config.executable.data(),32) || !Nonzero(config.content.data(),32))return false;
    impl->config=config;
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return false;impl->winsock=true;
    if(BCryptOpenAlgorithmProvider(&impl->algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0){Close();return false;}
    impl->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(impl->socket==INVALID_SOCKET){Close();return false;}
    // Small lockstep frames must not wait for TCP's batching timer. Keep the
    // byte stream, authenticators, sequencing and all payload bounds unchanged.
    const int low_latency=1;int actual=0,actual_length=sizeof(actual);
    if(setsockopt(impl->socket,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&low_latency),sizeof(low_latency))
        || getsockopt(impl->socket,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&actual),&actual_length)
        || (actual_length!=1 && actual_length!=static_cast<int>(sizeof(actual))) || actual!=1){Close();return false;}
    sockaddr_in local{};local.sin_family=AF_INET;
    local.sin_addr.s_addr=htonl(0x7f000001u+config.slot);
    sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(config.port);remote.sin_addr.s_addr=htonl(0x7f000001u);
    u_long nonblocking=1;
    if(bind(impl->socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))
        || ioctlsocket(impl->socket,FIONBIO,&nonblocking)){Close();return false;}
    const int result=connect(impl->socket,reinterpret_cast<sockaddr*>(&remote),sizeof(remote));
    if(result==SOCKET_ERROR && WSAGetLastError()!=WSAEWOULDBLOCK){Close();return false;}
    impl->started=GetTickCount64();impl->state=State::Connecting;return true;
}
bool Channel::Poll(){if(!impl->Owned())return false;if(impl->Poll())return true;impl->Close();return false;}
bool Channel::Send(const std::vector<unsigned char>& bytes) {
    if(!impl->Owned() || impl->state!=State::Connected || bytes.empty() || bytes.size()>MaximumPayload)return false;
    if(impl->Queue(Event,bytes))return true;impl->Close();return false;
}
bool Channel::Receive(std::vector<unsigned char>& bytes) {
    if(!impl->Owned() || (impl->state!=State::Connected && impl->state!=State::Draining) || impl->incoming.empty())return false;
    bytes=std::move(impl->incoming.front());impl->incoming_bytes-=bytes.size();impl->incoming.pop_front();return true;
}
bool Channel::HasPendingWrites()const{return impl->Owned() && impl->state==State::Connected
    && impl->output_at<impl->output.size();}
void Channel::Close(){if(impl->Owned())impl->Close();}
State Channel::GetState()const{return impl->Owned()?impl->state:State::Closed;}
const Peer* Channel::GetPeer()const{return impl->Owned() && (impl->state==State::Connected || impl->state==State::Draining)?&impl->peer:nullptr;}
}
