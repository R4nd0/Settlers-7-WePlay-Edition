#pragma once
#include "on12_scene_projection.h"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <atomic>

namespace On12Graphics {
inline bool SkinningPoseProgram(const void* data,size_t bytes){
    if(!SceneProjectionShader(data,bytes))return false;const auto* p=static_cast<const unsigned char*>(data);
    auto u32=[&](size_t at){std::uint32_t value=0;std::memcpy(&value,p+at,4);return value;};
    auto u16=[&](size_t at){std::uint16_t value=0;std::memcpy(&value,p+at,2);return value;};
    bool world=false,bones=false;
    for(size_t at=4;at<bytes;){const auto token=u32(at),op=token&0xffff;if(token==0xffff)break;
        const size_t count=op==0xfffe?(token>>16)&0x7fff:(token>>24)&15;
        if(op==0xfffe&&count&&u32(at+4)==0x42415443){const size_t base=at+8;const auto n=u32(base+12),offset=u32(base+16);
            for(std::uint32_t i=0;i<n;++i){const size_t entry=base+offset+i*20;const auto name=reinterpret_cast<const char*>(p+base+u32(entry));
                if(u16(entry+4)!=2)continue;
                if(u16(entry+6)==68&&u16(entry+8)==4&&!std::strcmp(name,"g_WorldMatrix"))world=true;
                if(u16(entry+6)==80&&u16(entry+8)>=3&&!std::strcmp(name,"g_SkinningMatrixArray"))bones=true;
            }}at+=(count+1)*4;
    }return world&&bones;
}
struct MotionPoseCommand {DWORD size=16,version=1,sequence=0,frames=0;};
struct MotionPoseKey {std::uint32_t kind=0,primitive=0;std::int32_t base_vertex=0;std::uint32_t minimum=0,vertices=0,start=0,primitives=0;};
#pragma pack(push,1)
struct MotionPoseRecord {
    std::uint64_t frame=0,source=0,caller=0,vertex_buffer=0,index_buffer=0,vertex_shader=0,pixel_shader=0;
    std::uint32_t kind=0,primitive=0;std::int32_t base_vertex=0;
    std::uint32_t minimum=0,vertices=0,start=0,primitives=0;
    std::uint32_t offset=0,stride=0,frequency=0,vb_usage=0,vb_bytes=0,index_format=0,source_type=0,source_stack=0,upload_first=0,upload_count=0;
    std::array<float,1024> constants{};std::array<std::int32_t,64> integers{};std::array<std::int32_t,16> booleans{};
    std::uint64_t world_hash=0,bone_hash=0;
    std::uint64_t bone_source=0,bone_caller=0;
    std::uint32_t bone_upload_first=0,bone_upload_count=0,bone_source_type=0,bone_source_stack=0;
};
struct MotionPoseHeader {
    char magic[8]={'S','7','P','O','S','E','1',0};
    DWORD version=1,record_bytes=sizeof(MotionPoseRecord),sequence=0,records=0,frames=0,requested=0;
};
#pragma pack(pop)
static_assert(sizeof(MotionPoseCommand)==16&&sizeof(MotionPoseHeader)==32&&sizeof(MotionPoseRecord)==4588,"Private pose observation ABI");
class MotionPoseProbe {
public:
    void Initialize(){owner_=GetCurrentThreadId();}
    bool Arm(const MotionPoseCommand& command){
        if(!Own()||command.size!=16||command.version!=1||!command.sequence||command.sequence<=sequence_||command.frames<2||command.frames>4)return false;
        records_.clear();records_.reserve(command.frames*128);for(auto& item:shaders_)item={};shader_count_=0;
        sequence_=command.sequence;requested_=command.frames;captured_=0;frame_=0;in_frame_=false;frame_records_=0;InvalidateSource();foreign_.store(false);enabled_.store(true,std::memory_order_release);return true;
    }
    void Begin(std::uint64_t frame){if(!Own()||!Active())return;frame_=frame;frame_records_=0;in_frame_=true;}
    bool End(){if(!Own()||!Active()||!in_frame_)return false;in_frame_=false;++captured_;if(!Active())enabled_.store(false,std::memory_order_release);return true;}
    bool Enabled()const{return enabled_.load(std::memory_order_acquire);}
    bool Active()const{return requested_&&captured_<requested_;}
    bool CanObserve()const{return Active()&&in_frame_&&frame_records_<128&&!foreign_.load(std::memory_order_acquire);}
    DWORD Sequence()const{return sequence_;}
    DWORD Captured()const{return captured_;}DWORD Requested()const{return requested_;}
    const std::vector<MotionPoseRecord>& Records()const{return records_;}
    void InvalidateSource(){if(Own()){source_=caller_=bone_source_=bone_caller_=0;first_=count_=bone_first_=bone_count_=0;}}
    void ForeignUpdate(){foreign_.store(true,std::memory_order_release);}
    void Upload(UINT first,const float* values,UINT count,std::uintptr_t caller){
        if(!Own()||!count||first>=256||count>256-first)return;
        if(first<72&&first+count>68){source_=0;caller_=caller;first_=first;count_=count;
            if(values&&first<=68&&first+count>=72)source_=reinterpret_cast<std::uintptr_t>(values)+static_cast<size_t>(68-first)*16;}
        if(first<230&&first+count>80){bone_caller_=caller;bone_first_=first;bone_count_=count;
            if(values&&first<=80)bone_source_=reinterpret_cast<std::uintptr_t>(values)+static_cast<size_t>(80-first)*16;
            else if(!values||!bone_source_||reinterpret_cast<std::uintptr_t>(values)!=bone_source_+static_cast<size_t>(first-80)*16)bone_source_=0;}
    }
    bool Skinning(IDirect3DVertexShader9* shader){
        if(!shader)return false;for(UINT i=0;i<shader_count_;++i)if(shaders_[i].shader.Get()==shader)return shaders_[i].skin;
        UINT bytes=0;bool skin=false;
        if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes>=16&&bytes<=65536){std::vector<unsigned char> code(bytes);
            if(SUCCEEDED(shader->GetFunction(code.data(),&bytes))&&bytes==code.size())skin=SkinningPoseProgram(code.data(),bytes);}
        if(shader_count_<shaders_.size()){auto& item=shaders_[shader_count_++];item.shader=shader;item.skin=skin;}return skin;
    }
    template<class Read> bool Observe(MotionPoseRecord record,Read read){
        if(!Own()||!CanObserve()||foreign_.load(std::memory_order_acquire))return false;
        record.frame=frame_;record.source=source_;record.caller=caller_;record.upload_first=first_;record.upload_count=count_;
        record.bone_source=bone_source_;record.bone_caller=bone_caller_;record.bone_upload_first=bone_first_;record.bone_upload_count=bone_count_;
        if(!read(record)||foreign_.load(std::memory_order_acquire))return false;
        record.world_hash=Hash(record.constants.data()+68*4,64);record.bone_hash=Hash(record.constants.data()+80*4,150*16);
        records_.push_back(record);++frame_records_;return true;
    }
    MotionPoseHeader Header()const{MotionPoseHeader h;h.sequence=sequence_;h.records=static_cast<DWORD>(records_.size());h.frames=captured_;h.requested=requested_;return h;}
    void Stop(){if(!Own())return;enabled_.store(false,std::memory_order_release);requested_=0;in_frame_=false;for(auto& item:shaders_)item={};shader_count_=0;}
private:
    struct Shader{Microsoft::WRL::ComPtr<IDirect3DVertexShader9> shader;bool skin=false;};
    static std::uint64_t Hash(const void* pointer,size_t bytes){const auto* p=static_cast<const unsigned char*>(pointer);std::uint64_t value=14695981039346656037ull;
        for(size_t i=0;i<bytes;++i)value=(value^p[i])*1099511628211ull;return value;}
    bool Own()const{return owner_&&owner_==GetCurrentThreadId();}
    DWORD owner_=0,sequence_=0,requested_=0,captured_=0;UINT frame_records_=0,first_=0,count_=0,bone_first_=0,bone_count_=0,shader_count_=0;
    std::uint64_t frame_=0;std::uintptr_t source_=0,caller_=0,bone_source_=0,bone_caller_=0;bool in_frame_=false;
    std::atomic_bool foreign_{false},enabled_{false};
    std::vector<MotionPoseRecord> records_;std::array<Shader,64> shaders_{};
};
}
