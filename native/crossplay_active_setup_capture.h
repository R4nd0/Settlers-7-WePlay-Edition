#pragma once
#include "crossplay_active_player_writer.h"
#include "crossplay_setup_checkpoint_native.h"

// Opt-in diagnostic only. Native identities and unknown fields are retained;
// these row digests are not a portable setup agreement or start permission.
namespace CrossplayActiveSetupCapture {
constexpr size_t RowCount=25,MaximumDependencyBytes=131072;
struct Row {uint32_t bytes=0;Crossplay::Digest digest{};};
struct Snapshot {
    CrossplaySetupCheckpoint::Snapshot owner{};
    Crossplay::Digest dependencies{};
    std::array<Row,RowCount> rows{};
};
namespace Detail {
using CrossplayNativeCodec::Detail::Readable;
inline bool Append(std::vector<unsigned char>& bytes,const void* pointer,size_t length) {
    if(length>MaximumDependencyBytes || bytes.size()>MaximumDependencyBytes-length)return false;
    if(!length)return true;
    if(!Readable(pointer,length))return false;
    const auto* begin=static_cast<const unsigned char*>(pointer);bytes.insert(bytes.end(),begin,begin+length);return true;
}
inline bool Text(const unsigned char* object,std::vector<unsigned char>& bytes,size_t maximum=512) {
    if(!Readable(object,16))return false;
    const uintptr_t pointer=CrossplayChat::Word(object);
    const uint32_t capacity=CrossplayChat::Word(object+4),length=CrossplayChat::Word(object+8);
    if(length>maximum || (length && capacity<=length))return false;
    if(!pointer)return !length && !capacity;
    const auto* text=reinterpret_cast<const unsigned char*>(pointer);
    return Readable(text,length+1) && !text[length] && Append(bytes,text,length+1);
}
inline bool Vector(const unsigned char* header,size_t stride,size_t maximum,
                   std::vector<unsigned char>& bytes,const unsigned char*& data,size_t& count) {
    data=nullptr;count=0;
    if(!stride || !Readable(header,12))return false;
    const uintptr_t begin=CrossplayChat::Word(header),end=CrossplayChat::Word(header+4),capacity=CrossplayChat::Word(header+8);
    if(end<begin || capacity<end || (end-begin)%stride || (end-begin)/stride>maximum)return false;
    count=(end-begin)/stride;data=reinterpret_cast<const unsigned char*>(begin);
    return Append(bytes,data,end-begin);
}
inline bool List(const unsigned char* header,size_t maximum,std::vector<unsigned char>& bytes) {
    if(!Readable(header,8))return false;
    const auto* head=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(header));
    const uint32_t count=CrossplayChat::Word(header+4);
    if(count>maximum || !Append(bytes,head,8))return false;
    const unsigned char* previous=head;
    auto* node=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(head));
    std::vector<const unsigned char*> visited;
    for(uint32_t i=0;i<count;++i) {
        if(node==head || std::find(visited.begin(),visited.end(),node)!=visited.end()
            || !Append(bytes,node,12) || CrossplayChat::Word(node+4)!=reinterpret_cast<uintptr_t>(previous))return false;
        visited.push_back(node);previous=node;node=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(node));
    }
    return node==head && CrossplayChat::Word(head+4)==reinterpret_cast<uintptr_t>(previous);
}
inline bool Dependencies(const unsigned char* rows,bool gold,Crossplay::Digest& digest) {
    digest={};const size_t stride=gold?0x5c4:0x5a8;
    if(!Readable(rows,gold?0x9079:0x8db9))return false;
    std::vector<unsigned char> bytes;
    const auto* table=rows+(gold?0x9024:0x8d68);
    const auto* registry=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(table+0x18));
    // Original reserve routines in all three profiles read/write capacity at
    // +0x0c. +0x04 is an opaque pool pointer, not a capacity. Retain its local
    // value without dereferencing storage the write-only visitor never reads.
    if(!Append(bytes,registry,16))return false;
    const auto* symbols=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(registry));
    const uint32_t count=CrossplayChat::Word(registry+8),capacity=CrossplayChat::Word(registry+12);
    if(!count || count>4096 || capacity<count || capacity>4096 || !Append(bytes,symbols,count*4))return false;
    for(size_t i=0;i<RowCount;++i) {
        const auto* row=rows+i*stride;
        const uint32_t color=CrossplayChat::Word(row+0x2c);
        if(color>=count)return false;
        const auto* symbol=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(symbols+color*4));
        if(!Append(bytes,symbol,16) || !Text(symbol,bytes,255) || !Text(row+0x1c,bytes))return false;
        // Original row headers: vectors store begin/end/capacity; Gold's
        // iterator debug proxy precedes each such triple and each list pair.
        const size_t vectors[]={gold?0x78u:0x74u,gold?0x52cu:0x51cu,gold?0x558u:0x544u};
        const size_t strides[]={4,12,1},limits[]={512,64,512};
        const unsigned char* data=nullptr;size_t length=0;
        for(size_t v=0;v<3;++v)if(!Vector(row+vectors[v],strides[v],limits[v],bytes,data,length))return false;
        const size_t lists[]={gold?0x4e8u:0x4e0u,gold?0x4f4u:0x4e8u,gold?0x56cu:0x554u};
        for(size_t list=0;list<3;++list)if(!List(row+lists[list],list==2?25:128,bytes))return false;
        const auto* castle=row+(gold?0x574:0x55c);
        if(!Text(castle+(gold?0:4),bytes) || !Text(castle+(gold?0x10:0x14),bytes)
            || !Vector(castle+0x24,16,128,bytes,data,length))return false;
        const size_t parts=length;
        for(size_t part=0;part<parts;++part)if(!Text(data+part*16,bytes))return false;
        if(!Vector(castle+(gold?0x34:0x30),4,128,bytes,data,length) || length!=parts
            || !Vector(castle+(gold?0x44:0x3c),4,128,bytes,data,length) || length!=parts)return false;
    }
    return CrossplaySetupSignatureNative::Hash(bytes,digest);
}
inline bool SameOwner(const CrossplaySetupCheckpoint::Snapshot& a,const CrossplaySetupCheckpoint::Snapshot& b) {
    return a.thread && a.thread==b.thread && a.session && a.session==b.session
        && a.preparation && a.preparation==b.preparation && a.cache && a.cache==b.cache
        && CrossplaySetupCheckpoint::Nonzero(a.local) && a.local==b.local;
}
}
inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,bool host,Snapshot& result,unsigned* diagnostic=nullptr) {
    using namespace CrossplayNativeCodec;result={};Snapshot captured{};
    const auto stage=[diagnostic](unsigned value){if(diagnostic)*diagnostic=value;};stage(1);
    CrossplayActivePlayerWriter::Pin pin{};CrossplaySetupCheckpoint::Snapshot before{},after{};
    if(!CrossplayActivePlayerWriter::Layout(edition,pin) || !CrossplayActivePlayerWriter::Methods(pin))return false;
    stage(2);if(!CrossplaySetupCheckpointNative::Detail::Local(edition,binding,ids,host,before))return false;
    const auto profile=CrossplayNativeCodec::Detail::Pin(edition);
    stage(3);
    if(!CrossplayNativeCodec::Detail::Methods(profile) || !CrossplayNativeCodec::Detail::Tables(profile,edition))return false;
    const auto* rows=reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(before.preparation));
    Crossplay::Digest dependencies{},final_dependencies{};
    stage(4);if(!Detail::Dependencies(rows,profile.gold,dependencies))return false;
    for(uint32_t slot=0;slot<RowCount;++slot) {
        stage(100+slot);
        CrossplayNativeCodec::Detail::OwnedStream stream(profile);if(!stream.initialized)return false;
        CrossplayNativeCodec::Detail::Visitor visitor{profile.writer_vt,0,&stream.value,0};
        CrossplayActivePlayerWriter::Detail::Write(edition,pin,const_cast<unsigned char*>(rows),&visitor,slot,8);
        const auto& s=stream.value;
        if(s.vt!=profile.stream_vt || s.external || !s.length || s.length>65536 || s.position!=s.length
            || s.capacity<s.length || visitor.flags || !CrossplayNativeCodec::Detail::Readable(s.bytes,s.length))return false;
        const auto* data=static_cast<const unsigned char*>(s.bytes);
        if(!CrossplaySetupSignatureNative::Hash(std::vector<unsigned char>(data,data+s.length),captured.rows[slot].digest))return false;
        captured.rows[slot].bytes=s.length;
    }
    stage(200);if(!CrossplaySetupCheckpointNative::Detail::Local(edition,binding,ids,host,after)
        || !Detail::SameOwner(before,after) || !Detail::Dependencies(rows,profile.gold,final_dependencies)
        || dependencies!=final_dependencies || !CrossplayActivePlayerWriter::Methods(pin))return false;
    captured.owner=after;captured.dependencies=final_dependencies;result=captured;stage(0);return true;
}
}
