#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace On12Graphics {
struct ShaderPose {
    // Projection constants are logical/unjittered. Raster jitter belongs only
    // to the physical current shader, never the previous-pose texture.
    std::array<float,1024> floats{};
    std::array<std::int32_t,64> integers{};
    std::array<std::int32_t,16> booleans{};
};
using PackedShaderPose=std::array<float,288*4>;
inline bool PackShaderPose(const ShaderPose& input,PackedShaderPose& output)noexcept {
    for(const auto value:input.floats)if(!std::isfinite(value))return false;
    for(const auto value:input.integers)if(value < -16777216||value > 16777216)return false;
    std::memcpy(output.data(),input.floats.data(),sizeof(input.floats));
    for(size_t n=0;n<input.integers.size();++n)output[1024+n]=static_cast<float>(input.integers[n]);
    for(size_t n=0;n<input.booleans.size();++n){output[1088+n*4]=input.booleans[n]?1.0f:0.0f;
        output[1089+n*4]=output[1090+n*4]=output[1091+n*4]=0;}
    return true;
}
struct PoseKey {
    // Tokens represent retained resource lifetimes and content versions, not
    // raw staging addresses. Geometry includes draw range and stream layout.
    std::uint64_t object=0,geometry=0,program=0,material=0;
    bool operator==(const PoseKey& other)const noexcept {return object==other.object&&geometry==other.geometry&&program==other.program&&material==other.material;}
    explicit operator bool()const noexcept {return object&&geometry&&program&&material;}
};
enum PoseProof : std::uint32_t { StableVertices=1,StableIndices=2,StableVertexTextures=4,StableTopology=8,AllPoseProofs=15 };
enum class PoseStatus { Unsupported,FirstSeen,Previous,Duplicate,Conflict,Capacity,Invalid };
struct PreviousPose {PoseStatus status=PoseStatus::Unsupported;const PackedShaderPose* previous=nullptr;};

// CPU ownership follows the render thread. Results are borrowed only until
// Commit/Abort/Begin; upload them before closing the frame. A conflicting draw
// invalidates the entire pending frame, including motion already recorded.
class PoseHistory {
public:
    explicit PoseHistory(size_t capacity=2048):capacity_(capacity){}
    PoseHistory(const PoseHistory&)=delete;PoseHistory& operator=(const PoseHistory&)=delete;
    bool Begin(std::uint64_t frame,std::uint64_t epoch)noexcept {
        if(pending_||!frame||!epoch){Abort();return false;}
        if(!previous_||frame-1!=previous_||epoch!=epoch_)entries_.clear();
        frame_=frame;epoch_=epoch;pending_=healthy_=true;return true;
    }
    PreviousPose Observe(const PoseKey& key,const ShaderPose& pose,std::uint32_t proofs)noexcept {
        if(!pending_||!healthy_)return {PoseStatus::Invalid,nullptr};
        if(!key)return {};
        const size_t slot=static_cast<size_t>(frame_&1),prior=slot^1;
        auto found=entries_.find(key);
        PackedShaderPose packed{};
        if(proofs!=AllPoseProofs||!PackShaderPose(pose,packed)){
            if(found!=entries_.end()){
                auto& entry=found->second;
                if(entry.frames[slot]==frame_&&entry.valid[slot])healthy_=false;
                entry.frames[slot]=frame_;entry.valid[slot]=false;
            }
            return {PoseStatus::Unsupported,nullptr};
        }
        try {
            if(found==entries_.end()){
                if(entries_.size()>=capacity_){
                    for(auto at=entries_.begin();at!=entries_.end();++at){
                        const auto& frames=at->second.frames;
                        if(frames[0]!=previous_&&frames[1]!=previous_&&frames[0]!=frame_&&frames[1]!=frame_){entries_.erase(at);break;}
                    }
                    if(entries_.size()>=capacity_)return {PoseStatus::Capacity,nullptr};
                }
                found=entries_.emplace(key,Entry{}).first;
            }
            auto& entry=found->second;
            if(entry.frames[slot]==frame_){
                if(!entry.valid[slot]||std::memcmp(entry.banks[slot].data(),packed.data(),sizeof(packed))){healthy_=false;return {PoseStatus::Conflict,nullptr};}
                return {PoseStatus::Duplicate,entry.frames[prior]==previous_&&entry.valid[prior]?&entry.banks[prior]:nullptr};
            }
            const auto* before=entry.frames[prior]==previous_&&entry.valid[prior]?&entry.banks[prior]:nullptr;
            entry.banks[slot]=packed;entry.frames[slot]=frame_;entry.valid[slot]=true;
            return {before?PoseStatus::Previous:PoseStatus::FirstSeen,before};
        }catch(...){healthy_=false;return {PoseStatus::Invalid,nullptr};}
    }
    bool Commit()noexcept {
        if(!pending_||!healthy_){Abort();return false;}
        previous_=frame_;pending_=false;return true;
    }
    void Abort()noexcept {entries_.clear();frame_=previous_=epoch_=0;pending_=false;healthy_=false;}
    bool Healthy()const noexcept {return pending_&&healthy_;}
    size_t Entries()const noexcept {return entries_.size();}
private:
    struct Hash {size_t operator()(const PoseKey& key)const noexcept {
        std::uint64_t value=14695981039346656037ull;
        for(const auto part:{key.object,key.geometry,key.program,key.material})value=(value^part)*1099511628211ull;
        return static_cast<size_t>(value^(value>>32));
    }};
    struct Entry {std::array<PackedShaderPose,2> banks{};std::array<std::uint64_t,2> frames{};std::array<bool,2> valid{};};
    size_t capacity_;std::uint64_t frame_=0,previous_=0,epoch_=0;bool pending_=false,healthy_=false;
    std::unordered_map<PoseKey,Entry,Hash> entries_;
};
}
