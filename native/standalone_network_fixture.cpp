// No external probe is submitted unless this exact executable has all WFP blocks.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <cstdio>
#include <vector>
DWORD VerifyStandaloneNetworkPolicy(const wchar_t* executable);
namespace {
int failures = 0,checks = 0;
#ifdef S7_KERNEL_PROBE_LIBRARY
DWORD probe_families=0;
#endif
void Check(bool value,const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s: %d\n",label,WSAGetLastError()); }
}
void Loopback(int family) {
    SOCKET listener = socket(family,SOCK_STREAM,IPPROTO_TCP);
    Check(listener != INVALID_SOCKET,"loopback listener");
    if (listener == INVALID_SOCKET) return;
    sockaddr_storage address{}; int length = 0;
    if (family == AF_INET) {
        auto* value = reinterpret_cast<sockaddr_in*>(&address); value->sin_family = AF_INET;
        value->sin_addr.s_addr = htonl(INADDR_LOOPBACK); length = sizeof(*value);
    } else {
        auto* value = reinterpret_cast<sockaddr_in6*>(&address); value->sin6_family = AF_INET6;
        value->sin6_addr = in6addr_loopback; length = sizeof(*value);
    }
    Check(bind(listener,reinterpret_cast<sockaddr*>(&address),length) == 0,"loopback bind");
    Check(listen(listener,1) == 0,"loopback listen");
    Check(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length) == 0,"loopback port");
    SOCKET client = socket(family,SOCK_STREAM,IPPROTO_TCP);
    Check(client != INVALID_SOCKET && connect(client,reinterpret_cast<sockaddr*>(&address),length) == 0,"loopback connection");
    if (client != INVALID_SOCKET) closesocket(client);
    closesocket(listener);
}
bool OwnAddress(int family,sockaddr_storage& address,int& length) {
    ULONG size = 0;
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
    if (GetAdaptersAddresses(AF_UNSPEC,flags,nullptr,nullptr,&size) != ERROR_BUFFER_OVERFLOW || size > 1048576) return false;
    std::vector<unsigned char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_UNSPEC,flags,nullptr,adapters,&size)) return false;
    for (auto* item = adapters; item; item = item->Next) {
        if (item->OperStatus != IfOperStatusUp || item->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* unicast = item->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            auto* value = unicast->Address.lpSockaddr;
            if (!value || value->sa_family != family || unicast->DadState != IpDadStatePreferred) continue;
            if (family == AF_INET) {
                auto* ipv4 = reinterpret_cast<sockaddr_in*>(value);
                if (!ipv4->sin_addr.s_addr || (ntohl(ipv4->sin_addr.s_addr) >> 24) == 127) continue;
                length = sizeof(sockaddr_in);
            } else {
                auto* ipv6 = reinterpret_cast<sockaddr_in6*>(value);
                if (IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr) || IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr)) continue;
                length = sizeof(sockaddr_in6);
            }
            memcpy(&address,value,length);
            if (family == AF_INET) reinterpret_cast<sockaddr_in*>(&address)->sin_port = 0;
            else reinterpret_cast<sockaddr_in6*>(&address)->sin6_port = 0;
            return true;
        }
    }
    return false;
}
void Delivery(int family,bool datagram,bool baseline,bool game_port=false) {
    sockaddr_storage address{}; int length = 0;
    if (!OwnAddress(family,address,length)) {
#ifndef S7_KERNEL_PROBE_LIBRARY
        Check(false,family == AF_INET ? "own non-loopback IPv4 address available" : "own non-loopback IPv6 address available");
#endif
        return;
    }
#ifdef S7_KERNEL_PROBE_LIBRARY
    probe_families|=family==AF_INET ? 2u : 4u;
#endif
    if (game_port) reinterpret_cast<sockaddr_in*>(&address)->sin_port=htons(3074);
    const int type = datagram ? SOCK_DGRAM : SOCK_STREAM, protocol = datagram ? IPPROTO_UDP : IPPROTO_TCP;
    SOCKET receiver = socket(family,type,protocol), sender = socket(family,type,protocol);
    Check(receiver != INVALID_SOCKET && sender != INVALID_SOCKET,"own-address probe sockets");
    if (receiver == INVALID_SOCKET || sender == INVALID_SOCKET) {
        if (receiver != INVALID_SOCKET) closesocket(receiver);
        if (sender != INVALID_SOCKET) closesocket(sender);
        return;
    }
    if (bind(receiver,reinterpret_cast<sockaddr*>(&address),length)
        || getsockname(receiver,reinterpret_cast<sockaddr*>(&address),&length) || (!datagram && listen(receiver,1))) {
        Check(false,"own-address receiver bind/listen"); closesocket(receiver); closesocket(sender); return;
    }
    // Every destination was obtained from an assigned address on this host and
    // successfully bound by our receiver. There is no Internet or LAN peer probe.
    u_long nonblocking = 1; ioctlsocket(sender,FIONBIO,&nonblocking);
    if (datagram) sendto(game_port ? receiver : sender,"x",1,0,reinterpret_cast<sockaddr*>(&address),length);
    else connect(sender,reinterpret_cast<sockaddr*>(&address),length);
    fd_set readers; FD_ZERO(&readers); FD_SET(receiver,&readers);
#ifdef S7_KERNEL_PROBE_LIBRARY
    timeval timeout{0,200000};
#else
    timeval timeout{2,0};
#endif
    int selected = select(0,&readers,nullptr,nullptr,&timeout);
    bool delivered = false;
    if (selected > 0) {
        if (datagram) { char value = 0; delivered = recv(receiver,&value,1,0) == 1 && value == 'x'; }
        else { SOCKET accepted = accept(receiver,nullptr,nullptr); delivered = accepted != INVALID_SOCKET; if (delivered) closesocket(accepted); }
    }
    Check(selected >= 0 && delivered == baseline,datagram ? "actual non-loopback UDP delivery" : "actual non-loopback TCP delivery");
    closesocket(receiver); closesocket(sender);
}
}
#ifdef S7_KERNEL_PROBE_LIBRARY
DWORD ProbeStandaloneKernelIsolation() {
    failures=checks=0;probe_families=0;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2),&data)) return 0;
    Loopback(AF_INET);
    SOCKET ipv6=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    const bool ipv6_available=ipv6!=INVALID_SOCKET;
    if (ipv6_available) { closesocket(ipv6);Loopback(AF_INET6); }
    Delivery(AF_INET,false,false);Delivery(AF_INET,true,false);
    if (ipv6_available) { Delivery(AF_INET6,false,false);Delivery(AF_INET6,true,false); }
    WSACleanup();
    // With no assigned non-loopback address there is no own-host negative
    // target. Offline play remains possible; the parent still verifies all
    // four persistent rules, including IPv6, before creating this process.
    return failures ? 0 : 1u|probe_families;
}
#else
int wmain(int argc,wchar_t** argv) {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,executable,MAX_PATH)) return 1;
    if (argc==2 && !wcscmp(argv[1],L"--self-address")) {
        sockaddr_storage address{};int length=0;char text[64]{};
        if (!OwnAddress(AF_INET,address,length) || !InetNtopA(AF_INET,&reinterpret_cast<sockaddr_in*>(&address)->sin_addr,text,sizeof(text))) return 1;
        std::puts(text);return 0;
    }
    const wchar_t* basename = wcsrchr(executable,L'\\');
    const bool baseline = argc == 2 && (!wcscmp(argv[1],L"--baseline") || !wcscmp(argv[1],L"--baseline-peer"))
        && basename && !wcscmp(basename+1,L"standalone_network_baseline.exe");
    if (!baseline && VerifyStandaloneNetworkPolicy(executable)) {
        std::puts("REFUSED: no negative probe without verified persistent application filters");
        return 5;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2),&data)) return 1;
    Loopback(AF_INET); Loopback(AF_INET6);
    Delivery(AF_INET,false,baseline); Delivery(AF_INET,true,baseline);
    Delivery(AF_INET6,false,baseline); Delivery(AF_INET6,true,baseline);
    if (argc==2 && (!wcscmp(argv[1],L"--peer-open") || !wcscmp(argv[1],L"--peer-closed") || !wcscmp(argv[1],L"--baseline-peer")))
        Delivery(AF_INET,true,!wcscmp(argv[1],L"--peer-open") || baseline,true);
    WSACleanup();
    std::printf("%s %d kernel network checks (%s)\n",failures ? "FAIL" : "PASS",checks,baseline ? "unfiltered control" : "filtered own-host receivers");
    return failures ? 1 : 0;
}
#endif
