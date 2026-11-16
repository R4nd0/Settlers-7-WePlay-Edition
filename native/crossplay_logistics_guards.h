#pragma once
namespace CrossplayLogisticsBinding {
inline bool OriginalGuards(Crossplay::Edition edition) {
ErrorScope error;
struct Region{uintptr_t at;unsigned length;uint64_t hash;};
 if(edition==Crossplay::Edition::Gold){
static constexpr Region rows[]={
{0xf317e0,8,0x3dee5dbb30aed89ull},
{0xf4bb0c,8,0x841afd756dc6f69dull},
{0xf4beb4,8,0x7427e3a6bffa8fc4ull},
{0xf4c684,8,0x52fdfffce53cde18ull},
{0xf4c74c,8,0x9072d3ecc45e4809ull},
{0x947d80,453,0x9248d8db35cffa49ull},
{0x91fe00,204,0x9333d9ae3bba87c4ull},
};
for(const auto& row:rows){if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.at),row.length))return false;uint64_t value=14695981039346656037ull;for(unsigned i=0;i<row.length;++i)value=(value^reinterpret_cast<const unsigned char*>(row.at)[i])*1099511628211ull;if(value!=row.hash)return false;}return true;}
 if(edition==Crossplay::Edition::UbisoftHistory){
static constexpr Region rows[]={
{0x1141534,8,0x339ef3cc9658f561ull},
{0x1147f40,8,0x120e21b8b01694a9ull},
{0x1148328,8,0x8d426e7e0257ef7aull},
{0x1148a68,8,0x9d4f71421483f656ull},
{0x1148b6c,8,0xff83a1268556f570ull},
{0x78f380,144,0x2449f1e39b3fb752ull},
{0x7a6300,414,0xec4412e358186abcull},
};
for(const auto& row:rows){if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.at),row.length))return false;uint64_t value=14695981039346656037ull;for(unsigned i=0;i<row.length;++i)value=(value^reinterpret_cast<const unsigned char*>(row.at)[i])*1099511628211ull;if(value!=row.hash)return false;}return true;}
 if(edition==Crossplay::Edition::SteamHistory){
static constexpr Region rows[]={
{0x113fa54,8,0x418d53bc90c2b1a0ull},
{0x11464d0,8,0xa512f3fa32862455ull},
{0x1146850,8,0xbea11467d80ebfd6ull},
{0x1146f90,8,0xe0b38f83ce729f36ull},
{0x1147094,8,0xbca2816069acfaa4ull},
{0x78f150,144,0x2596a4e4b7a740bbull},
{0x7a60d0,414,0xad9f02bc87cafd48ull},
};
for(const auto& row:rows){if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.at),row.length))return false;uint64_t value=14695981039346656037ull;for(unsigned i=0;i<row.length;++i)value=(value^reinterpret_cast<const unsigned char*>(row.at)[i])*1099511628211ull;if(value!=row.hash)return false;}return true;}
return false;
}
}
