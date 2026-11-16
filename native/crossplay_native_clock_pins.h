#pragma once
namespace CrossplayNativeClock {
inline Profile Pin(Crossplay::Edition edition){
 if(edition==Crossplay::Edition::Gold)return {0x10f83f0,0xf32a50,0x41cb40,13,0x469377ea6a362169ull,0xe76a4a5bc0af6d50ull};
 if(edition==Crossplay::Edition::UbisoftHistory)return {0x13a5200,0x1133f40,0x4e8130,17,0x256c679794d29e9aull,0x811bca8e1d040803ull};
 if(edition==Crossplay::Edition::SteamHistory)return {0x13a22e8,0x113244c,0x4e7ed0,17,0xd835b7640e9c5548ull,0x811bca8e1d040803ull};
 return {};
}
}
