// Own synthetic peers over actual loopback TCP. No game or native game ABI.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "crossplay_channel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
using namespace Crossplay;
unsigned int checks=0;
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"Channel check failed: %u\n",checks);std::exit(2);}}
template<size_t N> std::array<unsigned char,N> EnvironmentHex(const char* name) {
    char buffer[N*2+1]{};Check(GetEnvironmentVariableA(name,buffer,sizeof(buffer))==N*2);
    std::array<unsigned char,N> result{};
    for(size_t i=0;i<N;++i) {
        unsigned int value=0;
        for(unsigned int j=0;j<2;++j) {
            const char c=buffer[2*i+j];Check((c>='0' && c<='9') || (c>='a' && c<='f'));
            value=value*16+static_cast<unsigned int>(c<='9'?c-'0':c-'a'+10);
        }
        result[i]=static_cast<unsigned char>(value);
    }
    SetEnvironmentVariableA(name,nullptr);return result;
}
std::vector<unsigned char> Message(unsigned int index,unsigned int direction) {
    std::vector<unsigned char> bytes(index==0?1:index==1?65536:60000+index);
    for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>((i*17+index*31+direction*91)&255);
    return bytes;
}
}
int main(int argc,char** argv) {
    Check(argc==4);char* end=nullptr;const long port=std::strtol(argv[1],&end,10);
    Check(end && !*end && port>0 && port<=65535);
    const long edition=std::strtol(argv[2],&end,10);Check(end && !*end && (edition==2 || edition==3));
    const bool reject=!std::strcmp(argv[3],"reject"),bad_key=!std::strcmp(argv[3],"bad-key");
    const bool burst=!std::strcmp(argv[3],"burst");
    Check(reject || bad_key || burst || !std::strcmp(argv[3],"exchange"));
    Config left{},right{};left.port=right.port=static_cast<uint16_t>(port);
    left.slot=1;right.slot=2;left.edition=Edition::Gold;right.edition=static_cast<Edition>(edition);
    left.expected_peer=right.edition;right.expected_peer=left.edition;
    left.session=right.session=EnvironmentHex<16>("WEPLAY_CROSSPLAY_TEST_SESSION");
    left.key=EnvironmentHex<32>("WEPLAY_CROSSPLAY_TEST_KEY_A");right.key=EnvironmentHex<32>("WEPLAY_CROSSPLAY_TEST_KEY_B");
    left.executable=EnvironmentHex<32>("WEPLAY_CROSSPLAY_TEST_EXE_A");right.executable=EnvironmentHex<32>("WEPLAY_CROSSPLAY_TEST_EXE_B");
    left.content.fill(0x51);right.content=left.content;
    left.native_identity[0]=42;right.native_identity.fill(0x22);
    {Config invalid=left;invalid.expected_peer=Edition::Gold;Channel excluded;Check(!excluded.Start(invalid));}
    {Config invalid=right;invalid.expected_peer=right.edition;Channel excluded;Check(!excluded.Start(invalid));}
    if(bad_key)left.key[0]^=0x80;
    Channel a,b;Check(!a.Send({1}));Check(!a.GetPeer());Check(a.Start(left));Check(b.Start(right));
    Check(!a.Start(left));Check(!a.Send({1}));
    const ULONGLONG deadline=GetTickCount64()+10000;
    bool failed=false;
    while(GetTickCount64()<deadline && (a.GetState()!=State::Connected || b.GetState()!=State::Connected)) {
        if(!a.Poll() || !b.Poll()){failed=true;break;}Sleep(1);
    }
    if(reject || bad_key) {
        Check(failed);a.Close();b.Close();std::printf("PASS authenticated channel rejection (%u checks).\n",checks);return 0;
    }
    Check(!failed && a.GetState()==State::Connected && b.GetState()==State::Connected);
    Check(a.GetPeer() && b.GetPeer());Check(a.GetPeer()->edition==right.edition);
    Check(a.GetPeer()->local_id==b.GetPeer()->remote_id && b.GetPeer()->local_id==a.GetPeer()->remote_id);
    Check(a.GetPeer()->native_identity==right.native_identity && b.GetPeer()->native_identity==left.native_identity);
    Check(a.GetPeer()->peer_in_local_namespace==b.GetPeer()->local_in_peer_namespace);
    Check(a.GetPeer()->local_in_peer_namespace==b.GetPeer()->peer_in_local_namespace);
    Check(a.GetPeer()->peer_in_local_namespace!=left.native_identity);
    Check(b.GetPeer()->peer_in_local_namespace!=right.native_identity);
    if(burst) {
        for(unsigned int i=0;i<1024;++i)Check(b.Send({static_cast<unsigned char>(i)}));
        while(b.HasPendingWrites() && GetTickCount64()<deadline){Check(b.Poll());Sleep(1);}
        Check(!b.HasPendingWrites());b.Close();
        unsigned int received=0;std::vector<unsigned char> bytes;
        while(GetTickCount64()<deadline && received<1024) {
            Check(a.Poll());
            while(a.Receive(bytes)){Check(bytes==std::vector<unsigned char>({static_cast<unsigned char>(received)}));++received;}
            Sleep(1);
        }
        Check(received==1024);a.Close();
        std::printf("PASS native transport burst of 1024 tiny messages before EOF (%u checks).\n",checks);return 0;
    }
    bool wrong_thread=true;std::thread thread([&]{wrong_thread=a.Send({1});});thread.join();Check(!wrong_thread);
    Check(!a.Send({}));Check(!a.Send(std::vector<unsigned char>(65537)));
    for(unsigned int i=0;i<32;++i){Check(a.Send(Message(i,0)));Check(b.Send(Message(i,1)));}
    unsigned int left_count=0,right_count=0;std::vector<unsigned char> bytes;
    while(GetTickCount64()<deadline && (left_count<32 || right_count<32)) {
        Check(a.Poll());Check(b.Poll());
        while(a.Receive(bytes)){Check(bytes==Message(left_count,1));++left_count;}
        while(b.Receive(bytes)){Check(bytes==Message(right_count,0));++right_count;}
        Sleep(1);
    }
    Check(left_count==32 && right_count==32);
    // TCP EOF follows queued bytes: the remaining channel must expose the last
    // authenticated event before retiring its session, even if EOF coalesces.
    Check(a.Send({7,8,9}));Check(a.Poll());a.Close();
    bool last=false;
    while(GetTickCount64()<deadline && !last) {
        if(!b.Poll())break;
        if(b.Receive(bytes)){Check(bytes==std::vector<unsigned char>({7,8,9}));last=true;}
        Sleep(1);
    }
    Check(last);b.Close();Check(!b.Send({1}));Check(!b.GetPeer());
    std::printf("PASS native transport with synthetic edition profiles, identity binding, ordered payloads and EOF drain (%u checks).\n",checks);
}
