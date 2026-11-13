#pragma once
#include "crossplay_preparation_capture.h"
#include "crossplay_setup_root_dependencies.h"
#include "crossplay_setup_root_writer.h"

// Original enclosing-write diagnostic only. No remote setup reader, content
// permission, world event, NEO command or save acknowledgement is supplied.
namespace CrossplaySetupRootCapture {
struct Snapshot {
    uint32_t format_version=0;
    CrossplayPreparationCapture::Snapshot preparation{};
    Crossplay::Digest dependencies{};
    CrossplayActiveSetupCapture::Row complete{};
};
inline uintptr_t ProfilePointer(Crossplay::Edition edition) {
    if(edition==Crossplay::Edition::Gold)return 0x10f835c;
    if(edition==Crossplay::Edition::UbisoftHistory)return 0x13a5214;
    if(edition==Crossplay::Edition::SteamHistory)return 0x13a2304;
    return 0;
}
inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    CrossplaySessionConfig::Connections ids,bool host,Snapshot& result,uint32_t version=1,unsigned* diagnostic=nullptr) {
    result={};Snapshot captured{};
    const auto stage=[diagnostic](unsigned value){if(diagnostic)*diagnostic=value;};stage(400);
    if(version!=1 && version!=2)return false;
    if(!CrossplayPreparationCapture::Capture(edition,binding,ids,host,captured.preparation,diagnostic,1))return false;
    stage(410);CrossplaySetupRootWriter::Pin writer{};CrossplaySetupSignatureNative::Pin rules{};
    if(!CrossplaySetupRootWriter::Layout(edition,writer) || !CrossplaySetupRootWriter::Methods(edition,writer)
        || !CrossplaySetupSignatureNative::Layout(edition,rules))return false;
    using CrossplayNativeCodec::Detail::Readable;
    const auto profile=CrossplayNativeCodec::Detail::Pin(edition);
    if(!Readable(reinterpret_cast<const void*>(rules.manager),4))return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(rules.manager);
    if(!Readable(manager,8+2*rules.stride) || CrossplayChat::Word(manager)!=rules.type || manager[4]>1)return false;
    const unsigned mode=manager[4];auto* root=const_cast<unsigned char*>(manager+((mode-1)&1)*rules.stride+0x34);
    if(!Readable(root,rules.root_size))return false;
    const auto* config=reinterpret_cast<const unsigned char*>(CrossplayChat::Word(root+(profile.gold?0x70:0x64)));
    auto* rows=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(captured.preparation.players.owner.preparation));
    if(!Readable(config,8) || CrossplayChat::Word(config)!=reinterpret_cast<uintptr_t>(rows)
        || CrossplayChat::Word(config+4)!=reinterpret_cast<uintptr_t>(rows))return false;
    const uintptr_t pointer=ProfilePointer(edition);
    if(!pointer || !Readable(reinterpret_cast<const void*>(pointer),4))return false;
    const auto* globals=*reinterpret_cast<const unsigned char* const*>(pointer);
    stage(411);
    if(!CrossplaySetupRootDependencies::Capture(root,globals,profile.gold,captured.dependencies))return false;
    const size_t row_bytes=25*(profile.gold?0x5c4:0x5a8);
    if(!Readable(rows,row_bytes) || (version==2 && !CrossplayNative::HistoryRoleDetail::Writable(root+(profile.gold?0x64:0x58),4)))return false;
    const std::vector<unsigned char> original_rows(rows,rows+row_bytes);
    stage(412);CrossplayNativeCodec::Detail::OwnedStream stream(profile);if(!stream.initialized)return false;
    CrossplayNativeCodec::Detail::Writer visitor{profile.writer_vt,0,&stream.value,0};
    CrossplaySetupRootWriter::Detail::Write(edition,writer,root,&visitor,version);
    const auto& s=stream.value;
    stage(413);
    if(s.vt!=profile.stream_vt || s.external || !s.length || s.length>131072 || s.position!=s.length
        || s.capacity<s.length || visitor.flags || !Readable(s.bytes,s.length))return false;
    const auto* data=static_cast<const unsigned char*>(s.bytes);
    if(!CrossplaySetupSignatureNative::Hash(std::vector<unsigned char>(data,data+s.length),captured.complete.digest))return false;
    captured.complete.bytes=s.length;
    stage(414);Crossplay::Digest dependencies{},positions{},players{};CrossplaySetupCheckpoint::Snapshot owner{};
    if(*reinterpret_cast<const unsigned char* const*>(rules.manager)!=manager || manager[4]!=mode
        || *reinterpret_cast<const unsigned char* const*>(pointer)!=globals
        || !CrossplaySetupCheckpointNative::Detail::Local(edition,binding,ids,host,owner)
        || !CrossplayActiveSetupCapture::Detail::SameOwner(captured.preparation.players.owner,owner)
        || memcmp(rows,original_rows.data(),original_rows.size()))return false;
    stage(415);
    if(!CrossplaySetupRootDependencies::Capture(root,globals,profile.gold,dependencies) || dependencies!=captured.dependencies
        || !CrossplayActiveSetupCapture::Detail::Dependencies(rows,profile.gold,players) || players!=captured.preparation.players.dependencies
        || !CrossplayPreparationCapture::Detail::Positions(rows,profile.gold,positions) || positions!=captured.preparation.positions
        || !CrossplaySetupRootWriter::Methods(edition,writer))return false;
    captured.format_version=version;result=captured;stage(0);return true;
}
}
