#include "network_guard.h"
#include "lan_address.h"
#include "local_pair_lab.h"
#include "lab_address_table.h"
#if defined(S7_LOCAL_PAIR_LAB) || defined(S7_LOCAL_NETWORK_PUMP)
#include "runtime_config.h"
#endif
#include "transport_observation.h"
#include <atomic>
#include <ws2tcpip.h>
#include <windows.h>
#include "lan_membership_file.h"
#include <winhttp.h>
#include <shellapi.h>
#include <cstring>
#include <vector>
#include <cstddef>
#include <intrin.h>
#include <process.h>
#ifdef S7_GUARD_TEST_DIAGNOSTICS
#include <cstdio>
#endif

#if !defined(_M_IX86)
#error This adapter targets the verified 32-bit game only.
#endif

namespace {
std::atomic<unsigned long> lan_local{0}, lan_peer{0};
std::atomic<bool> lan_configured{false};
std::atomic<bool> lan_group{false};
SRWLOCK membership_lock = SRWLOCK_INIT;
lan_membership::Snapshot membership;
std::wstring membership_root;
DWORD membership_thread = 0;
SRWLOCK membership_lifecycle = SRWLOCK_INIT;
HANDLE membership_stop_event = nullptr, membership_worker = nullptr;
std::atomic<bool> membership_stopping{true};
#ifdef S7_GUARD_TESTING
std::atomic<HANDLE> membership_test_entered{nullptr}, membership_test_release{nullptr};
std::atomic<DWORD> membership_join_timeout{5000};
std::atomic<unsigned long> membership_reads{0};
void MembershipReadGate() {
    HANDLE entered = membership_test_entered.load(), release = membership_test_release.load();
    if (entered && release) { SetEvent(entered); WaitForSingleObject(release, INFINITE); }
}
#endif
bool AllowedV4(unsigned long network_order) {
    if ((ntohl(network_order) >> 24) == 127) return true;
    if (!lan_configured.load(std::memory_order_acquire)) return false;
    if (network_order == lan_local.load() || network_order == lan_peer.load()) return true;
    if (!lan_group.load(std::memory_order_acquire)) return false;
    AcquireSRWLockShared(&membership_lock);
    const bool allowed = !membership_stopping.load() && lan_membership::Contains(membership, ntohl(network_order), GetTickCount64());
    ReleaseSRWLockShared(&membership_lock);
    return allowed;
}
unsigned long LocalBindAddress() {
#ifdef S7_LOCAL_PAIR_LAB
    return htonl(local_pair_bind.load());
#else
    return lan_configured.load(std::memory_order_acquire) ? lan_local.load() : htonl(INADDR_LOOPBACK);
#endif
}
}
bool ConfigureLanNetworkGuard(const char* local, const char* peer) {
    uint32_t left = 0, right = 0;
    if (lan_configured.load() || !ValidLanPair(local, peer)
        || !ParsePrivateLanAddress(local, left) || !ParsePrivateLanAddress(peer, right)) return false;
    // Called once by startup configuration before the game network is initialized.
    lan_local.store(htonl(left)); lan_peer.store(htonl(right));
    lan_configured.store(true, std::memory_order_release); return true;
}

bool ConfigureLanGroupNetworkGuard(const char* local, const char* coordinator, const wchar_t* root) {
    uint32_t local_ip = 0, server_ip = 0;
    if (lan_configured.load() || !ParsePrivateLanAddress(local, local_ip) || !coordinator) return false;
    if (strcmp(coordinator, "127.0.0.1") == 0) server_ip = 0x7f000001;
    else if (!ParsePrivateLanAddress(coordinator, server_ip)) return false;
    try {
        if (!lan_membership::ValidRoot(root)) return false;
        membership_root = root;
    } catch (...) { return false; }
    membership_thread = GetCurrentThreadId();
    lan_local.store(htonl(local_ip)); lan_peer.store(htonl(server_ip));
    lan_group.store(true, std::memory_order_release);
    lan_configured.store(true, std::memory_order_release);
    return true;
}

