#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
extern "C" __declspec(dllimport) bool __cdecl S7TestInstallGuard();
int main() {
    WSADATA data{}; if (WSAStartup(MAKEWORD(2,2),&data) || !S7TestInstallGuard()) return 1;
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
    STARTUPINFOA sa{};sa.cb=sizeof(sa);STARTUPINFOW sw{};sw.cb=sizeof(sw);PROCESS_INFORMATION child{};
    if (CreateProcessA("?:\\weplay-invented-missing.exe",nullptr,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&sa,&child)
        || GetLastError()!=ERROR_ACCESS_DENIED) return 6;
    if (CreateProcessW(L"?:\\weplay-invented-missing.exe",nullptr,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&sw,&child)
        || GetLastError()!=ERROR_ACCESS_DENIED) return 7;
    SHELLEXECUTEINFOA shell_a{};shell_a.cbSize=sizeof(shell_a);shell_a.fMask=SEE_MASK_FLAG_NO_UI;shell_a.lpFile="?:\\weplay-invented-missing.exe";
    if (ShellExecuteExA(&shell_a) || GetLastError()!=ERROR_ACCESS_DENIED) return 8;
    SHELLEXECUTEINFOW shell_w{};shell_w.cbSize=sizeof(shell_w);shell_w.fMask=SEE_MASK_FLAG_NO_UI;shell_w.lpFile=L"?:\\weplay-invented-missing.exe";
    if (ShellExecuteExW(&shell_w) || GetLastError()!=ERROR_ACCESS_DENIED) return 9;
    WSACleanup();std::puts("Gold-specific wide DNS, WSAConnect and child/shell import guards passed.");return 0;
}
