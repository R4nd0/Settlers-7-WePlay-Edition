#pragma once
#include <winsock2.h>

// First offline prototype is loopback-only. LAN routing is a separate capability.
bool IsOfflineEndpointAllowed(const sockaddr* address, int length);
bool InstallOfflineNetworkGuard();
// Startup-only, immutable explicit two-peer mode. Does not permit general LAN access.
bool ConfigureLanNetworkGuard(const char* local, const char* peer);
// Startup-only opt-in; coordinator and local remain allowed when membership lapses.
bool ConfigureLanGroupNetworkGuard(const char* local, const char* coordinator, const wchar_t* local_data_root);
// Explicit platform lifecycle, never DllMain. Worker reads only the owned file.
// Start is confined to the configuring thread; stop is bounded and fails closed.
bool StartLanGroupNetworkGuard();
bool StopLanGroupNetworkGuard();
