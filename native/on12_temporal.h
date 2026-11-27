#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace On12Graphics::Temporal {
struct Color {float r=0,g=0,b=0,a=1;};
struct Motion {float x=0,y=0,previous_depth=0,confidence=0;};
// Motion is unjittered current-to-previous displacement in render pixels.
// previous_depth is expected PREVIOUS-view linear depth, not current-view Z.
struct Image {
    std::uint32_t width=0,height=0;
    std::vector<Color> color;
    std::vector<Motion> motion;
    std::vector<float> depth,reactive;
};
struct History {std::vector<Color> color;std::vector<float> depth;};
struct Parameters {
    std::uint32_t width=0,height=0,history_valid=0,color_srgb=0;
    float jitter_x=0,jitter_y=0,previous_jitter_x=0,previous_jitter_y=0;
    float history_max=0.9f,history_min=0.05f,depth_relative=0.01f,depth_absolute=0.01f;
    float variance_gamma=1.25f,motion_scale=32.0f,reactive_scale=1.0f,reserved=0;
};
static_assert(sizeof(Parameters)==64,"Temporal root constants ABI");
enum Proof : std::uint32_t {
    SceneOnly=1,LinearDepth=2,MotionPreviousPixels=4,ExpectedPreviousDepth=8,
    GeometryJitter=16,DynamicMotionAndReactive=32,AllProofs=63
};
enum class Reset : std::uint32_t { None,FirstFrame,MissingInputs,InvalidFrame,CameraCut,Extent,Projection,Sequence,Settings,Aborted };
struct Frame {
    std::uint64_t sequence=0,projection_epoch=0;
    std::uint32_t width=0,height=0,proofs=0;
    float jitter_x=0,jitter_y=0;
    bool camera_cut=false,enabled=false;
};
struct Decision {bool dispatch=false,reuse=false;Reset reset=Reset::MissingInputs;Parameters parameters;};
// Prepare never makes unfinished GPU data valid. Commit only after successful
// execution/fence ownership transfer; Abort discards continuity explicitly.
class HistoryState {
public:
    Decision Prepare(const Frame&,const Parameters& quality);
    bool Commit(const Frame&);
    void Abort();
private:
    Frame previous_{},pending_{};Parameters quality_{},pending_quality_{};
    bool valid_=false,pending_valid_=false;Reset invalidation_=Reset::FirstFrame;
};
std::array<float,2> Jitter(std::uint64_t sequence);
bool Valid(const Parameters&);
bool Resolve(const Image&,const History&,const Parameters&,Image& output,History& next);
}