namespace {
unsigned __stdcall MembershipWorker(void*) {
    while (!membership_stopping.load(std::memory_order_acquire)) {
        lan_membership::Snapshot next;
        try {
            void (*gate)() = nullptr;
#ifdef S7_GUARD_TESTING
            gate = MembershipReadGate;
#endif
            if (!lan_membership::Read(membership_root, ntohl(lan_local.load()), ntohl(lan_peer.load()), GetTickCount64(), next, gate)) next = {};
        } catch (...) { next = {}; }
        AcquireSRWLockExclusive(&membership_lock);
        if (!membership_stopping.load()) membership = next;
        ReleaseSRWLockExclusive(&membership_lock);
#ifdef S7_GUARD_TESTING
        ++membership_reads;
#endif
        if (WaitForSingleObject(membership_stop_event, 500) != WAIT_TIMEOUT) break;
    }
    return 0;
}
}
bool StartLanGroupNetworkGuard() {
    if (!lan_group.load(std::memory_order_acquire)) return true;
    if (GetCurrentThreadId() != membership_thread) return false;
    AcquireSRWLockExclusive(&membership_lifecycle);
    if (membership_worker) {
        const bool running = !membership_stopping.load() && WaitForSingleObject(membership_worker, 0) == WAIT_TIMEOUT;
        ReleaseSRWLockExclusive(&membership_lifecycle); return running;
    }
    // IAT/callsite replacements already require this adapter to remain loaded.
    // Pin it explicitly so a slow cancelled filesystem call cannot outlive DLL
    // storage even when the bounded Stop join has timed out.
    HMODULE module = nullptr;
    const bool pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&StartLanGroupNetworkGuard), &module) != 0;
    if (pinned) membership_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!membership_stop_event) { ReleaseSRWLockExclusive(&membership_lifecycle); return false; }
    membership_stopping.store(false, std::memory_order_release);
    membership_worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, MembershipWorker, nullptr, 0, nullptr));
    if (!membership_worker) {
        membership_stopping.store(true); CloseHandle(membership_stop_event); membership_stop_event = nullptr;
    }
    const bool okay = membership_worker != nullptr;
    ReleaseSRWLockExclusive(&membership_lifecycle); return okay;
}
bool StopLanGroupNetworkGuard() {
    if (!lan_group.load(std::memory_order_acquire)) return true;
    AcquireSRWLockExclusive(&membership_lifecycle);
    AcquireSRWLockExclusive(&membership_lock);
    membership_stopping.store(true, std::memory_order_release);
    membership = {};
    ReleaseSRWLockExclusive(&membership_lock);
    bool stopped = true;
    if (membership_worker) {
        SetEvent(membership_stop_event);
        CancelSynchronousIo(membership_worker);
        DWORD timeout = 5000;
#ifdef S7_GUARD_TESTING
        timeout = membership_join_timeout.load();
#endif
        stopped = WaitForSingleObject(membership_worker, timeout) == WAIT_OBJECT_0;
        if (stopped) {
            CloseHandle(membership_worker); membership_worker = nullptr;
            CloseHandle(membership_stop_event); membership_stop_event = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&membership_lifecycle);
    return stopped;
}
#ifdef S7_GUARD_TESTING
void SetLanGroupTestReadGate(HANDLE entered, HANDLE release, DWORD join_timeout) {
    membership_test_entered.store(entered); membership_test_release.store(release);
    membership_join_timeout.store(join_timeout);
}
unsigned long LanGroupTestReadCount() { return membership_reads.load(); }
#endif

bool IsOfflineEndpointAllowed(const sockaddr* address, int length) {
    if (!address || length < static_cast<int>(sizeof(address->sa_family))) return false;
    if (address->sa_family == AF_INET && length >= sizeof(sockaddr_in)) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        return AllowedV4(ipv4->sin_addr.s_addr);
    }
    if (address->sa_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        const unsigned char* bytes = ipv6->sin6_addr.s6_addr;
        static const unsigned char loopback[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        static const unsigned char mapped[12] = {0,0,0,0,0,0,0,0,0,0,255,255};
        return memcmp(bytes, loopback, 16) == 0 || (memcmp(bytes, mapped, 12) == 0 && bytes[12] == 127);
    }
    return false;
}

namespace {
#ifdef S7_LOCAL_NETWORK_PUMP
void LanObserve(uint32_t bits) {
    static std::atomic<uint32_t> observed{0};
    const auto fresh = bits & ~observed.fetch_or(bits);
    using namespace transport_observation;
    const struct { uint32_t bit; const char* label; } labels[] = {
        {denied, "lan-game-destination-denied"}, {bind_failed, "lan-game-source-bind-failed"},
        {port_3074, "lan-game-target-port-3074"}, {port_6200, "lan-game-target-port-6200"},
        {port_9103, "lan-game-target-port-9103"}, {other_port, "lan-game-target-other-port"},
        {target_self, "lan-game-target-self"}, {target_peer, "lan-game-target-configured-peer"},
        {target_loopback, "lan-game-target-loopback"}, {target_other, "lan-game-target-outside-pair"},
        {sent, "lan-game-datagram-sent"}, {pending, "lan-game-datagram-pending"},
        {access_denied, "lan-game-datagram-access-denied"}, {other_error, "lan-game-datagram-other-error"},
        {received, "lan-game-peer-datagram-received"}
    };
    for (const auto& label : labels) if (fresh & label.bit) ReportLocalStatus(label.label);
}
void LanSendResult(const sockaddr* address, int size, bool allowed, bool bound, int result, int error) {
    if (lan_configured.load(std::memory_order_acquire) && !lan_group.load() && address && size >= sizeof(sockaddr_in)
        && address->sa_family == AF_INET) {
        const auto* target = reinterpret_cast<const sockaddr_in*>(address);
        LanObserve(transport_observation::Send(ntohl(target->sin_addr.s_addr), ntohs(target->sin_port),
            ntohl(lan_local.load()), ntohl(lan_peer.load()), allowed, bound, result >= 0,
            error == WSA_IO_PENDING, error == WSAEACCES));
    }
    WSASetLastError(error);
}
int WSAAPI LanRecvFrom(SOCKET socket, char* buffer, int length, int flags, sockaddr* address, int* size) {
    const int result = ::recvfrom(socket, buffer, length, flags, address, size);
    const int error = WSAGetLastError();
    if (result >= 0 && lan_configured.load(std::memory_order_acquire) && address && size
        && *size >= sizeof(sockaddr_in) && address->sa_family == AF_INET) {
        const auto* source = reinterpret_cast<const sockaddr_in*>(address);
        sockaddr_in local{}; int local_size = sizeof(local);
        if (::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &local_size) == 0
            && local_size >= sizeof(local) && local.sin_family == AF_INET
            && transport_observation::Receive(ntohl(source->sin_addr.s_addr), ntohs(source->sin_port),
                ntohs(local.sin_port), ntohl(lan_peer.load()))) LanObserve(transport_observation::received);
    }
    WSASetLastError(error); return result;
}
#endif
#ifdef S7_LOCAL_PAIR_LAB
void LabOnce(unsigned int bit, const char* label);
NETIO_STATUS WINAPI LabUnicastTable(ADDRESS_FAMILY family, PMIB_UNICASTIPADDRESS_TABLE* table) {
    using Original = NETIO_STATUS (WINAPI*)(ADDRESS_FAMILY, PMIB_UNICASTIPADDRESS_TABLE*);
    const auto original = reinterpret_cast<Original>(GetProcAddress(GetModuleHandleW(L"iphlpapi.dll"), "GetUnicastIpAddressTable"));
    if (!original) return ERROR_PROC_NOT_FOUND;
    const auto result = original(family, table);
    const auto error = GetLastError();
    if (result == NO_ERROR && family == AF_INET && table && *table) {
        if (!lab_address_table::Project(*table, local_pair_bind.load())) (*table)->NumEntries = 0;
        else LabOnce(1048576, "lab-unicast-address-projected");
    }
    SetLastError(error); return result;
}
void LabOnce(unsigned int bit, const char* label) {
    static std::atomic<unsigned int> observed{0};
    if (!(observed.fetch_or(bit) & bit)) ReportLocalStatus(label);
}
void LabSendResult(const sockaddr* address, int size, bool allowed, bool bound, int result, int error) {
    if (!allowed) LabOnce(1, "lab-datagram-destination-denied");
    else if (!bound) LabOnce(2, "lab-datagram-source-bind-failed");
    if (address && size >= sizeof(sockaddr_in) && address->sa_family == AF_INET) {
        const auto* target = reinterpret_cast<const sockaddr_in*>(address);
        const auto ip = ntohl(target->sin_addr.s_addr);
        auto port = ntohs(target->sin_port);
        if (port == 3074 || port == 6200 || port == 9103) {
            if (port == 3074) LabOnce(64, "lab-game-send-target-port-3074");
            if (port == 6200) LabOnce(128, "lab-game-send-target-port-6200");
            if (port == 9103) LabOnce(256, "lab-game-send-target-port-9103");
            if (ip == local_pair_bind.load()) LabOnce(512, "lab-game-send-target-self");
            else if (ip == 0x7f000002u || ip == 0x7f000003u) LabOnce(1024, "lab-game-send-target-other-slot");
            else if ((ip >> 24) == 127) LabOnce(2048, "lab-game-send-target-other-loopback");
            else LabOnce(4096, "lab-game-send-target-nonloopback");
            if (result >= 0) LabOnce(8192, "lab-game-datagram-sent");
            else if (error == WSAEACCES) LabOnce(16384, "lab-game-datagram-access-denied");
            else if (error == WSA_IO_PENDING) LabOnce(32768, "lab-game-datagram-pending");
            else LabOnce(65536, "lab-game-datagram-send-other-error");
            if (port == 3074 && !allowed) {
                if ((ip & 255u) == 127u) LabOnce(131072, "lab-join-target-reversed-loopback");
                else if ((ip >> 24) == 10u || (ip >> 20) == 0xac1u || (ip >> 16) == 0xc0a8u)
                    LabOnce(262144, "lab-join-target-private-interface");
                else LabOnce(524288, "lab-join-target-other-nonloopback");
            }
        }
        if ((ip == 0x7f000002u || ip == 0x7f000003u) && ntohs(target->sin_port) == 3074) {
            if (result >= 0) LabOnce(4, "lab-game-peer-datagram-sent");
            else if (error == WSA_IO_PENDING) LabOnce(8, "lab-game-peer-datagram-pending");
            else LabOnce(16, "lab-game-peer-datagram-send-failed");
        }
    }
    WSASetLastError(error);
}
int WSAAPI LabRecvFrom(SOCKET socket, char* buffer, int length, int flags, sockaddr* address, int* size) {
    int result = ::recvfrom(socket, buffer, length, flags, address, size);
    int error = WSAGetLastError();
    if (result >= 0 && address && size && *size >= sizeof(sockaddr_in) && address->sa_family == AF_INET) {
        const auto* source = reinterpret_cast<const sockaddr_in*>(address);
        auto ip = ntohl(source->sin_addr.s_addr);
        if ((ip == 0x7f000002u || ip == 0x7f000003u) && ntohs(source->sin_port) == 3074)
            LabOnce(32, "lab-game-peer-datagram-received");
    }
    WSASetLastError(error); return result;
}
#endif
int DenySocket() { WSASetLastError(WSAEACCES); return SOCKET_ERROR; }
bool BindLocalSender(SOCKET socket, const sockaddr* destination) {
    sockaddr_storage local{}; int length = sizeof(local);
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
        if (IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&local), length)) return true;
        // An already-bound wildcard/nonlocal socket cannot be safely rebound.
        return false;
    }
    if (WSAGetLastError() != WSAEINVAL || !destination) return false;
    if (destination->sa_family == AF_INET) {
        sockaddr_in address{}; address.sin_family = AF_INET;
#ifdef S7_LOCAL_PAIR_LAB
        address.sin_addr.s_addr = LocalBindAddress();
#else
        const auto* target = reinterpret_cast<const sockaddr_in*>(destination);
        address.sin_addr.s_addr = (ntohl(target->sin_addr.s_addr) >> 24) == 127
            ? htonl(INADDR_LOOPBACK) : LocalBindAddress();
#endif
        return ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }
    if (destination->sa_family == AF_INET6) {
        sockaddr_in6 address{}; address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_loopback;
        return ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }
    return false;
}
bool PeerAllowed(SOCKET socket) {
    sockaddr_storage peer{}; int size = sizeof(peer);
    return getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &size) == 0
        && IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&peer), size);
}
// Verified HE path E7E457 -> E7E188: event-based WSARecvFrom (no callback),
// followed by WSAGetOverlappedResult even for immediate success. Payload/source
// consumption follows successful GetResult only. We retain metadata, never own
// or replace kernel buffers/OVERLAPPED and never infer cancellation on close.
struct ReceiveOperation {
    SOCKET socket = INVALID_SOCKET;
    LPWSAOVERLAPPED overlapped = nullptr;
    std::array<WSABUF, 16> buffers{};
    DWORD count = 0, capacity = 0;
    sockaddr* source = nullptr;
    int* source_size = nullptr;
    int source_capacity = 0;
    uint64_t generation = 0;
    bool active = false, pending = false, consumed = false, receiving = true, submitted = false;
};
SRWLOCK receive_lock = SRWLOCK_INIT;
std::array<ReceiveOperation, 64> receives;
uint64_t receive_generation = 0;
bool ReceiveArguments(ReceiveOperation& operation, SOCKET socket, LPWSABUF buffers, DWORD count,
                      sockaddr* source, int* source_size, LPWSAOVERLAPPED overlapped) {
    if (!buffers || !count || count > operation.buffers.size() || !source || !source_size
        || *source_size < static_cast<int>(sizeof(sockaddr_in)) || *source_size > static_cast<int>(sizeof(sockaddr_storage))) return false;
    operation.socket = socket; operation.overlapped = overlapped;
    operation.source = source; operation.source_size = source_size; operation.source_capacity = *source_size;
    for (DWORD i = 0; i < count; ++i) {
        if (!buffers[i].buf || buffers[i].len > 65536 - operation.capacity) return false;
        operation.buffers[i] = buffers[i]; operation.capacity += buffers[i].len;
    }
    operation.count = count;
    return operation.capacity != 0;
}
bool TrackReceive(ReceiveOperation& operation) {
    AcquireSRWLockExclusive(&receive_lock);
    ReceiveOperation* available = nullptr;
    for (auto& item : receives) {
        if (item.active && item.overlapped == operation.overlapped) {
            // Native send completion E83AF0 does not call GetResult. The same
            // persistent request is rearmed after its event/Immediate result,
            // without resetting OVERLAPPED.Internal (E7E4D0/E8430B). Inspect
            // only the live pointer supplied to this new call, never old
            // pointers while scanning for an evictable slot. Receives still
            // require their separate GetResult source/expiry gate.
            const bool completed_send = !item.receiving && item.submitted && item.socket == operation.socket
                && HasOverlappedIoCompleted(operation.overlapped);
            if ((item.pending || !item.consumed) && !completed_send) {
                ReleaseSRWLockExclusive(&receive_lock); return false;
            }
            available = &item; break;
        }
        if (!available && (!item.active || item.consumed)) available = &item;
    }
    if (available) {
        operation.active = true; operation.pending = true;
        operation.generation = ++receive_generation;
        *available = operation;
    }
    ReleaseSRWLockExclusive(&receive_lock);
    return available != nullptr;
}
bool FindReceive(SOCKET socket, LPWSAOVERLAPPED overlapped, ReceiveOperation& operation) {
    bool found = false;
    AcquireSRWLockShared(&receive_lock);
    for (const auto& item : receives) if (item.active && (socket == INVALID_SOCKET || item.socket == socket) && item.overlapped == overlapped) {
        operation = item; found = true; break;
    }
    ReleaseSRWLockShared(&receive_lock);
    return found;
}
void FinishReceive(const ReceiveOperation& operation, bool erase, bool consumed = false) {
    AcquireSRWLockExclusive(&receive_lock);
    for (auto& item : receives) if (item.active && item.generation == operation.generation) {
        if (erase) item = {};
        else { item.pending = false; item.consumed = consumed; item.submitted = true; }
        break;
    }
    ReleaseSRWLockExclusive(&receive_lock);
}
void MarkPendingSubmitted(const ReceiveOperation& operation) {
    AcquireSRWLockExclusive(&receive_lock);
    for (auto& item : receives) if (item.active && item.generation == operation.generation) {
        item.submitted = true; break;
    }
    ReleaseSRWLockExclusive(&receive_lock);
}
bool ReceiveSourceAllowed(const ReceiveOperation& operation, DWORD transferred) {
    return transferred <= operation.capacity && operation.source_size && *operation.source_size <= operation.source_capacity
        && IsOfflineEndpointAllowed(operation.source, *operation.source_size);
}
void EraseReceivedBytes(const ReceiveOperation& operation) {
    for (DWORD i = 0; i < operation.count; ++i) if (operation.buffers[i].len)
        SecureZeroMemory(operation.buffers[i].buf, operation.buffers[i].len);
    SecureZeroMemory(operation.source, static_cast<size_t>(operation.source_capacity));
    *operation.source_size = 0;
}
int WSAAPI GuardWSARecvFrom(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                          sockaddr* source, int* source_size, LPWSAOVERLAPPED overlapped,
                          LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (!lan_group.load(std::memory_order_acquire))
        return ::WSARecvFrom(socket, buffers, count, received, flags, source, source_size, overlapped, completion);
    // Completion routines would publish results outside the verified result API.
    if (completion) { WSASetLastError(WSAEOPNOTSUPP); return SOCKET_ERROR; }
    ReceiveOperation operation;
    if (!received || !flags || !ReceiveArguments(operation, socket, buffers, count, source, source_size, overlapped)) {
        WSASetLastError(WSAEFAULT); return SOCKET_ERROR;
    }
    if (overlapped && !TrackReceive(operation)) { WSASetLastError(WSAENOBUFS); return SOCKET_ERROR; }
    const int result = ::WSARecvFrom(socket, buffers, count, received, flags, source, source_size, overlapped, completion);
    const int error = WSAGetLastError();
    if (result == 0) {
        if (overlapped) FinishReceive(operation, false);
        if (!ReceiveSourceAllowed(operation, *received)) {
            EraseReceivedBytes(operation); *received = 0;
            if (overlapped) FinishReceive(operation, false, true);
            return DenySocket();
        }
    } else if (overlapped && error != WSA_IO_PENDING) FinishReceive(operation, true);
    WSASetLastError(error); return result;
}
BOOL WSAAPI GuardWSAGetOverlappedResult(SOCKET socket, LPWSAOVERLAPPED overlapped, LPDWORD transferred, BOOL wait, LPDWORD flags) {
    ReceiveOperation operation;
    const bool group = lan_group.load(std::memory_order_acquire);
    const bool tracked = group && FindReceive(INVALID_SOCKET, overlapped, operation);
    // Only requests submitted through our receive/send boundary are known.
    if (group && (!tracked || operation.socket != socket)) { WSASetLastError(WSAEINVAL); return FALSE; }
    const BOOL result = ::WSAGetOverlappedResult(socket, overlapped, transferred, wait, flags);
    const int error = WSAGetLastError();
    if (tracked) {
        if (result) {
            if (operation.receiving && (!transferred || !ReceiveSourceAllowed(operation, *transferred))) {
                EraseReceivedBytes(operation);
                if (transferred) *transferred = 0;
                FinishReceive(operation, false, true);
                WSASetLastError(WSAEACCES); return FALSE;
            }
            FinishReceive(operation, false, true);
        } else if (error != WSA_IO_INCOMPLETE && error != WSA_IO_PENDING) FinishReceive(operation, true);
    }
    WSASetLastError(error); return result;
}
int WSAAPI GuardCloseSocket(SOCKET socket) {
    const int result = ::closesocket(socket), error = WSAGetLastError();
    if (result == 0 && lan_group.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&receive_lock);
        for (auto& item : receives) if (item.active && item.socket == socket) item = {};
        ReleaseSRWLockExclusive(&receive_lock);
        // Only our pointer metadata was discarded. Original kernel operations
        // and the game's cancellation/destruction contract remain unchanged.
    }
    WSASetLastError(error); return result;
}
int WSAAPI GuardRecvFrom(SOCKET socket, char* buffer, int length, int flags, sockaddr* address, int* size) {
    if (!lan_group.load(std::memory_order_acquire)) {
#ifdef S7_LOCAL_PAIR_LAB
        return LabRecvFrom(socket, buffer, length, flags, address, size);
#elif defined(S7_LOCAL_NETWORK_PUMP)
        return LanRecvFrom(socket, buffer, length, flags, address, size);
#else
        return ::recvfrom(socket, buffer, length, flags, address, size);
#endif
    }
    sockaddr_storage source{}; int source_size = sizeof(source);
    if (address && (!size || *size < static_cast<int>(sizeof(sockaddr_in)))) {
        WSASetLastError(WSAEFAULT); return SOCKET_ERROR;
    }
    const int result = ::recvfrom(socket, buffer, length, flags, reinterpret_cast<sockaddr*>(&source), &source_size);
    const int error = WSAGetLastError();
    if (result >= 0) {
        if (!IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&source), source_size)) {
            if (buffer && result > 0) SecureZeroMemory(buffer, static_cast<size_t>(result));
            return DenySocket();
        }
        if (address) {
            if (*size < source_size) { if (buffer && result > 0) SecureZeroMemory(buffer, static_cast<size_t>(result)); WSASetLastError(WSAEFAULT); return SOCKET_ERROR; }
            memcpy(address, &source, source_size); *size = source_size;
        }
    }
    WSASetLastError(error); return result;
}
int WSAAPI GuardRecv(SOCKET socket, char* buffer, int length, int flags) {
    if (!lan_group.load(std::memory_order_acquire)) return ::recv(socket, buffer, length, flags);
    int type = 0, size = sizeof(type);
    if (::getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) != 0) return SOCKET_ERROR;
    if (type == SOCK_DGRAM) return GuardRecvFrom(socket, buffer, length, flags, nullptr, nullptr);
    if (!PeerAllowed(socket)) return DenySocket();
    const int result = ::recv(socket, buffer, length, flags), error = WSAGetLastError();
    if (result >= 0 && !PeerAllowed(socket)) {
        if (buffer && result > 0) SecureZeroMemory(buffer, static_cast<size_t>(result));
        return DenySocket();
    }
    WSASetLastError(error); return result;
}
int WSAAPI GuardConnect(SOCKET socket, const sockaddr* address, int length) {
    return IsOfflineEndpointAllowed(address, length) && BindLocalSender(socket, address)
        ? ::connect(socket, address, length) : DenySocket();
}
int WSAAPI GuardSend(SOCKET socket, const char* buffer, int length, int flags) {
    return PeerAllowed(socket) ? ::send(socket, buffer, length, flags) : DenySocket();
}
int WSAAPI GuardSendTo(SOCKET socket, const char* buffer, int length, int flags, const sockaddr* address, int size) {
#if defined(S7_LOCAL_PAIR_LAB) || defined(S7_LOCAL_NETWORK_PUMP)
    bool allowed = address ? IsOfflineEndpointAllowed(address, size) : PeerAllowed(socket);
    bool bound = allowed && (!address || BindLocalSender(socket, address));
    int result = allowed && bound ? ::sendto(socket, buffer, length, flags, address, size) : DenySocket();
    int error = WSAGetLastError();
#ifdef S7_LOCAL_PAIR_LAB
    LabSendResult(address, size, allowed, bound, result, error);
#endif
#ifdef S7_LOCAL_NETWORK_PUMP
    LanSendResult(address, size, allowed, bound, result, error);
#endif
    return result;
#else
    return (address ? IsOfflineEndpointAllowed(address, size) && BindLocalSender(socket, address) : PeerAllowed(socket))
        ? ::sendto(socket, buffer, length, flags, address, size) : DenySocket();
#endif
}
int WSAAPI GuardWSASendTo(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags,
    const sockaddr* address, int size, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    const bool tracked = overlapped && lan_group.load(std::memory_order_acquire);
    ReceiveOperation operation;
    if (tracked) {
        if (completion) { WSASetLastError(WSAEOPNOTSUPP); return SOCKET_ERROR; }
        operation.socket = socket; operation.overlapped = overlapped; operation.receiving = false;
        if (!TrackReceive(operation)) { WSASetLastError(WSAENOBUFS); return SOCKET_ERROR; }
    }
    bool allowed = address ? IsOfflineEndpointAllowed(address, size) : PeerAllowed(socket);
    bool bound = allowed && (!address || BindLocalSender(socket, address));
    int result = allowed && bound ? ::WSASendTo(socket, buffers, count, sent, flags, address, size, overlapped, completion) : DenySocket();
    int error = WSAGetLastError();
    if (tracked) {
        if (result == 0) FinishReceive(operation, false, true);
        else if (error != WSA_IO_PENDING) FinishReceive(operation, true);
        else MarkPendingSubmitted(operation);
    }
#ifdef S7_LOCAL_PAIR_LAB
    LabSendResult(address, size, allowed, bound, result, error);
#endif
#ifdef S7_LOCAL_NETWORK_PUMP
    LanSendResult(address, size, allowed, bound, result, error);
#endif
    WSASetLastError(error); return result;
}
int WSAAPI GuardBind(SOCKET socket, const sockaddr* address, int length) {
    if (!address) return DenySocket();
    // Native wildcard listeners become local listeners, preserving their port.
    if (length >= sizeof(sockaddr_in) && address->sa_family == AF_INET) {
        sockaddr_in local = *reinterpret_cast<const sockaddr_in*>(address);
        if (local.sin_addr.s_addr == INADDR_ANY) local.sin_addr.s_addr = LocalBindAddress();
#ifdef S7_LOCAL_PAIR_LAB
        if (local.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) local.sin_addr.s_addr = LocalBindAddress();
#endif
        return IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&local), sizeof(local))
            ? ::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) : DenySocket();
    }
    if (length >= sizeof(sockaddr_in6) && address->sa_family == AF_INET6) {
        sockaddr_in6 local = *reinterpret_cast<const sockaddr_in6*>(address);
        if (IN6_IS_ADDR_UNSPECIFIED(&local.sin6_addr)) local.sin6_addr = in6addr_loopback;
        return IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&local), sizeof(local))
            ? ::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) : DenySocket();
    }
    return DenySocket();
}
SOCKET WSAAPI GuardAccept(SOCKET socket, sockaddr* address, int* length) {
    SOCKET accepted = ::accept(socket, address, length);
    if (accepted != INVALID_SOCKET && !PeerAllowed(accepted)) { closesocket(accepted); DenySocket(); return INVALID_SOCKET; }
    return accepted;
}
bool NumericLocal(const char* host, int family) {
    if (!host) return true;
    if (_stricmp(host, "localhost") == 0) return true;
    sockaddr_in v4{}; v4.sin_family = AF_INET;
    sockaddr_in6 v6{}; v6.sin6_family = AF_INET6;
    return (family != AF_INET6 && InetPtonA(AF_INET, host, &v4.sin_addr) == 1
        && IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&v4), sizeof(v4)))
        || (family != AF_INET && InetPtonA(AF_INET6, host, &v6.sin6_addr) == 1
        && IsOfflineEndpointAllowed(reinterpret_cast<sockaddr*>(&v6), sizeof(v6)));
}
int WSAAPI GuardAddrInfo(PCSTR node, PCSTR service, const ADDRINFOA* hints, PADDRINFOA* result) {
    if (result) *result = nullptr;
    ADDRINFOA local = hints ? *hints : ADDRINFOA{};
    if (!NumericLocal(node, local.ai_family)) { WSASetLastError(WSAHOST_NOT_FOUND); return EAI_NONAME; }
    local.ai_flags |= AI_NUMERICHOST;
    const char* numeric = (!node || _stricmp(node, "localhost") == 0) ? (local.ai_family == AF_INET6 ? "::1" : "127.0.0.1") : node;
    return ::getaddrinfo(numeric, service, &local, result);
}
hostent* LocalHost(unsigned long numeric = htonl(INADDR_LOOPBACK)) {
    static thread_local char name[] = "localhost";
    static thread_local char address[4] = {127,0,0,1};
    static thread_local char* addresses[] = {address, nullptr};
    static thread_local char* aliases[] = {nullptr};
    static thread_local hostent value = {name, aliases, AF_INET, 4, addresses};
    memcpy(address, &numeric, 4);
    return &value;
}
hostent* WSAAPI GuardHostByName(const char* name) {
    if (!name || !NumericLocal(name, AF_INET)) { WSASetLastError(WSAHOST_NOT_FOUND); return nullptr; }
    IN_ADDR numeric{};
    if (_stricmp(name, "localhost") == 0) numeric.s_addr = htonl(INADDR_LOOPBACK);
    else if (InetPtonA(AF_INET, name, &numeric) != 1) return nullptr;
    return LocalHost(numeric.s_addr);
}
hostent* WSAAPI GuardHostByAddr(const char* address, int length, int family) {
    if (address && family == AF_INET && length == 4) {
        unsigned long numeric = 0; memcpy(&numeric, address, 4);
        if (AllowedV4(numeric)) return LocalHost(numeric);
    }
    WSASetLastError(WSAHOST_NOT_FOUND); return nullptr;
}
int WSAAPI GuardHostName(char* buffer, int length) {
    if (lan_configured.load(std::memory_order_acquire)) {
        IN_ADDR numeric{}; numeric.s_addr = lan_local.load();
        if (!buffer || length < INET_ADDRSTRLEN) { WSASetLastError(WSAEFAULT); return SOCKET_ERROR; }
        return InetNtopA(AF_INET, &numeric, buffer, length) ? 0 : SOCKET_ERROR;
    }
    if (!buffer || length < 10) { WSASetLastError(WSAEFAULT); return SOCKET_ERROR; }
    memcpy(buffer, "localhost", 10); return 0;
}
int WSAAPI GuardNameInfo(const sockaddr* address, socklen_t length, char* host, DWORD hostSize,
    char* service, DWORD serviceSize, int flags) {
    if (!IsOfflineEndpointAllowed(address, length)) return EAI_NONAME;
    return ::getnameinfo(address, length, host, hostSize, service, serviceSize, flags | NI_NUMERICHOST | NI_NUMERICSERV);
}
int WSAAPI GuardIoctl(SOCKET socket, DWORD code, LPVOID input, DWORD inSize, LPVOID output, DWORD outSize,
    LPDWORD returned, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (code == SIO_GET_EXTENSION_FUNCTION_POINTER) { WSASetLastError(WSAEOPNOTSUPP); return SOCKET_ERROR; }
    return ::WSAIoctl(socket, code, input, inSize, output, outSize, returned, overlapped, completion);
}
bool HttpLocal(const wchar_t* host) {
    if (!host) return false;
    return _wcsicmp(host, L"localhost") == 0 || wcscmp(host, L"127.0.0.1") == 0 || wcscmp(host, L"::1") == 0;
}
HINTERNET WINAPI GuardHttpOpen(LPCWSTR agent, DWORD, LPCWSTR, LPCWSTR, DWORD flags) {
    return ::WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, flags);
}
HINTERNET WINAPI GuardHttpConnect(HINTERNET session, LPCWSTR host, INTERNET_PORT port, DWORD reserved) {
    if (!HttpLocal(host)) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return ::WinHttpConnect(session, wcscmp(host, L"::1") == 0 ? L"::1" : L"127.0.0.1", port, reserved);
}
HINTERNET WINAPI GuardHttpRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR object, LPCWSTR version,
    LPCWSTR referrer, LPCWSTR* accept, DWORD flags) {
    if (object && (wcsstr(object, L"://") || wcsncmp(object, L"//", 2) == 0)) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    HINTERNET request = ::WinHttpOpenRequest(connection, verb, object, version, referrer, accept, flags);
    if (request) {
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!::WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never))) {
            ::WinHttpCloseHandle(request); return nullptr;
        }
    }
    return request;
}
BOOL WINAPI GuardHttpOption(HINTERNET handle, DWORD option, LPVOID value, DWORD size) {
    if (option == WINHTTP_OPTION_PROXY) {
        WINHTTP_PROXY_INFO direct{WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr};
        return ::WinHttpSetOption(handle, option, &direct, sizeof(direct));
    }
    if (option == WINHTTP_OPTION_REDIRECT_POLICY) {
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        return ::WinHttpSetOption(handle, option, &never, sizeof(never));
    }
    return ::WinHttpSetOption(handle, option, value, size);
}
HINSTANCE WINAPI GuardShellA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT) { SetLastError(ERROR_ACCESS_DENIED); return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED); }
HINSTANCE WINAPI GuardShellW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT) { SetLastError(ERROR_ACCESS_DENIED); return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED); }

