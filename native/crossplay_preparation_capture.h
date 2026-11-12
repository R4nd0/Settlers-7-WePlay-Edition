#pragma once
#include "crossplay_active_setup_capture.h"
#include "crossplay_preparation_writer.h"

// Opt-in point-in-time diagnostic. The enclosing rules/root, runtime scripts
// and complete content closure remain outside this snapshot. No start consent.
namespace CrossplayPreparationCapture {
struct Snapshot {
    uint32_t format_version=0;
    CrossplayActiveSetupCapture::Snapshot players{};
    Crossplay::Digest positions{};
    CrossplayActiveSetupCapture::Row complete{};
};
namespace Detail {
inline bool Positions(const unsigned char* rows,bool gold,Crossplay::Digest& digest) {
    digest={};
    const size_t begin=gold?0x9078:0x8db8,extent=gold?0x9094:0x8dcc;
    if(!CrossplayActiveSetupCapture::Detail::Readable(rows,extent) || rows[begin]!=4)return false;
    using CrossplayActiveSetupCapture::Detail::Append;
    std::vector<unsigned char> bytes;
    if(!Append(bytes,rows+begin,extent-begin))return false;
    std::array<const unsigned char*,2> heads{};std::array<bool,25> seen{};
    for(size_t list=0;list<2;++list) {
        const auto* header=rows+(gold?(list?0x908c:0x9080):(list?0x8dc4:0x8dbc));
        if(!CrossplayActiveSetupCapture::Detail::List(header,25,bytes))return false;
        heads[list]=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(header));
        const uint32_t count=CrossplayChat::Word(header+4);
        if(count>25 || (list && heads[0]==heads[1])
            || !CrossplayActiveSetupCapture::Detail::Readable(heads[list],8))return false;
        const auto* node=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(heads[list]));
        for(uint32_t i=0;i<count;++i) {
            // List has already validated every link and node range.
            if(node==heads[0] || (list && node==heads[1])
                || !CrossplayActiveSetupCapture::Detail::Readable(node,12) || node[8]>=25 || seen[node[8]])return false;
            seen[node[8]]=true;node=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(node));
        }
    }
    return CrossplaySetupSignatureNative::Hash(bytes,digest);
}
}
inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,bool host,Snapshot& result,unsigned* diagnostic=nullptr,uint32_t version=8) {
    result={};Snapshot captured{};
    // The enclosing original setup selects 1; 8 is the existing diagnostic.
    // Other versions have not been qualified against this live capture path.
    if(version!=1 && version!=8)return false;
    captured.format_version=version;
    if(!CrossplayActiveSetupCapture::Capture(edition,binding,ids,host,captured.players,diagnostic))return false;
    const auto stage=[diagnostic](unsigned value){if(diagnostic)*diagnostic=value;};stage(300);
    CrossplayPreparationWriter::Pin pin{};
    if(!CrossplayPreparationWriter::Layout(edition,pin) || !CrossplayPreparationWriter::Methods(edition,pin))return false;
    const auto profile=CrossplayNativeCodec::Detail::Pin(edition);
    auto* rows=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(captured.players.owner.preparation));
    stage(301);if(!Detail::Positions(rows,profile.gold,captured.positions))return false;
    stage(302);
    CrossplayNativeCodec::Detail::OwnedStream stream(profile);if(!stream.initialized)return false;
    CrossplayNativeCodec::Detail::Writer visitor{profile.writer_vt,0,&stream.value,0};
    CrossplayPreparationWriter::Detail::Write(edition,pin,rows,&visitor,version);
    const auto& s=stream.value;
    if(s.vt!=profile.stream_vt || s.external || !s.length || s.length>262144 || s.position!=s.length
        || s.capacity<s.length || visitor.flags || !CrossplayNativeCodec::Detail::Readable(s.bytes,s.length))return false;
    const auto* data=static_cast<const unsigned char*>(s.bytes);
    if(!CrossplaySetupSignatureNative::Hash(std::vector<unsigned char>(data,data+s.length),captured.complete.digest))return false;
    captured.complete.bytes=s.length;
    stage(303);CrossplaySetupCheckpoint::Snapshot after{};Crossplay::Digest dependencies{},positions{};
    if(!CrossplaySetupCheckpointNative::Detail::Local(edition,binding,ids,host,after))return false;
    stage(304);if(!CrossplayActiveSetupCapture::Detail::SameOwner(captured.players.owner,after))return false;
    stage(305);if(!CrossplayActiveSetupCapture::Detail::Dependencies(rows,profile.gold,dependencies))return false;
    stage(306);if(dependencies!=captured.players.dependencies)return false;
    stage(307);if(!Detail::Positions(rows,profile.gold,positions))return false;
    stage(308);if(positions!=captured.positions)return false;
    stage(309);if(!CrossplayPreparationWriter::Methods(edition,pin))return false;
    result=captured;stage(0);return true;
}
}
