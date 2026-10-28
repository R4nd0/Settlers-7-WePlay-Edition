// Separate DLL so the synthetic host's import table can be patched without
// recursively redirecting the guard's own operating-system calls.
#include "network_guard.h"
#include "local_pair_lab.h"
#if defined(S7_LOCAL_PAIR_LAB) || defined(S7_LOCAL_NETWORK_PUMP)
// Deliberately disturb Winsock's error slot: diagnostics must restore it.
static unsigned int observations = 0;
void ReportLocalStatus(const char*) { ++observations; WSASetLastError(12345); }
extern "C" __declspec(dllexport) unsigned int __cdecl S7TestTransportObservations() { return observations; }
#endif
#ifdef S7_LOCAL_PAIR_LAB
extern "C" __declspec(dllexport) void __cdecl S7TestLabAddress(unsigned long address) {
    local_pair_bind.store(address);
}
#endif
extern "C" __declspec(dllexport) bool __cdecl S7TestInstallGuard() {
    return InstallOfflineNetworkGuard();
}
extern "C" __declspec(dllexport) bool __cdecl S7TestEndpoint(const sockaddr* address, int length) {
    return IsOfflineEndpointAllowed(address, length);
}

extern "C" __declspec(dllexport) bool __cdecl S7TestConfigureLan(const char* local, const char* peer) {
    return ConfigureLanNetworkGuard(local, peer);
}
extern "C" __declspec(dllexport) bool __cdecl S7TestConfigureGroup(const char* local, const char* coordinator, const wchar_t* root) {
    return ConfigureLanGroupNetworkGuard(local, coordinator, root);
}
extern "C" __declspec(dllexport) bool __cdecl S7TestStartGroup() { return StartLanGroupNetworkGuard(); }
extern "C" __declspec(dllexport) bool __cdecl S7TestStopGroup() { return StopLanGroupNetworkGuard(); }
#ifdef S7_GUARD_TESTING
void SetLanGroupTestReadGate(HANDLE, HANDLE, DWORD);
unsigned long LanGroupTestReadCount();
bool MarkLanGroupTestSendPending(SOCKET, LPWSAOVERLAPPED);
extern "C" __declspec(dllexport) void __cdecl S7TestReadGate(HANDLE entered, HANDLE release, DWORD timeout) { SetLanGroupTestReadGate(entered, release, timeout); }
extern "C" __declspec(dllexport) unsigned long __cdecl S7TestReadCount() { return LanGroupTestReadCount(); }
extern "C" __declspec(dllexport) bool __cdecl S7TestMarkSendPending(SOCKET socket, LPWSAOVERLAPPED overlapped) {
    return MarkLanGroupTestSendPending(socket, overlapped);
}
#endif
