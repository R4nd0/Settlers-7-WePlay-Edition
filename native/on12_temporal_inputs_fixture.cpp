// CPU-only command, lifetime and same-frame pairing tests. No device created.
#include "on12_temporal_inputs.h"
#include <cstdio>
#include <limits>
#include <sstream>

using namespace On12Graphics;
namespace {unsigned checks=0;bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"temporal input check failed at %u\n",line);return value;}}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
int main(){
    TemporalInputProbe probe;unsigned reads=0;
    auto read=[&]{++reads;TemporalBoundaryInputs value;value.logical.viewport.Width=1920;value.physical.viewport.Width=1280;
        value.logical.candidate_view_projection[0]=(std::numeric_limits<float>::quiet_NaN)();value.physical.candidate_camera={1,2,3,4000};return value;};
    REQUIRE(!probe.Composite(1,read)&&!probe.Ui(1,read)&&!reads);
    TemporalInputCommand command;command.sequence=1;command.frames=2;
    REQUIRE(probe.Arm(command)&&probe.Active()&&!probe.Count());
    REQUIRE(!probe.Composite(0,read)&&!probe.Ui(1,read)&&!reads);
    REQUIRE(probe.Composite(10,read)&&reads==1);
    REQUIRE(!probe.Composite(10,read)&&!probe.Ui(11,read)&&reads==1);
    REQUIRE(probe.Ui(10,read)&&reads==2&&probe.Count()==1&&probe.Active());
    REQUIRE(!probe.Composite(10,read)&&!probe.Ui(10,read)&&reads==2);
    REQUIRE(probe.Composite(11,read)&&probe.Composite(12,read)&&reads==4);
    REQUIRE(!probe.Ui(11,read)&&probe.Ui(12,read)&&reads==5&&!probe.Active()&&probe.Count()==2);
    REQUIRE(!probe.Composite(13,read)&&!probe.Ui(13,read)&&reads==5);
    std::ostringstream report;probe.Json(report);const auto text=report.str();
    REQUIRE(text.find("\"proofs\":0")!=std::string::npos&&text.find("\"game_taa_enabled\":false")!=std::string::npos);
    REQUIRE(text.find("\"discarded\":1")!=std::string::npos&&text.find("\"candidate_c12\":[null,")!=std::string::npos);
    REQUIRE(text.find("\"frame\":10")!=std::string::npos&&text.find("\"frame\":12")!=std::string::npos);
    REQUIRE(text.find("\"candidate_c17\":[1,2,3,4000]")!=std::string::npos);
    REQUIRE(text.find("\"viewport\":[0,0,1920,0]")!=std::string::npos&&text.find("\"viewport\":[0,0,1280,0]")!=std::string::npos);
    REQUIRE(!probe.Arm(command)&&probe.Count()==2);
    for(unsigned test=0;test<4;++test){auto invalid=command;invalid.sequence=2;if(test==0)invalid.size=20;if(test==1)invalid.version=2;if(test==2)invalid.frames=0;if(test==3)invalid.frames=5;
        REQUIRE(!probe.Arm(invalid)&&probe.Sequence()==1&&probe.Count()==2);}
    command.sequence=2;command.frames=4;REQUIRE(probe.Arm(command)&&probe.Count()==0);
    REQUIRE(probe.Composite(20,read));probe.Invalidate();REQUIRE(!probe.Active()&&!probe.Ui(20,read)&&!probe.Count());
    command.sequence=3;REQUIRE(probe.Arm(command));
    for(std::uint64_t i=30;i<34;++i)REQUIRE(probe.Composite(i,read)&&probe.Ui(i,read));
    REQUIRE(!probe.Active()&&probe.Count()==4&&!probe.Composite(34,read));
    const auto missing=ReadTemporalDeviceInputs(nullptr);REQUIRE(FAILED(missing.matrix_result)&&FAILED(missing.viewport_result));
    TemporalInputDepthCommand depth_command;depth_command.sequence=4;depth_command.frames=2;
    REQUIRE(probe.Arm(depth_command)&&probe.Depth().requested&&!probe.Depth().attempted);
    REQUIRE(!probe.BeginDepth(40)&&probe.Composite(40,read)&&probe.BeginDepth(40));
    REQUIRE(!probe.BeginDepth(40)&&probe.Depth().frame==40&&probe.Depth().scale==4000);
    auto result=probe.Depth();result.result=S_OK;result.written=true;result.width=8;result.height=8;result.finite=result.interior=64;
    probe.DepthResult(result);REQUIRE(probe.Ui(40,read)&&probe.Depth().written);
    REQUIRE(probe.Composite(41,read)&&!probe.BeginDepth(41)&&probe.Ui(41,read));
    depth_command.sequence=5;depth_command.flags=2;REQUIRE(!probe.Arm(depth_command)&&probe.Sequence()==4);
    command.sequence=5;command.frames=1;REQUIRE(probe.Arm(command)&&!probe.Depth().requested);
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"command_bytes\":16,\"bounded_pairs\":4}\n",checks);return 0;
}