struct Binding { const char* dll; const char* name; void* replacement; };
#define BIND(dll, name, replacement) {dll, name, reinterpret_cast<void*>(replacement)}
const Binding bindings[] = {
#ifdef S7_LOCAL_PAIR_LAB
    BIND("IPHLPAPI.DLL", "GetUnicastIpAddressTable", LabUnicastTable),
#endif
    BIND("WS2_32.dll", "recvfrom", GuardRecvFrom), BIND("WS2_32.dll", "recv", GuardRecv),
    BIND("WS2_32.dll", "WSARecvFrom", GuardWSARecvFrom), BIND("WS2_32.dll", "WSAGetOverlappedResult", GuardWSAGetOverlappedResult),
    BIND("WS2_32.dll", "closesocket", GuardCloseSocket),
    BIND("WS2_32.dll", "connect", GuardConnect), BIND("WS2_32.dll", "send", GuardSend),
    BIND("WS2_32.dll", "sendto", GuardSendTo), BIND("WS2_32.dll", "WSASendTo", GuardWSASendTo),
    BIND("WS2_32.dll", "bind", GuardBind), BIND("WS2_32.dll", "accept", GuardAccept),
    BIND("WS2_32.dll", "getaddrinfo", GuardAddrInfo), BIND("WS2_32.dll", "gethostbyname", GuardHostByName),
    BIND("WS2_32.dll", "gethostbyaddr", GuardHostByAddr), BIND("WS2_32.dll", "gethostname", GuardHostName),
    BIND("WS2_32.dll", "getnameinfo", GuardNameInfo), BIND("WS2_32.dll", "WSAIoctl", GuardIoctl),
    BIND("WINHTTP.dll", "WinHttpOpen", GuardHttpOpen), BIND("WINHTTP.dll", "WinHttpConnect", GuardHttpConnect),
    BIND("WINHTTP.dll", "WinHttpOpenRequest", GuardHttpRequest), BIND("WINHTTP.dll", "WinHttpSetOption", GuardHttpOption),
    BIND("SHELL32.dll", "ShellExecuteA", GuardShellA), BIND("SHELL32.dll", "ShellExecuteW", GuardShellW)
};
struct Patch { void** slot; void* original; void* replacement; };
bool BoundedName(const unsigned char* base, DWORD image_size, DWORD offset) {
    return offset < image_size && memchr(base + offset, 0, image_size - offset) != nullptr;
}
bool Collect(HMODULE module, std::vector<Patch>& patches) {
    auto* base = reinterpret_cast<unsigned char*>(module);
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(*dos) || dos->e_lfanew > 4096) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || directory.VirtualAddress >= imageSize || directory.Size > imageSize - directory.VirtualAddress) return false;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (size_t index = 0; index < directory.Size / sizeof(*imports) && imports[index].Name; ++index) {
        const auto& entry = imports[index];
        if (!BoundedName(base, imageSize, entry.Name) || !entry.OriginalFirstThunk || entry.OriginalFirstThunk >= imageSize || !entry.FirstThunk || entry.FirstThunk >= imageSize) return false;
        const char* dll = reinterpret_cast<const char*>(base + entry.Name);
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + entry.OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + entry.FirstThunk);
        const size_t count = (imageSize - (entry.OriginalFirstThunk > entry.FirstThunk ? entry.OriginalFirstThunk : entry.FirstThunk)) / sizeof(*names);
        for (size_t item = 0; item < count && names[item].u1.AddressOfData; ++item) {
            const bool ordinal = IMAGE_SNAP_BY_ORDINAL32(names[item].u1.Ordinal) != 0;
            const char* name = nullptr;
            if (!ordinal) {
                if (names[item].u1.AddressOfData > imageSize - sizeof(IMAGE_IMPORT_BY_NAME)) return false;
                if (!BoundedName(base, imageSize, names[item].u1.AddressOfData + offsetof(IMAGE_IMPORT_BY_NAME, Name))) return false;
                name = reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[item].u1.AddressOfData)->Name);
            }
            // Windows can resolve legacy Winsock imports by ordinal even when
            // the on-disk import library named them. Match the system export,
            // never assume all in-memory imports still have names.
            for (const auto& binding : bindings) if (_stricmp(dll, binding.dll) == 0) {
                auto** slot = reinterpret_cast<void**>(&slots[item].u1.Function);
                HMODULE library = GetModuleHandleA(binding.dll);
                void* expected = reinterpret_cast<void*>(GetProcAddress(library, binding.name));
                if (ordinal) {
                    void* resolved = reinterpret_cast<void*>(GetProcAddress(library,
                        MAKEINTRESOURCEA(IMAGE_ORDINAL32(names[item].u1.Ordinal))));
                    if (resolved != expected) continue;
                } else if (strcmp(name, binding.name) != 0) continue;
                if (!expected || *slot != expected) return false;
                patches.push_back({slot, expected, binding.replacement});
#ifdef S7_GUARD_TEST_DIAGNOSTICS
                std::printf("Collect %s %p -> %p\n", binding.name, *slot, binding.replacement);
#endif
            }
        }
    }
    return true;
}
bool Exchange(const Patch& patch, bool install) {
    DWORD previous = 0;
    if (!VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
    void* expected = install ? patch.original : patch.replacement;
    void* replacement = install ? patch.replacement : patch.original;
    bool matched = InterlockedCompareExchangePointer(patch.slot, replacement, expected) == expected;
    DWORD discarded = 0;
    if (!VirtualProtect(patch.slot, sizeof(void*), previous, &discarded)) {
        TerminateProcess(GetCurrentProcess(), 0xE0537003u);
        __fastfail(7);
    }
    return matched;
}
}

