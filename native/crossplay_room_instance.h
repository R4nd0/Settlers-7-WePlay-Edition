#pragma once
#include "crossplay_channel.h"

namespace CrossplayProduction {
inline constexpr char CommonInstanceName[]="Global\\Settlers7";
inline constexpr uintptr_t RoomInstanceNameRva(Crossplay::Edition edition) {
    switch(edition) {
    case Crossplay::Edition::Gold:return 0xb24fc4;
    case Crossplay::Edition::UbisoftHistory:return 0xd10c24;
    case Crossplay::Edition::SteamHistory:return 0xd0ec54;
    default:return 0;
    }
}
inline constexpr std::array<char,sizeof(CommonInstanceName)> RoomInstanceName(uint32_t pid) {
    std::array<char,sizeof(CommonInstanceName)> name{'L','o','c','a','l','\\','S','7'};
    constexpr char hex[]="0123456789ABCDEF";
    for(unsigned i=0;i<8;++i)name[8+i]=hex[(pid>>(28-4*i))&15];
    return name;
}
}
