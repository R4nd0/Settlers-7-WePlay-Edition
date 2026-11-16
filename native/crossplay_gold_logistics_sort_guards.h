#pragma once
namespace GoldLogisticsSort {
inline uint64_t RegionHash(uintptr_t at,unsigned bytes) {
 if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(at),bytes))return 0;
 uint64_t value=14695981039346656037ull;
 for(unsigned i=0;i<bytes;++i)value=(value^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
 return value;
}
inline bool Guard() {
 struct Region {uintptr_t at;unsigned size;uint64_t hash;};
 static constexpr Region regions[]={
 {0x91ad00,97,0x2956b0e7ba3ef981ull},
 {0x91fe00,204,0x9333d9ae3bba87c4ull},
 {0x947d80,453,0x9248d8db35cffa49ull},
 {0xf4c74c,8,0x9072d3ecc45e4809ull},
 {0x946890,175,0x4ba6b3ea3d1b32d4ull},
 {0x946990,137,0x3afc134793772ff0ull},

 {0x9516e0,68,0x44eddc6941ed31b6ull},
 {0x93b5b0,173,0x49de90d233f29311ull},
 {0x8ff160,225,0xf43266e185212a10ull},
 {0x919690,616,0xec164b87ea99fa91ull},
 {0xf317e0,8,0x3dee5dbb30aed89ull},
 {0xf4bb0c,8,0x841afd756dc6f69dull},
 {0xf4beb4,8,0x7427e3a6bffa8fc4ull},
 {0xf4c684,8,0x52fdfffce53cde18ull},
};
 for(const auto& region:regions)if(RegionHash(region.at,region.size)!=region.hash)return false;
 return true;
}
}
