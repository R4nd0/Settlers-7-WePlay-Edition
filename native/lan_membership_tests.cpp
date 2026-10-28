#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include "lan_membership_file.h"
#include "runtime_config.h"
#include <cstdio>
#include <thread>

extern "C" __declspec(dllimport) bool __cdecl S7TestConfigureGroup(const char*, const char*, const wchar_t*);
extern "C" __declspec(dllimport) bool __cdecl S7TestStartGroup();
extern "C" __declspec(dllimport) bool __cdecl S7TestStopGroup();
extern "C" __declspec(dllimport) void __cdecl S7TestReadGate(HANDLE, HANDLE, DWORD);
extern "C" __declspec(dllimport) unsigned long __cdecl S7TestReadCount();
extern "C" __declspec(dllimport) bool __cdecl S7TestMarkSendPending(SOCKET, LPWSAOVERLAPPED);
extern "C" __declspec(dllimport) bool __cdecl S7TestEndpoint(const sockaddr*, int);
extern "C" __declspec(dllimport) bool __cdecl S7TestInstallGuard();
static int checks = 0, failures = 0;
static void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; printf("FAIL %s\n", label); } }
static void CALLBACK UnexpectedCompletion(DWORD, DWORD, LPWSAOVERLAPPED, DWORD) { Check(false, "unsupported callback must never run"); }
static bool Allowed(uint32_t ip) {
    sockaddr_in endpoint{}; endpoint.sin_family = AF_INET; endpoint.sin_addr.s_addr = htonl(ip);
    return S7TestEndpoint(reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
}
static bool WaitAllowed(uint32_t ip, bool expected) {
    const auto deadline = GetTickCount64() + 3000;
    while (Allowed(ip) != expected && GetTickCount64() < deadline) Sleep(10);
    return Allowed(ip) == expected;
}
static bool WaitRead(unsigned long previous) {
    const auto deadline = GetTickCount64() + 3000;
    while (S7TestReadCount() == previous && GetTickCount64() < deadline) Sleep(10);
    return S7TestReadCount() != previous;
}
static std::string Record(uint64_t expiry, const std::string& peers) {
    return "format=s7-lan-peers-v1\nlocal=192.168.10.10\nexpires_tick_ms=" + std::to_string(expiry) + "\n" + peers;
}
static bool Replace(const std::wstring& root, const std::string& content) {
    const auto temporary = root + L"\\lan-peers.new", target = root + L"\\lan-peers.ini";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool okay = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) && written == content.size();
    CloseHandle(file);
    return okay && MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
static bool Junction(const std::wstring& link, const std::wstring& target) {
    // Both paths are freshly owned test fixtures. Mount-point reparses need no
    // system configuration or symbolic-link privilege.
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) return false;
    struct JunctionRecord {
        DWORD tag; WORD data_length, reserved;
        WORD substitute_offset, substitute_length, print_offset, print_length;
        wchar_t paths[MAX_PATH * 2 + 8];
    } record{};
    const std::wstring substitute = L"\\??\\" + target;
    const size_t chars = substitute.size() + target.size() + 2;
    bool okay = chars <= sizeof(record.paths) / sizeof(wchar_t);
    if (okay) {
        record.tag = IO_REPARSE_TAG_MOUNT_POINT;
        record.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
        record.print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
        record.print_length = static_cast<WORD>(target.size() * sizeof(wchar_t));
        record.data_length = static_cast<WORD>(8 + chars * sizeof(wchar_t));
        memcpy(record.paths, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
        memcpy(record.paths + substitute.size() + 1, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
        DWORD returned = 0;
        okay = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, &record, 8 + record.data_length,
            nullptr, 0, &returned, nullptr) != 0;
    }
    CloseHandle(directory); return okay;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    constexpr uint32_t local = 0xc0a80a0a, coordinator = 0xc0a80a14, a = 0xc0a80a15, b = 0xc0a80a16, c = 0xc0a80a17;
    const std::string three = "peer_1=192.168.10.21\npeer_2=192.168.10.22\npeer_3=192.168.10.23\n";
    using namespace lan_membership;
    Snapshot snapshot;
    auto parse = [&](const std::string& input) { return Parse(input.data(), input.size(), local, coordinator, 1000, snapshot); };
    Check(parse(Record(16000, three)) && snapshot.count == 3 && Contains(snapshot, a, 15999), "three exact remote peers");
    Check(!Contains(snapshot, a, 16000), "expires at precise monotonic tick");
    Check(!Contains(snapshot, 0xc0a80a18, 1001), "unlisted same subnet denied");
    Check(parse(Record(31000, "")), "maximum TTL inclusive");
    for (auto expiry : {0ULL, 1000ULL, 31001ULL, 18446744073709551615ULL}) Check(!parse(Record(expiry, three)) && !snapshot.count, "invalid TTL resets snapshot");
    const char* bad_peers[] = {"peer_1=8.8.8.8\n", "peer_1=127.0.0.1\n", "peer_1=192.168.10.10\n", "peer_2=192.168.10.21\n",
        "peer_1=192.168.10.21\npeer_2=192.168.10.21\n", "peer_1=192.168.010.21\n", "peer_1=192.168.10.255\n", "peer_1=192.168.10.0\n",
        "peer_1=192.168.10.21", "unknown=1\n", "peer_1=192.168.10.21\n\n", "peer_1=192.168.10.21\rX\n"};
    for (const char* bad : bad_peers) Check(!parse(Record(16000, bad)), "malformed or unauthorized peer record");
    std::string seven;
    for (int i = 1; i <= 7; ++i) seven += "peer_" + std::to_string(i) + "=192.168.10." + std::to_string(20 + i) + "\n";
    Check(!parse(Record(16000, seven)), "remote coordinator reserves eighth total slot");
    seven.replace(seven.find("192.168.10.21"), 13, "192.168.10.20");
    Check(parse(Record(16000, seven)), "eight total distinct members including coordinator");
    Check(!parse(Record(16000, seven + "peer_8=192.168.10.29\n")), "ninth total member denied");
    Check(!parse(std::string(4097, 'a')), "oversized list rejected");
    auto embedded = Record(16000, three); embedded[10] = 0;
    Check(!parse(embedded), "embedded NUL rejected");
    auto wrong_local = Record(16000, three); wrong_local.replace(wrong_local.find("192.168.10.10"), 13, "192.168.10.11");
    Check(!parse(wrong_local), "wrong local interface rejected");
    auto overflow = Record(16000, three); overflow.replace(overflow.find("16000"), 5, "18446744073709551616");
    Check(!parse(overflow), "decimal overflow rejected");

    size_t size = 0;
    for (const char* name : {"LocalPlayer", "J\xc3\xb6rg", "\xe6\x9d\x8e", "\xf0\x9f\x8e\xae", "abcdefghijklmnopqrstuvwx"})
        Check(ValidLocalDisplayName(name, size), "valid bounded UTF8 name");
    for (const char* name : {"", "abcdefghijklmnopqrstuvwxy", "x\n", "x\t", "\x7f", "\xc2\x85", "\xc0\xaf", "\xed\xa0\x80",
        "\xf4\x90\x80\x80", "\xf0\x80\x80\x80", "\xe2\x80\xa8", "\xe2\x80\xa9", "\xc3", "\x80"})
        Check(!ValidLocalDisplayName(name, size), "invalid UTF8 or control name");
    const std::string base = "format=s7-local-runtime-v1\naddress=127.0.0.1\nudp_port=12345\npublic_key=fixture\nprofile_id=fixture\ndisplay_name=J\xc3\xb6rg\nticket=fixture\n";
    std::map<std::string, std::string> fields;
    auto config = [&](const std::string& value) { return ParseLocalRuntimeConfiguration(value.data(), value.size(), fields); };
    Check(config(base), "only display name accepts UTF8");
    Check(config(base + "connect_probe=1\nlan_group=1\nlan_local=192.168.10.10\n"), "group runtime mode");
    Check(!config(base + "connect_probe=1\nlan_group=1\nlan_local=192.168.10.10\nlan_peer=192.168.10.20\n"), "ambiguous group pair rejected");
    Check(!config(base + "lan_group=1\nlan_local=192.168.10.10\n"), "LAN requires proven connect path");
    Check(!config(base + "connect_probe=1\nlan_group=0\nlan_local=192.168.10.10\n"), "group key is explicit one only");
    Check(!config(base + "lan_local=192.168.10.10\n"), "partial pair rejected");
    auto pair = base; pair.replace(pair.find("127.0.0.1"), 9, "192.168.10.20");
    Check(config(pair + "connect_probe=1\nlan_local=192.168.10.10\nlan_peer=192.168.10.20\n"), "legacy pair preserved");
    auto nonascii = base; nonascii.replace(nonascii.find("ticket=fixture"), 14, "ticket=\xc3\xb6");
    Check(!config(nonascii), "nonname fields remain ASCII");
    auto nulname = base; nulname.insert(nulname.find("rg\n"), 1, '\0');
    Check(!config(nulname), "embedded name NUL rejected");
    Check(!config(base + "unknown=value\n"), "unknown config key rejected");

    const std::wstring root = argv[1];
    const bool fresh_root = CreateDirectoryW(root.c_str(), nullptr) != 0;
    Check(fresh_root, "fresh private LocalData fixture");
    if (!fresh_root) return 1; // Never replace files in an existing caller path.
    Check(ValidRoot(root.c_str()), "owned absolute LocalData path");
    Check(!ValidRoot((root + L"\\..").c_str()) && !ValidRoot(L"relative\\LocalData"), "traversal and relative roots denied");
    const auto parent = root.substr(0, root.find_last_of(L'\\'));
    const auto ancestor_alias = parent + L"\\ancestor-alias";
    Check(Junction(ancestor_alias, parent), "create owned ancestor junction");
    Check(!ValidRoot((ancestor_alias + L"\\LocalData").c_str()), "ancestor directory reparse rejected by final handle path");
    const auto child = parent + L"\\direct-link";
    Check(CreateDirectoryW(child.c_str(), nullptr) != 0 && Junction(child + L"\\LocalData", root), "create owned direct LocalData junction");
    Check(!ValidRoot((child + L"\\LocalData").c_str()), "direct LocalData reparse rejected");
    Check(RemoveDirectoryW(ancestor_alias.c_str()) != 0 && RemoveDirectoryW((child + L"\\LocalData").c_str()) != 0,
          "remove only owned junctions without traversing target");
    Check(!Read(root, local, coordinator, GetTickCount64(), snapshot), "missing file fails closed");
    Check(Replace(root, Record(GetTickCount64() + 15000, three)), "atomic initial member record");
    Check(!S7TestConfigureGroup("192.168.10.10", "8.8.8.8", root.c_str()), "public coordinator denied");
    Check(S7TestConfigureGroup("192.168.10.10", "192.168.10.20", root.c_str()), "configure bounded group");
    Check(!Allowed(a), "configuration performs no synchronous membership read");
    bool outsider_start = true;
    std::thread outsider([&] { outsider_start = S7TestStartGroup(); }); outsider.join();
    Check(!outsider_start && !S7TestReadCount(), "worker startup confined to configuration thread");
    Check(S7TestStartGroup() && S7TestStartGroup() && WaitAllowed(a, true), "explicit worker startup is idempotent");
    Check(Allowed(local) && Allowed(coordinator) && Allowed(0x7f000001) && Allowed(a) && Allowed(b) && Allowed(c), "local coordinator loopback and three peers admitted");
    Check(!Allowed(0xc0a80a18) && !Allowed(0x08080808), "unknown peer and internet denied");
    Check(!S7TestConfigureGroup("192.168.10.10", "192.168.10.30", root.c_str()), "startup policy immutable");
    Check(Replace(root, Record(GetTickCount64() + 15000, "peer_1=192.168.10.24\n")), "atomic peer switch");
    const auto before_early_read = S7TestReadCount();
    Sleep(100);
    Check(S7TestReadCount() == before_early_read, "worker waits at least 500ms between reads");
    Check(WaitAllowed(0xc0a80a18, true), "worker reads atomic replacement without platform ticks");
    Check(!Allowed(a) && !Allowed(b) && Allowed(0xc0a80a18), "member switch revokes prior peers atomically");
    Check(Replace(root, Record(GetTickCount64() + 1200, three)), "short lease fixture");
    Check(WaitAllowed(a, true), "short lease initially admitted");
    Sleep(1250); Check(!Allowed(a) && Allowed(coordinator) && Allowed(local), "expiry enforced even without update pump");
    Check(Replace(root, Record(GetTickCount64() + 15000, three)), "renew valid list");
    Check(WaitAllowed(a, true), "renewal admits peers");
    Check(Replace(root, "broken\n"), "corrupt replacement fixture");
    Check(WaitAllowed(a, false) && Allowed(coordinator), "corrupt file revokes only dynamic peers");
    Check(DeleteFileW((root + L"\\lan-peers.ini").c_str()) != 0, "remove owned fixture record");
    Check(WaitRead(S7TestReadCount()) && !Allowed(a) && Allowed(local), "missing list stays closed");
    Check(Replace(root, Record(GetTickCount64() + 15000, three)), "hardlink source fixture");
    Check(CreateHardLinkW((root + L"\\alias.ini").c_str(), (root + L"\\lan-peers.ini").c_str(), nullptr) != 0, "create owned hardlink fixture");
    Check(!Read(root, local, coordinator, GetTickCount64(), snapshot), "hardlinked record rejected");
    DeleteFileW((root + L"\\alias.ini").c_str());
    Check(Replace(root, std::string(4097, 'a')) && !Read(root, local, coordinator, GetTickCount64(), snapshot), "oversized file rejected before read");
    WSADATA data{}; Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock synthetic setup");
    Check(S7TestInstallGuard(), "install real import guards in group host");
    SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "group loopback bind");
    int address_length = sizeof(address); getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &address_length);
    DWORD timeout = 1500; setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    char packet[4]{};
    Check(sendto(sender, "abc", 3, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 3
        && recv(receiver, packet, sizeof(packet), 0) == 3 && memcmp(packet, "abc", 3) == 0, "group recv without caller source still checks and accepts loopback");
    Check(Replace(root, Record(GetTickCount64() + 15000, three)), "async membership fixture");
    Check(WaitAllowed(a, true), "async member snapshot ready");
    WSAOVERLAPPED overlap{}; overlap.hEvent = WSACreateEvent();
    sockaddr_storage source{}; int source_size = sizeof(source);
    DWORD received = 0, flags = 0;
    char payload[8]{}; WSABUF buffer{sizeof(payload), payload};
    auto rearm = [&] {
        source = {}; source_size = sizeof(source); received = 0; flags = 0;
        memset(payload, 0x5a, sizeof(payload)); WSAResetEvent(overlap.hEvent);
        return WSARecvFrom(receiver, &buffer, 1, &received, &flags, reinterpret_cast<sockaddr*>(&source), &source_size, &overlap, nullptr);
    };
    auto deliver = [&] {
        Check(sendto(sender, "abc", 3, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 3, "async loopback send");
        Check(WaitForSingleObject(overlap.hEvent, 1500) == WAIT_OBJECT_0, "kernel completion event");
    };
    Check(WSARecvFrom(receiver, &buffer, 1, &received, &flags, reinterpret_cast<sockaddr*>(&source), &source_size, &overlap,
        UnexpectedCompletion) == SOCKET_ERROR && WSAGetLastError() == WSAEOPNOTSUPP, "unverified callback completion path rejected before submission");
    Check(WSARecvFrom(receiver, &buffer, 1, &received, &flags, nullptr, nullptr, &overlap, nullptr) == SOCKET_ERROR
        && WSAGetLastError() == WSAEFAULT, "async reception requires stable explicit source storage");
    WSABUF oversized_buffer{65537, payload};
    Check(WSARecvFrom(receiver, &oversized_buffer, 1, &received, &flags, reinterpret_cast<sockaddr*>(&source), &source_size, &overlap, nullptr)
        == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "oversized receive metadata rejected before kernel buffer access");
    Check(rearm() == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "async pending preserved");
    Check(!WSAGetOverlappedResult(sender, &overlap, &received, FALSE, &flags) && WSAGetLastError() == WSAEINVAL,
          "receive completion is bound to its original socket");
    Check(WSARecvFrom(sender, &buffer, 1, &received, &flags, reinterpret_cast<sockaddr*>(&source), &source_size, &overlap, nullptr)
          == SOCKET_ERROR && WSAGetLastError() == WSAENOBUFS, "pending OVERLAPPED cannot be reused on another socket");
    Check(!WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && WSAGetLastError() == WSA_IO_INCOMPLETE,
          "incomplete preserves pending receive metadata");
    Check(WSARecvFrom(receiver, &buffer, 1, &received, &flags, reinterpret_cast<sockaddr*>(&source), &source_size, &overlap, nullptr)
          == SOCKET_ERROR && WSAGetLastError() == WSAENOBUFS, "active operation cannot be rearmed or evicted");
    deliver();
    Check(WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && received == 3 && memcmp(payload, "abc", 3) == 0,
          "pending receive publishes loopback after result gate");
    Check(rearm() == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "completed same OVERLAPPED can rearm");
    deliver();
    // Test-only mutation of the completed kernel sockaddr exercises policy at
    // the same native GetResult boundary without sending packets on real LAN.
    reinterpret_cast<sockaddr_in*>(&source)->sin_addr.s_addr = htonl(a);
    Check(WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && received == 3, "listed remote source accepted at completion boundary");
    Check(rearm() == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "rearm denied-source fixture");
    deliver(); reinterpret_cast<sockaddr_in*>(&source)->sin_addr.s_addr = htonl(0xc0a80a63);
    Check(!WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && WSAGetLastError() == WSAEACCES && !received
        && !source_size && memcmp(payload, "\0\0\0\0\0\0\0\0", sizeof(payload)) == 0, "unlisted source denied with erased bytes");
    Check(rearm() == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "rearm malformed source fixture");
    deliver(); source_size = sizeof(source) + 1;
    Check(!WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && WSAGetLastError() == WSAEACCES,
          "malformed completed address length denied");
    Check(Replace(root, Record(GetTickCount64() + 1600, three)), "async short lease fixture");
    Check(WaitRead(S7TestReadCount()), "worker publishes short async lease");
    Check(rearm() == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "rearm expiring source fixture");
    deliver(); reinterpret_cast<sockaddr_in*>(&source)->sin_addr.s_addr = htonl(a);
    Sleep(1610);
    Check(!WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && WSAGetLastError() == WSAEACCES && !received,
          "source lease must remain live when completed bytes are published");
    Check(sendto(sender, "abc", 3, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 3, "queue immediate receive");
    fd_set ready; FD_ZERO(&ready); FD_SET(receiver, &ready); timeval ready_timeout{1, 0};
    Check(select(0, &ready, nullptr, nullptr, &ready_timeout) == 1, "packet ready for immediate completion");
    Check(rearm() == 0 && received == 3, "immediate receive success gated");
    Check(WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && received == 3,
          "immediate operation is also gated at GetResult");
    WSAOVERLAPPED send_operation{}; send_operation.hEvent = WSACreateEvent();
    WSABUF sent_buffer{3, const_cast<char*>("out")}; DWORD sent = 0;
    int send_result = WSASendTo(sender, &sent_buffer, 1, &sent, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address), &send_operation, nullptr);
    Check(send_result == 0 || (send_result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING), "known asynchronous send submitted");
    Check(WaitForSingleObject(send_operation.hEvent, 1500) == WAIT_OBJECT_0
        && WSAGetOverlappedResult(sender, &send_operation, &sent, FALSE, &flags) && sent == 3,
        "GetResult distinguishes known sends from received packets");
    Check(recv(receiver, payload, sizeof(payload), 0) == 3, "known send fixture drained");
    // Native E83AF0 consumes completed sends without calling GetResult. Reuse
    // the identical request/event repeatedly, exactly without that extra call.
    for (int i = 0; i < 80; ++i) {
        WSAResetEvent(send_operation.hEvent);
        send_result = WSASendTo(sender, &sent_buffer, 1, &sent, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address), &send_operation, nullptr);
        Check(send_result == 0 || (send_result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING), "native send request can be reused without GetResult");
        Check(WaitForSingleObject(send_operation.hEvent, 1500) == WAIT_OBJECT_0 && HasOverlappedIoCompleted(&send_operation),
              "real kernel send completion before native event reset");
        Check(recv(receiver, payload, sizeof(payload), 0) == 3, "native-pattern send fixture drained");
    }
    // UDP commonly completes immediately on loopback. Exercise pending/final
    // status deterministically on a finished, test-owned OVERLAPPED as well;
    // no actual pending kernel operation is altered by this fixture.
    const ULONG_PTR finished_status = send_operation.Internal;
    Check(S7TestMarkSendPending(sender, &send_operation), "seed test-only pending-send metadata from finished request");
    send_operation.Internal = 0x103; // NTSTATUS STATUS_PENDING, not WSA_IO_PENDING.
    Check(WSASendTo(sender, &sent_buffer, 1, &sent, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address), &send_operation, nullptr)
        == SOCKET_ERROR && WSAGetLastError() == WSAENOBUFS, "pending kernel send cannot be rearmed");
    send_operation.Internal = finished_status; WSAResetEvent(send_operation.hEvent);
    send_result = WSASendTo(sender, &sent_buffer, 1, &sent, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address), &send_operation, nullptr);
    Check(send_result == 0 || (send_result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING), "completed pending send rearmed from live original request status without GetResult");
    Check(WaitForSingleObject(send_operation.hEvent, 1500) == WAIT_OBJECT_0 && recv(receiver, payload, sizeof(payload), 0) == 3,
          "rearmed pending-status fixture completed and drained");
    WSACloseEvent(send_operation.hEvent);
    WSAOVERLAPPED never_submitted{};
    Check(!WSAGetOverlappedResult(receiver, &never_submitted, &received, FALSE, &flags) && WSAGetLastError() == WSAEINVAL,
          "unknown completion cannot bypass source classification");
    Check(Replace(root, Record(GetTickCount64() + 2000, three)) && WaitAllowed(a, true), "unconsumed immediate receive lease fixture");
    Check(sendto(sender, "abc", 3, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 3, "queue unconsumed immediate receive");
    FD_ZERO(&ready); FD_SET(receiver, &ready); ready_timeout = {1, 0};
    Check(select(0, &ready, nullptr, nullptr, &ready_timeout) == 1 && rearm() == 0, "immediate receive remains reserved before GetResult");
    Check(WSASendTo(receiver, &sent_buffer, 1, &sent, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address), &overlap, nullptr)
        == SOCKET_ERROR && WSAGetLastError() == WSAENOBUFS, "completed receive cannot use send-reuse exception before GetResult");
    reinterpret_cast<sockaddr_in*>(&source)->sin_addr.s_addr = htonl(a);
    std::array<WSAOVERLAPPED, 64> pending{};
    std::array<sockaddr_storage, 64> pending_sources{};
    std::array<int, 64> pending_sizes{};
    std::array<char, 64> pending_bytes{};
    for (size_t i = 0; i < pending.size(); ++i) {
        pending[i].hEvent = WSACreateEvent(); pending_sizes[i] = sizeof(sockaddr_storage);
        WSABUF one{1, &pending_bytes[i]}; received = flags = 0;
        const int result = WSARecvFrom(receiver, &one, 1, &received, &flags, reinterpret_cast<sockaddr*>(&pending_sources[i]),
                                      &pending_sizes[i], &pending[i], nullptr);
        Check(result == SOCKET_ERROR && WSAGetLastError() == (i < 63 ? WSA_IO_PENDING : WSAENOBUFS), "bounded table never evicts pending or unconsumed immediate entry");
    }
    Sleep(2010);
    Check(!WSAGetOverlappedResult(receiver, &overlap, &received, FALSE, &flags) && WSAGetLastError() == WSAEACCES && !received,
          "unconsumed immediate entry survives table pressure and expiry is checked");
    WSACloseEvent(overlap.hEvent);
    closesocket(receiver); receiver = INVALID_SOCKET;
    // The test itself waits for each real cancelled kernel operation before
    // freeing its buffers/events. Guard close only removed pointer metadata.
    for (size_t i = 0; i < pending.size(); ++i) {
        if (i < 63) Check(WaitForSingleObject(pending[i].hEvent, 1500) == WAIT_OBJECT_0, "test awaits cancelled kernel receive lifetime");
        WSACloseEvent(pending[i].hEvent);
    }
    closesocket(sender); closesocket(receiver); WSACleanup();
    Check(Replace(root, Record(GetTickCount64() + 15000, three)) && WaitAllowed(a, true), "worker shutdown fixture");
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr), release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    S7TestReadGate(entered, release, 25);
    Check(WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0, "test pauses worker after verified file open before reading");
    Check(!S7TestStopGroup() && !Allowed(a) && Allowed(local) && Allowed(coordinator), "bounded stop timeout immediately revokes dynamic peers");
    Check(!S7TestStartGroup(), "timed out old worker prevents a second worker");
    SetEvent(release); S7TestReadGate(entered, release, 2000);
    Check(S7TestStopGroup() && !Allowed(a), "late file completion cannot republish after stop");
    S7TestReadGate(nullptr, nullptr, 5000); CloseHandle(entered); CloseHandle(release);
    Check(S7TestStartGroup() && WaitAllowed(a, true), "worker restarts only after previous lifetime joined");
    Check(S7TestStopGroup() && S7TestStopGroup() && !Allowed(a), "normal explicit worker shutdown is idempotent");
    printf("%s %d LAN membership checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
