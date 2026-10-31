#pragma once
#ifdef S7_LOCAL_PAIR_LAB
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <cstdint>
namespace lab_address_table {
// The lab binds real game sockets to distinct loopback slots. Storm also
// exchanges the OS unicast table directly, bypassing catalogue projection.
// Project only this process's returned table, never OS configuration or peers.
inline bool Project(MIB_UNICASTIPADDRESS_TABLE* table, uint32_t slot) {
    if (!table || (slot != 0x7f000002u && slot != 0x7f000003u)
        || table->NumEntries == 0 || table->NumEntries > 4096) return false;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& row = table->Table[i];
        if (row.Address.si_family != AF_INET || row.DadState != IpDadStatePreferred) continue;
        const auto address = ntohl(row.Address.Ipv4.sin_addr.s_addr);
        const bool private_address = address >> 24 == 10u || address >> 20 == 0xac1u
            || address >> 16 == 0xc0a8u;
        if (!private_address) continue;
        auto selected = row;
        selected.Address.Ipv4.sin_addr.s_addr = htonl(slot);
        selected.Address.Ipv4.sin_port = 0;
        selected.OnLinkPrefixLength = 8;
        table->Table[0] = selected;
        table->NumEntries = 1;
        return true;
    }
    return false;
}
}
#endif
