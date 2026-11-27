#include "on12_temporal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace On12Graphics::Temporal {
namespace {
bool Finite(float f){return std::isfinite(f);}
float Clamp(float x,float a,float b){return (std::max)(a,(std::min)(x,b));}
float Linear(float x){return x<=0.04045f?x/12.92f:std::pow((x+0.055f)/1.055f,2.4f);}
float Encoded(float x){x=Clamp(x,0,1);return x<=0.0031308f?x*12.92f:1.055f*std::pow(x,1.0f/2.4f)-0.055f;}
using Triple=std::array<float,3>;
Triple Decode(Color c,bool srgb){return srgb?Triple{Linear(c.r),Linear(c.g),Linear(c.b)}:Triple{c.r,c.g,c.b};}
Triple YCoCg(Triple c){return{0.25f*c[0]+0.5f*c[1]+0.25f*c[2],0.5f*c[0]-0.5f*c[2],-0.25f*c[0]+0.5f*c[1]-0.25f*c[2]};}
Triple Rgb(Triple c){return{c[0]+c[1]-c[2],c[0]+c[2],c[0]-c[1]-c[2]};}
bool ValidColor(Color c){return Finite(c.r)&&Finite(c.g)&&Finite(c.b)&&Finite(c.a)&&c.r>=0&&c.r<=1&&c.g>=0&&c.g<=1&&c.b>=0&&c.b<=1&&c.a>=0&&c.a<=1;}
float Radical(unsigned index,unsigned base){float value=0,factor=1;while(index){factor/=base;value+=factor*(index%base);index/=base;}return value;}
bool SameFrame(const Frame& a,const Frame& b){return a.sequence==b.sequence&&a.projection_epoch==b.projection_epoch&&a.width==b.width&&a.height==b.height&&a.proofs==b.proofs&&a.jitter_x==b.jitter_x&&a.jitter_y==b.jitter_y&&a.enabled==b.enabled&&a.camera_cut==b.camera_cut;}
bool Bilinear(const History& history,unsigned w,unsigned h,float px,float py,float expected,float threshold,Color& result){
    const float x=px-0.5f,y=py-0.5f;const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));const float fx=x-std::floor(x),fy=y-std::floor(y);
    result={0,0,0,0};for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto sx=static_cast<unsigned>((std::max)(0,(std::min)(static_cast<int>(w)-1,ix+dx)));
        const auto sy=static_cast<unsigned>((std::max)(0,(std::min)(static_cast<int>(h)-1,iy+dy)));const float weight=(dx?fx:1-fx)*(dy?fy:1-fy);if(weight<=0)continue;
        const auto i=static_cast<size_t>(sy)*w+sx;const float z=history.depth[i];const auto c=history.color[i];
        // Every contributing bilinear tap must belong to this surface. Testing
        // only the nearest depth lets foreground history bleed into a fence.
        if(!Finite(z)||z<=0||std::fabs(z-expected)>threshold||!ValidColor(c))return false;
        result.r+=c.r*weight;result.g+=c.g*weight;result.b+=c.b*weight;result.a+=c.a*weight;}return true;
}
}
std::array<float,2> Jitter(std::uint64_t sequence){const unsigned phase=static_cast<unsigned>(sequence%8)+1;return{Radical(phase,2)-0.5f,Radical(phase,3)-0.5f};}
bool Valid(const Parameters& p){
    return p.width&&p.height&&p.width<=16384&&p.height<=16384&&p.history_valid<=1&&p.color_srgb<=1
        &&Finite(p.jitter_x)&&Finite(p.jitter_y)&&Finite(p.previous_jitter_x)&&Finite(p.previous_jitter_y)
        &&std::fabs(p.jitter_x)<=0.5f&&std::fabs(p.jitter_y)<=0.5f&&std::fabs(p.previous_jitter_x)<=0.5f&&std::fabs(p.previous_jitter_y)<=0.5f
        &&Finite(p.history_min)&&Finite(p.history_max)&&p.history_min>=0&&p.history_max<=0.95f&&p.history_min<=p.history_max
        &&Finite(p.depth_relative)&&p.depth_relative>=0&&p.depth_relative<=0.25f&&Finite(p.depth_absolute)&&p.depth_absolute>=0&&p.depth_absolute<=100
        &&Finite(p.variance_gamma)&&p.variance_gamma>=0.5f&&p.variance_gamma<=3&&Finite(p.motion_scale)&&p.motion_scale>=1&&p.motion_scale<=256
        &&Finite(p.reactive_scale)&&p.reactive_scale>=0&&p.reactive_scale<=4&&p.reserved==0;
}
Decision HistoryState::Prepare(const Frame& frame,const Parameters& quality){
    Decision d;pending_valid_=false;d.parameters=quality;d.parameters.width=frame.width;d.parameters.height=frame.height;
    if(!frame.enabled||(frame.proofs&AllProofs)!=AllProofs||(frame.proofs&~AllProofs)){valid_=false;invalidation_=d.reset=Reset::MissingInputs;return d;}
    const auto jitter=Jitter(frame.sequence);
    if(!frame.sequence||!frame.projection_epoch||!Finite(frame.jitter_x)||!Finite(frame.jitter_y)||std::fabs(frame.jitter_x-jitter[0])>1e-6f||std::fabs(frame.jitter_y-jitter[1])>1e-6f||!Valid(d.parameters)){
        valid_=false;invalidation_=d.reset=Reset::InvalidFrame;return d;}
    d.dispatch=true;d.reset=valid_?Reset::None:invalidation_;
    if(valid_){
        if(frame.camera_cut)d.reset=Reset::CameraCut;
        else if(frame.width!=previous_.width||frame.height!=previous_.height)d.reset=Reset::Extent;
        else if(frame.projection_epoch!=previous_.projection_epoch)d.reset=Reset::Projection;
        else if(previous_.sequence==(std::numeric_limits<std::uint64_t>::max)()||frame.sequence!=previous_.sequence+1)d.reset=Reset::Sequence;
        else if(std::memcmp(&quality,&quality_,sizeof(quality)))d.reset=Reset::Settings;
    }
    d.reuse=valid_&&d.reset==Reset::None;d.parameters.history_valid=d.reuse?1u:0u;
    d.parameters.jitter_x=frame.jitter_x;d.parameters.jitter_y=frame.jitter_y;
    d.parameters.previous_jitter_x=d.reuse?previous_.jitter_x:frame.jitter_x;d.parameters.previous_jitter_y=d.reuse?previous_.jitter_y:frame.jitter_y;
    if(d.reset!=Reset::None)valid_=false;
    pending_=frame;pending_valid_=true;pending_quality_=quality;return d;
}
bool HistoryState::Commit(const Frame& frame){if(!pending_valid_||!SameFrame(frame,pending_))return false;previous_=frame;quality_=pending_quality_;valid_=true;pending_valid_=false;invalidation_=Reset::None;return true;}
void HistoryState::Abort(){valid_=pending_valid_=false;invalidation_=Reset::Aborted;}
bool Resolve(const Image& image,const History& history,const Parameters& p,Image& output,History& next){
    const auto count=static_cast<size_t>(p.width)*p.height;
    if(!Valid(p)||image.width!=p.width||image.height!=p.height||image.color.size()!=count||image.depth.size()!=count||image.motion.size()!=count||image.reactive.size()!=count
        ||(p.history_valid&&(history.color.size()!=count||history.depth.size()!=count)))return false;
    if(&image==&output||&history==&next)return false;
    for(const auto& c:image.color)if(!ValidColor(c))return false;
    output={};output.width=p.width;output.height=p.height;output.color.resize(count);next.color.resize(count);next.depth.resize(count);
    for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width;++x){const size_t i=static_cast<size_t>(y)*p.width+x;const Color current=image.color[i];const auto linear=Decode(current,p.color_srgb!=0);auto result=linear;
        const auto motion=image.motion[i];const float reactive=image.reactive[i],depth=image.depth[i];float weight=0;
        const float px=x+0.5f+motion.x+p.previous_jitter_x-p.jitter_x,py=y+0.5f+motion.y+p.previous_jitter_y-p.jitter_y;
        if(p.history_valid&&Finite(depth)&&depth>0&&Finite(motion.x)&&Finite(motion.y)&&Finite(motion.previous_depth)&&motion.previous_depth>0&&Finite(motion.confidence)&&motion.confidence>0
            &&Finite(reactive)&&px>=0.5f&&py>=0.5f&&px<=p.width-0.5f&&py<=p.height-0.5f){
            const float threshold=(std::max)(p.depth_absolute,p.depth_relative*motion.previous_depth);
            Color previous;
            if(Bilinear(history,p.width,p.height,px,py,motion.previous_depth,threshold,previous)){
                Triple minimum{1,1,1},maximum{-1,-1,-1},mean{},squares{};
                for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){const unsigned sx=static_cast<unsigned>((std::max)(0,(std::min)(static_cast<int>(p.width)-1,static_cast<int>(x)+ox)));
                    const unsigned sy=static_cast<unsigned>((std::max)(0,(std::min)(static_cast<int>(p.height)-1,static_cast<int>(y)+oy)));const auto n=YCoCg(Decode(image.color[sy*p.width+sx],p.color_srgb!=0));
                    for(unsigned c=0;c<3;++c){minimum[c]=(std::min)(minimum[c],n[c]);maximum[c]=(std::max)(maximum[c],n[c]);mean[c]+=n[c]/9;squares[c]+=n[c]*n[c]/9;}}
                auto clipped=YCoCg({previous.r,previous.g,previous.b});const auto center=YCoCg(linear);
                for(unsigned c=0;c<3;++c){const float sigma=std::sqrt((std::max)(0.0f,squares[c]-mean[c]*mean[c]));clipped[c]=Clamp(clipped[c],(std::max)(minimum[c],mean[c]-p.variance_gamma*sigma),(std::min)(maximum[c],mean[c]+p.variance_gamma*sigma));}
                const float difference=std::fabs(clipped[0]-center[0])/((maximum[0]-minimum[0])+0.001f);
                const float speed=std::sqrt(motion.x*motion.x+motion.y*motion.y);
                weight=Clamp(p.history_max/(1+2*difference+speed/p.motion_scale),p.history_min,p.history_max)
                    *(1-Clamp(reactive*p.reactive_scale,0,1))*Clamp(motion.confidence,0,1);
                const auto rgb=Rgb(clipped);for(unsigned c=0;c<3;++c)result[c]=Clamp(linear[c]*(1-weight)+rgb[c]*weight,0,1);
            }
        }
        next.color[i]={result[0],result[1],result[2],current.a};next.depth[i]=Finite(depth)&&depth>0?depth:0;
        output.color[i]={p.color_srgb?Encoded(result[0]):result[0],p.color_srgb?Encoded(result[1]):result[1],p.color_srgb?Encoded(result[2]):result[2],current.a};
    }return true;
}
}
