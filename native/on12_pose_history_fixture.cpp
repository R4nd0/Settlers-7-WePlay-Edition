#include "on12_pose_history.h"
#include <cstdio>
#include <limits>
namespace {
unsigned checks=0;
bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"pose history check failed at %u\n",line);return value;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
}
int main(){
    using namespace On12Graphics;ShaderPose before,now;
    for(size_t n=0;n<before.floats.size();++n)before.floats[n]=static_cast<float>(n)*.25f;
    for(size_t n=0;n<before.integers.size();++n)before.integers[n]=static_cast<std::int32_t>(n)-32;
    before.integers[0]=-16777216;before.integers[1]=16777216;
    for(size_t n=0;n<before.booleans.size();++n)before.booleans[n]=n%2?-1:0;
    PackedShaderPose packed{};REQUIRE(PackShaderPose(before,packed));
    for(size_t n=0;n<1024;++n)REQUIRE(packed[n]==before.floats[n]);
    for(size_t n=0;n<64;++n)REQUIRE(packed[1024+n]==static_cast<float>(before.integers[n]));
    for(size_t n=0;n<16;++n)REQUIRE(packed[1088+n*4]==(n%2?1:0)&&packed[1089+n*4]==0&&packed[1090+n*4]==0&&packed[1091+n*4]==0);
    auto invalid=before;invalid.integers[0]=-16777217;const auto unchanged=packed;REQUIRE(!PackShaderPose(invalid,packed)&&packed==unchanged);
    invalid=before;invalid.floats[0]=(std::numeric_limits<float>::quiet_NaN)();REQUIRE(!PackShaderPose(invalid,packed)&&packed==unchanged);
    invalid.floats[0]=(std::numeric_limits<float>::infinity)();REQUIRE(!PackShaderPose(invalid,packed));
    PoseKey key{1,2,3,4};PoseHistory history(2);
    REQUIRE(!history.Begin(0,1)&&!history.Begin(1,0));REQUIRE(history.Begin(100,7));
    REQUIRE(history.Observe({},before,AllPoseProofs).status==PoseStatus::Unsupported);
    REQUIRE(history.Observe(key,before,StableVertices).status==PoseStatus::Unsupported);
    auto first=history.Observe(key,before,AllPoseProofs);REQUIRE(first.status==PoseStatus::FirstSeen&&!first.previous);
    REQUIRE(history.Commit()&&history.Begin(101,7));now=before;now.floats[68*4+12]+=10;now.floats[80*4+3]-=3;now.floats[63*4]+=.1f;
    auto second=history.Observe(key,now,AllPoseProofs);REQUIRE(second.status==PoseStatus::Previous&&second.previous&&*second.previous==unchanged);
    auto duplicate=history.Observe(key,now,AllPoseProofs);REQUIRE(duplicate.status==PoseStatus::Duplicate&&duplicate.previous==second.previous&&history.Healthy());
    auto conflicting=now;conflicting.floats[80*4]+=1;REQUIRE(history.Observe(key,conflicting,AllPoseProofs).status==PoseStatus::Conflict&&!history.Commit());
    REQUIRE(history.Begin(102,7)&&history.Observe(key,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Commit());
    REQUIRE(history.Begin(103,7));auto reused=key;reused.object=9;REQUIRE(history.Observe(reused,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Commit());
    REQUIRE(history.Begin(104,7)&&history.Observe(reused,now,AllPoseProofs).status==PoseStatus::Previous);
    auto changed_mesh=reused;changed_mesh.geometry=99;REQUIRE(history.Observe(changed_mesh,now,AllPoseProofs).status==PoseStatus::FirstSeen);
    REQUIRE(history.Entries()==2&&history.Commit());
    REQUIRE(history.Begin(105,7));auto new_key=key;new_key.object=111;
    REQUIRE(history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::Capacity&&history.Commit());
    REQUIRE(history.Begin(106,7)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Entries()==2&&history.Commit());
    REQUIRE(history.Begin(107,7)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::Previous);
    REQUIRE(history.Observe(new_key,now,StableVertices).status==PoseStatus::Unsupported&&!history.Commit());
    REQUIRE(history.Begin(108,7)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Commit());
    REQUIRE(history.Begin(110,7)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Commit());
    REQUIRE(history.Begin(111,8)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Commit());
    REQUIRE(history.Begin(112,8)&&!history.Begin(113,8)&&!history.Commit());
    REQUIRE(history.Begin(114,8)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::FirstSeen);history.Abort();
    REQUIRE(history.Begin(115,8)&&history.Observe(new_key,now,AllPoseProofs).status==PoseStatus::FirstSeen&&history.Commit());
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"complete_banks\":true,\"exact_integer_guard\":true,\"address_reuse_rejected\":true,\"duplicate_conflict_aborts_frame\":true,\"geometry_version_required\":true,\"game_taa_enabled\":false}\n",checks);
}