#ifdef S7_GUARD_TESTING
// Deterministic status-boundary fixture: no kernel work is submitted or changed.
bool MarkLanGroupTestSendPending(SOCKET socket, LPWSAOVERLAPPED overlapped) {
    bool changed = false;
    AcquireSRWLockExclusive(&receive_lock);
    for (auto& item : receives) if (item.active && !item.receiving && item.submitted
        && item.socket == socket && item.overlapped == overlapped && HasOverlappedIoCompleted(overlapped)) {
        item.pending = true; item.consumed = false; item.submitted = true;
        changed = true; break;
    }
    ReleaseSRWLockExclusive(&receive_lock);
    return changed;
}
#endif

bool InstallOfflineNetworkGuard() {
    static bool installed = false;
    if (installed) return true;
    std::vector<Patch> patches;
    if (!Collect(GetModuleHandleW(nullptr), patches)) return false;
    HMODULE engine = GetModuleHandleW(L"vBase90.dll");
    if (engine && !Collect(engine, patches)) return false;
    if (patches.empty()) return false;
    for (size_t index = 0; index < patches.size(); ++index) if (!Exchange(patches[index], true)) {
        // A failed exchange has not changed that entry. Roll back only the
        // completed exchanges; never continue after an uncertain rollback.
        for (size_t undo = index; undo > 0; --undo) if (!Exchange(patches[undo - 1], false)) {
            TerminateProcess(GetCurrentProcess(), 0xE0537003u);
            __fastfail(7);
        }
        return false;
    }
    installed = true;
    return true;
}
