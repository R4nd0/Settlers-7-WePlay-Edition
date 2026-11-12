#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
extern "C" __declspec(dllimport) bool __cdecl S7TestInstallGuard();
extern "C" __declspec(dllimport) bool __cdecl S7TestGoldLoopback(unsigned int);
#ifdef S7_PLATFORM_INDEPENDENT
extern "C" __declspec(dllimport) BOOL WINAPI InternetGetCookieExW(LPCWSTR,LPCWSTR,LPWSTR,LPDWORD,DWORD,LPVOID);
extern "C" __declspec(dllimport) DWORD WINAPI InternetSetCookieExW(LPCWSTR,LPCWSTR,LPCWSTR,DWORD,DWORD_PTR);
#endif
int main() {
    WSADATA data{}; if (WSAStartup(MAKEWORD(2,2),&data)) return 1;
    if (S7TestGoldLoopback(0) || S7TestGoldLoopback(1) || S7TestGoldLoopback(10)
        || !S7TestGoldLoopback(2) || !S7TestInstallGuard() || S7TestGoldLoopback(3)) return 12;
    PADDRINFOW result=nullptr;
    if (GetAddrInfoW(L"203.0.113.1",nullptr,nullptr,&result)!=EAI_NONAME || result) return 2;
    if (GetAddrInfoW(L"::2",nullptr,nullptr,&result)!=EAI_NONAME || result) return 3;
    if (GetAddrInfoW(L"localhost",nullptr,nullptr,&result) || !result) return 4;
    FreeAddrInfoW(result);
    sockaddr_in external{};external.sin_family=AF_INET;external.sin_addr.s_addr=htonl(0xcb007101);
    // Invalid socket: a missing hook cannot emit a packet to the test address.
    if (WSAConnect(INVALID_SOCKET,reinterpret_cast<sockaddr*>(&external),sizeof(external),nullptr,nullptr,nullptr,nullptr)!=SOCKET_ERROR
        || WSAGetLastError()!=WSAEACCES) return 5;
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP),client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int size=sizeof(local);
    if (listener==INVALID_SOCKET || client==INVALID_SOCKET
        || bind(listener,reinterpret_cast<sockaddr*>(&local),sizeof(local)) || listen(listener,1)
        || getsockname(listener,reinterpret_cast<sockaddr*>(&local),&size)
        || WSAConnect(client,reinterpret_cast<sockaddr*>(&local),sizeof(local),nullptr,nullptr,nullptr,nullptr)) return 10;
    SOCKET accepted=accept(listener,nullptr,nullptr);if (accepted==INVALID_SOCKET) return 11;
    closesocket(accepted);closesocket(client);closesocket(listener);
    SOCKET isolated=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    local.sin_port=0;local.sin_addr.s_addr=htonl(INADDR_ANY);size=sizeof(local);
    if (isolated==INVALID_SOCKET || bind(isolated,reinterpret_cast<sockaddr*>(&local),sizeof(local))
        || getsockname(isolated,reinterpret_cast<sockaddr*>(&local),&size)
        || local.sin_addr.s_addr!=htonl(0x7f000002u)) return 13;
    // The same native port remains available on another loopback address.
    using NativeBind=int(WSAAPI*)(SOCKET,const sockaddr*,int);
    const auto original_bind=reinterpret_cast<NativeBind>(GetProcAddress(GetModuleHandleW(L"ws2_32.dll"),"bind"));
    SOCKET receiver=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP),sender=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in destination=local;destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (!original_bind || receiver==INVALID_SOCKET || sender==INVALID_SOCKET
        || original_bind(receiver,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))) return 15;
    const DWORD timeout=1000;
    if (setsockopt(receiver,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))
        || sendto(sender,"x",1,0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))!=1) return 16;
    sockaddr_in source{};size=sizeof(source);char received=0;
    if (recvfrom(receiver,&received,1,0,reinterpret_cast<sockaddr*>(&source),&size)!=1
        || received!='x' || source.sin_addr.s_addr!=htonl(0x7f000002u)) return 17;
    closesocket(sender);closesocket(receiver);
    closesocket(isolated);
    isolated=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    local.sin_port=0;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);size=sizeof(local);
    if (isolated==INVALID_SOCKET || bind(isolated,reinterpret_cast<sockaddr*>(&local),sizeof(local))
        || getsockname(isolated,reinterpret_cast<sockaddr*>(&local),&size)
        || local.sin_addr.s_addr!=htonl(0x7f000002u)) return 14;
    closesocket(isolated);
    STARTUPINFOA sa{};sa.cb=sizeof(sa);STARTUPINFOW sw{};sw.cb=sizeof(sw);PROCESS_INFORMATION child{};
    if (CreateProcessA("?:\\weplay-invented-missing.exe",nullptr,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&sa,&child)
        || GetLastError()!=ERROR_ACCESS_DENIED) return 6;
    if (CreateProcessW(L"?:\\weplay-invented-missing.exe",nullptr,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&sw,&child)
        || GetLastError()!=ERROR_ACCESS_DENIED) return 7;
    SHELLEXECUTEINFOA shell_a{};shell_a.cbSize=sizeof(shell_a);shell_a.fMask=SEE_MASK_FLAG_NO_UI;shell_a.lpFile="?:\\weplay-invented-missing.exe";
    if (ShellExecuteExA(&shell_a) || GetLastError()!=ERROR_ACCESS_DENIED) return 8;
    SHELLEXECUTEINFOW shell_w{};shell_w.cbSize=sizeof(shell_w);shell_w.fMask=SEE_MASK_FLAG_NO_UI;shell_w.lpFile=L"?:\\weplay-invented-missing.exe";
    if (ShellExecuteExW(&shell_w) || GetLastError()!=ERROR_ACCESS_DENIED) return 9;
#ifdef S7_PLATFORM_INDEPENDENT
    wchar_t untouched[]=L"authored sentinel";DWORD cookie_size=32;
    if (InternetGetCookieExW(L"https://weplay-test.invalid/",nullptr,untouched,&cookie_size,0,nullptr)
        || GetLastError()!=ERROR_NO_MORE_ITEMS || cookie_size || wcscmp(untouched,L"authored sentinel")) return 18;
    if (InternetSetCookieExW(L"https://weplay-test.invalid/",L"authored",L"invented",0,0)!=5
        || GetLastError()!=ERROR_ACCESS_DENIED) return 19;
    std::puts("Gold WinINet cookie imports rejected without reading/writing cookie data.");
#endif
    WSACleanup();std::puts("Gold-specific wide DNS, WSAConnect and child/shell import guards passed.");return 0;
}
