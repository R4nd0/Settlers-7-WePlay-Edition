#pragma once
#include "crossplay_active_setup_capture.h"

// Bounded read-only dependency inventory for the original enclosing writer.
// Caller supplies an independently owned root/profile and checks ownership again
// afterward. This does not invoke a reader/writer or authorize a world start.
namespace CrossplaySetupRootDependencies {
constexpr size_t MaximumBytes=2*1024*1024;
namespace Detail {
using CrossplayNativeCodec::Detail::Readable;
using CrossplayChat::Word;
struct Inventory {
    std::vector<unsigned char> bytes;
    bool Append(const void* pointer,size_t size) {
        if(size>MaximumBytes || bytes.size()>MaximumBytes-size)return false;
        if(!size)return true;
        if(!Readable(pointer,size))return false;
        const auto* begin=static_cast<const unsigned char*>(pointer);
        bytes.insert(bytes.end(),begin,begin+size);return true;
    }
    bool Text(const unsigned char* object,bool& populated) {
        populated=false;if(!Readable(object,16))return false;
        const uint32_t pointer=Word(object),capacity=Word(object+4),length=Word(object+8);
        if(length>512 || (length && capacity<=length))return false;
        if(!pointer)return !length && !capacity;
        const auto* value=reinterpret_cast<const unsigned char*>(pointer);
        if(!Readable(value,length+1) || value[length] || !Append(value,length+1))return false;
        populated=length!=0;return true;
    }
    bool Vector(const unsigned char* header,size_t stride,size_t maximum,const unsigned char*& data,size_t& count) {
        data=nullptr;count=0;if(!Readable(header,12) || !stride)return false;
        const uint32_t begin=Word(header),end=Word(header+4),capacity=Word(header+8);
        if(end<begin || capacity<end || (end-begin)%stride || (end-begin)/stride>maximum)return false;
        count=(end-begin)/stride;data=reinterpret_cast<const unsigned char*>(begin);
        return Append(data,end-begin);
    }
    bool Registry(const unsigned char* table,bool lookup,std::vector<const unsigned char*>& entries) {
        entries.clear();if(!Append(table,lookup?0x20:0x1c))return false;
        const auto* registry=reinterpret_cast<const unsigned char*>(Word(table+0x18));
        if(!Append(registry,16))return false;
        const uint32_t count=Word(registry+8),capacity=Word(registry+12);
        const auto* array=reinterpret_cast<const unsigned char*>(Word(registry));
        if(!count || count>4096 || capacity<count || capacity>4096 || !Append(array,count*4))return false;
        for(uint32_t i=0;i<count;++i) {
            const auto* entry=reinterpret_cast<const unsigned char*>(Word(array+4*i));entries.push_back(entry);
            bool populated=false;
            if(entry && (!Append(entry,16) || !Text(entry,populated)))return false;
        }
        if(!lookup)return true;
        const auto* buckets=reinterpret_cast<const unsigned char*>(Word(table+0x1c));
        if(!Append(buckets,8))return false;
        const uint32_t mask=Word(buckets+4);
        if(mask>65535 || (mask&(mask+1)))return false;
        const auto* data=reinterpret_cast<const unsigned char*>(Word(buckets));
        if(!Append(data,8*(static_cast<size_t>(mask)+1)))return false;
        bool empty=false;
        for(uint32_t i=0;i<=mask;++i) {
            const uint32_t id=Word(data+8*i);
            if(!id){empty=true;continue;}
            if(id>=entries.size() || !entries[id])return false;
        }
        // Original lookup linearly probes until an empty bucket; a full or
        // corrupt table must never reach the original unbounded search loop.
        return empty;
    }
};
inline bool UsedSectorId(uint32_t id,const std::vector<const unsigned char*>& entries) {
    // Zero and out-of-range IDs use the original empty/default-string branch.
    return !id || id>=entries.size() || entries[id]!=nullptr;
}
}
inline bool Capture(const unsigned char* root,const unsigned char* profile,bool gold,Crossplay::Digest& digest) {
    digest={};Detail::Inventory inventory;
    if(!inventory.Append(root,gold?0xa0:0x90))return false;
    bool populated=false;
    for(size_t offset:{4u,0x14u,0x24u})if(!inventory.Text(root+offset,populated))return false;
    const unsigned char* victories=nullptr;size_t victory_count=0;
    if(!inventory.Vector(root+(gold?0x38:0x34),0x44,128,victories,victory_count))return false;
    bool lookup=false;
    for(size_t i=0;i<victory_count;++i)for(size_t field=0;field<4;++field) {
        if(!inventory.Text(victories+i*0x44+field*16,populated))return false;
        if(!field && populated)lookup=true;
    }
    const unsigned char* sectors=nullptr;size_t sector_count=0;
    const size_t sector_stride=gold?0x18:0x14;
    if(!inventory.Vector(root+(gold?0x58:0x4c),sector_stride,128,sectors,sector_count))return false;
    if((victory_count || sector_count) && !Detail::Readable(profile,gold?0x1ac:0x16c))return false;
    if(victory_count) {
        const size_t offset=gold?0x188:0x148;
        if(!inventory.Append(profile+offset,4))return false;
        const auto* catalogue=reinterpret_cast<const unsigned char*>(Detail::Word(profile+offset));
        if(!Detail::Readable(catalogue,gold?0xb08:0xa7c))return false;
        std::vector<const unsigned char*> entries;
        if(!inventory.Registry(catalogue+(gold?0xac0:0xa38),lookup,entries))return false;
        const size_t values_offset=gold?0xb04:0xa78;
        if(!inventory.Append(catalogue+values_offset,4))return false;
        const auto* values=reinterpret_cast<const unsigned char*>(Detail::Word(catalogue+values_offset));
        if(!inventory.Append(values,entries.size()*(gold?0x98:0xa0)))return false;
    }
    if(sector_count) {
        const size_t offset=gold?0x1a8:0x168;
        if(!inventory.Append(profile+offset,4))return false;
        const auto* catalogue=reinterpret_cast<const unsigned char*>(Detail::Word(profile+offset));
        if(!Detail::Readable(catalogue,gold?0x20:0x24))return false;
        std::vector<const unsigned char*> entries;
        // Independently located: Gold reads catalogue+0x1c, History +0x20.
        if(!inventory.Registry(catalogue+(gold?4:8),false,entries))return false;
        for(size_t i=0;i<sector_count;++i) {
            const auto* sector=sectors+i*sector_stride;
            if(!Detail::UsedSectorId(Detail::Word(sector+4),entries))return false;
            const unsigned char* placements=nullptr;size_t count=0;
            if(!inventory.Vector(sector+(gold?0xc:8),8,512,placements,count))return false;
            for(size_t j=0;j<count;++j)if(!Detail::UsedSectorId(Detail::Word(placements+8*j),entries))return false;
        }
    }
    return CrossplaySetupSignatureNative::Hash(inventory.bytes,digest);
}
}
