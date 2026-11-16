#pragma once
namespace CrossplayLogisticsIntent {
inline bool OriginalGuards(Crossplay::Edition value){
CrossplayLogisticsBinding::ErrorScope error;
struct Region{uintptr_t at;unsigned size;uint64_t hash;};
if(value==Crossplay::Edition::Gold){
static constexpr Region rows[]={
{0x90abf0,689,0xaf1df6416dce3a78ull},
{0x9002de,5,0x70e22ad13093ce6dull},
{0x907c80,139,0xc18ff12347d5f431ull},
};
for(const auto& row:rows){if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.at),row.size))return false;uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<row.size;++i)hash=(hash^reinterpret_cast<const unsigned char*>(row.at)[i])*1099511628211ull;if(hash!=row.hash)return false;}return true;}
if(value==Crossplay::Edition::UbisoftHistory){
static constexpr Region rows[]={
{0x782ee0,635,0x13e2c596ec571bf1ull},
{0x77abbc,5,0x468b4040d6daff49ull},
{0x7bbb97,5,0x369de784311a260bull},
{0x9e77f1,5,0x8cb76a0fc3bf0383ull},
{0xa145c4,5,0xbb683ef8bd1a1448ull},
{0xa146d4,5,0x8664367ce026387full},
{0xa147a0,5,0x3ba3ee69d3d92c06ull},
{0xa147d9,5,0xd2847dab31952e4full},
};
for(const auto& row:rows){if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.at),row.size))return false;uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<row.size;++i)hash=(hash^reinterpret_cast<const unsigned char*>(row.at)[i])*1099511628211ull;if(hash!=row.hash)return false;}return true;}
if(value==Crossplay::Edition::SteamHistory){
static constexpr Region rows[]={
{0x782cb0,635,0x4334a3e26d7964e5ull},
{0x77a98c,5,0x468b4040d6daff49ull},
{0x7bb967,5,0x369de784311a260bull},
{0x9e71c1,5,0xf141560ffbb8a87full},
{0xa13a44,5,0x4924af755cccd47dull},
{0xa13b54,5,0x4184eab2e8cab0ull},
{0xa13c20,5,0x62f3ab0cce075c43ull},
{0xa13c59,5,0x81052a1010365e6aull},
};
for(const auto& row:rows){if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(row.at),row.size))return false;uint64_t hash=14695981039346656037ull;for(unsigned i=0;i<row.size;++i)hash=(hash^reinterpret_cast<const unsigned char*>(row.at)[i])*1099511628211ull;if(hash!=row.hash)return false;}return true;}
return false;
}
}
