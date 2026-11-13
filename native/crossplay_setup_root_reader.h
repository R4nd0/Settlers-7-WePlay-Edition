#pragma once
#include "crossplay_setup_root_lifetime.h"
// Reads only a separately constructed original-owned diagnostic target.
namespace CrossplaySetupRootReader {
namespace Detail {
struct Result : CrossplaySetupRootLifetime::Detail::Result {
    bool reader_returned=false,input_consumed=false,input_unchanged=false,configuration_after_read=false;
    bool target_dependencies_bounded=false,writeback_equal=false;
};
inline bool Serialize(Crossplay::Edition edition,const CrossplaySetupRootWriter::Pin& pin,void* root,void* visitor,uint32_t version,DWORD& exception) {
    __try {CrossplaySetupRootWriter::Detail::Write(edition,pin,root,visitor,version);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {exception=GetExceptionCode();return false;}
}
inline bool Configuration(const unsigned char* root,bool gold,uintptr_t rows,uintptr_t selector,uintptr_t config) {
    using CrossplayNativeCodec::Detail::Readable;
    return CrossplayChat::Word(root+(gold?0x68:0x5c))==rows
        && CrossplayChat::Word(root+(gold?0x6c:0x60))==selector
        && CrossplayChat::Word(root+(gold?0x70:0x64))==config
        && Readable(reinterpret_cast<void*>(selector),4) && Readable(reinterpret_cast<void*>(config),8)
        && CrossplayChat::Word(reinterpret_cast<const unsigned char*>(selector))==rows
        && CrossplayChat::Word(reinterpret_cast<const unsigned char*>(config))==rows
        && CrossplayChat::Word(reinterpret_cast<const unsigned char*>(config)+4)==rows;
}
inline bool Probe(Crossplay::Edition edition,uintptr_t active_rows,const std::vector<unsigned char>& input,uint32_t version,Result& result) {
    result={};CrossplaySetupRootWriter::Pin pin{};
    if(!active_rows || input.empty() || input.size()>131072 || (version!=1 && version!=2)
        || !CrossplaySetupRootLifetime::Detail::Methods(edition) || !CrossplaySetupRootWriter::Layout(edition,pin)
        || !CrossplaySetupRootWriter::Methods(edition,pin))return false;
    const auto profile=CrossplayNativeCodec::Detail::Pin(edition);
    if(!CrossplayNativeCodec::Detail::Methods(profile) || !CrossplayNativeCodec::Detail::Tables(profile,edition))return false;
    Crossplay::Digest initial_digest{};if(!CrossplaySetupSignatureNative::Hash(input,initial_digest))return false;
    struct alignas(16) Storage {unsigned char before[16],root[0xa0],after[16];} storage{};
    memset(storage.before,0xa7,sizeof(storage.before));memset(storage.after,0x5d,sizeof(storage.after));
    result.constructed=CrossplaySetupRootLifetime::Detail::Construct(edition,storage.root,result.exception);
    if(!result.constructed)return false;
    const bool gold=edition==Crossplay::Edition::Gold;
    const uintptr_t rows=CrossplayChat::Word(storage.root+(gold?0x68:0x5c));
    const uintptr_t selector=CrossplayChat::Word(storage.root+(gold?0x6c:0x60));
    const uintptr_t config=CrossplayChat::Word(storage.root+(gold?0x70:0x64));
    using CrossplayNativeCodec::Detail::Readable;
    result.separate_rows=rows && rows!=active_rows && Readable(reinterpret_cast<void*>(rows),gold?0x9094:0x8dd0);
    result.configuration_owned=result.separate_rows && selector!=config && selector!=active_rows && config!=active_rows
        && Configuration(storage.root,gold,rows,selector,config);
    if(result.configuration_owned) {
        CrossplayNativeCodec::Detail::OwnedStream stream(profile);
        if(stream.initialized) {
            stream.value.bytes=const_cast<unsigned char*>(input.data());stream.value.capacity=stream.value.length=static_cast<uint32_t>(input.size());stream.value.external=1;
            struct GuardedReader {unsigned char before[16];CrossplayNativeCodec::Detail::Visitor reader;unsigned char after[16];} visitor{};
            memset(visitor.before,0xc7,sizeof(visitor.before));memset(visitor.after,0x3d,sizeof(visitor.after));
            visitor.reader={profile.reader_vt,1,&stream.value,0};
            result.reader_returned=Serialize(edition,pin,storage.root,&visitor.reader,version,result.exception);
            result.input_consumed=result.reader_returned && stream.value.vt==profile.stream_vt
                && stream.value.bytes==input.data() && stream.value.capacity==input.size() && stream.value.length==input.size()
                && stream.value.position==input.size() && stream.value.external==1
                && visitor.reader.vt==profile.reader_vt && visitor.reader.reading==1 && visitor.reader.stream==&stream.value && !visitor.reader.flags;
            for(unsigned i=0;i<16;++i)if(visitor.before[i]!=0xc7 || visitor.after[i]!=0x3d)result.input_consumed=false;
            Crossplay::Digest after_digest{};result.input_unchanged=CrossplaySetupSignatureNative::Hash(input,after_digest) && after_digest==initial_digest;
            result.configuration_after_read=Configuration(storage.root,gold,rows,selector,config);
        }
        const uintptr_t globals_pointer=CrossplaySetupRootCapture::ProfilePointer(edition);
        Crossplay::Digest dependency_digest{};
        result.target_dependencies_bounded=Readable(reinterpret_cast<void*>(globals_pointer),4)
            && CrossplaySetupRootDependencies::Capture(storage.root,*reinterpret_cast<const unsigned char* const*>(globals_pointer),gold,dependency_digest);
        if(result.input_consumed && result.input_unchanged && result.configuration_after_read && result.target_dependencies_bounded
            && CrossplaySetupRootWriter::Methods(edition,pin)) {
            CrossplayNativeCodec::Detail::OwnedStream output(profile);
            if(output.initialized) {
                struct GuardedWriter {unsigned char before[16];CrossplayNativeCodec::Detail::Writer writer;unsigned char after[16];} visitor{};
                memset(visitor.before,0xe7,sizeof(visitor.before));memset(visitor.after,0x1d,sizeof(visitor.after));
                visitor.writer={profile.writer_vt,0,&output.value,0};
                const bool returned=Serialize(edition,pin,storage.root,&visitor.writer,version,result.exception);
                const auto& value=output.value;
                result.writeback_equal=returned && value.vt==profile.stream_vt && !value.external && value.length==input.size()
                    && value.position==value.length && value.capacity>=value.length && !visitor.writer.flags
                    && Readable(value.bytes,value.length) && !memcmp(value.bytes,input.data(),input.size());
                for(unsigned i=0;i<16;++i)if(visitor.before[i]!=0xe7 || visitor.after[i]!=0x1d)result.writeback_equal=false;
            }
        }
    }
    result.destroyed=CrossplaySetupRootLifetime::Detail::Destroy(edition,storage.root,result.exception);
    result.canaries=true;for(unsigned i=0;i<16;++i)if(storage.before[i]!=0xa7 || storage.after[i]!=0x5d)result.canaries=false;
    return result.configuration_owned && result.reader_returned && result.input_consumed && result.input_unchanged
        && result.configuration_after_read && result.target_dependencies_bounded && result.writeback_equal
        && result.destroyed && result.canaries && !result.exception && CrossplaySetupRootLifetime::Detail::Methods(edition)
        && CrossplaySetupRootWriter::Methods(edition,pin) && CrossplayNativeCodec::Detail::Tables(profile,edition);
}
}
using Result=Detail::Result;

struct Verification {
    Result native{};
    bool fresh_input_equal=false;
    bool active_unchanged=false;
    unsigned capture_stage=0;
};

// Diagnostic verification only. The caller must authenticate the received
// message and confirm its current native checkpoint before entering on the
// owner thread. Refuse bytes differing from a fresh owner-bound local capture
// before invoking a native reader. The independent target is always destroyed;
// no target, active-session replacement or world permission is returned.
inline bool VerifyReceived(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
    CrossplaySessionConfig::Connections ids,bool host,uint32_t version,
    const std::vector<unsigned char>& input,Verification& result) {
    result={};
    if(input.empty() || input.size()>131072 || (version!=1 && version!=2))return false;
    CrossplaySetupRootCapture::Snapshot before{};
    result.fresh_input_equal=CrossplaySetupRootCapture::Capture(edition,binding,ids,host,before,version,&result.capture_stage)
        && before.serialized==input;
    if(!result.fresh_input_equal)return false;
    const bool verified=Detail::Probe(edition,static_cast<uintptr_t>(before.preparation.players.owner.preparation),input,version,result.native);
    CrossplaySetupRootCapture::Snapshot after{};
    result.active_unchanged=CrossplaySetupRootCapture::Capture(edition,binding,ids,host,after,version,&result.capture_stage)
        && after.serialized==input && after.complete.digest==before.complete.digest;
    return verified && result.active_unchanged;
}
}
