#pragma once
#include <windows.h>

namespace KernelProbe {
// These numeric values are decoded by tools/installer/standalone_entry.py.
enum class Test : DWORD {
    Winsock = 0, Ipv4LoopbackTcp, Ipv6LoopbackTcp,
    Ipv4NonloopbackTcp, Ipv4NonloopbackUdp, Ipv6NonloopbackTcp, Ipv6NonloopbackUdp
};

enum class Step : DWORD {
    Startup = 1, ListenerSocket, Bind, Listen, LocalEndpoint, ClientSocket,
    Connect, WaitForDelivery, UnexpectedDelivery, MissingDelivery
};

// Keep the first failed check in the process exit code. The remote status page
// may already be gone by the time the launcher observes the terminated child.
constexpr DWORD FailureExit(Test test, Step step, int winsock_error) {
    const DWORD error = winsock_error > 0 && winsock_error <= 0xffff ? static_cast<DWORD>(winsock_error) : 0;
    return 0xe1000000u | (static_cast<DWORD>(test) << 20) | (static_cast<DWORD>(step) << 16) | error;
}
}
