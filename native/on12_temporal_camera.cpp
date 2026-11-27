#include "on12_temporal_camera.h"
#include <algorithm>
#include <cmath>

namespace On12Graphics::Temporal {
namespace {
bool Extent(std::uint32_t w,std::uint32_t h){return w&&h&&w<=16384&&h<=16384;}
bool JitterValid(float x,float y){return std::isfinite(x)&&std::isfinite(y)&&std::fabs(x)<=0.5f&&std::fabs(y)<=0.5f;}
bool MatrixValid(const CameraMatrix& m){
    for(float x:m)if(!std::isfinite(x))return false;
    const double norm=static_cast<double>(m[3])*m[3]+static_cast<double>(m[7])*m[7]+static_cast<double>(m[11])*m[11];
    // Positive clipW must measure view-space distance. Reject orthographic or
    // arbitrarily rescaled matrices instead of silently changing depth units.
    return std::fabs(norm-1)<0.001;
}
}
bool ValidCamera(const CameraParameters& p){
    if(!Extent(p.width,p.height)||!JitterValid(p.jitter_x,p.jitter_y)||!std::isfinite(p.normalized_depth_scale)
        ||p.normalized_depth_scale<=0||p.normalized_depth_scale>1e9f||p.reserved!=0
        ||(p.raster_origin_x!=0&&p.raster_origin_x!=0.5f)||p.raster_origin_y!=p.raster_origin_x)return false;
    for(float value:p.previous_clip_from_xyw)if(!std::isfinite(value))return false;
    return true;
}
bool BuildCamera(const CameraMatrix& current,const CameraMatrix& previous,float depth_scale,
                 std::uint32_t width,std::uint32_t height,float jx,float jy,float origin,CameraParameters& output){
    output={};
    if(!MatrixValid(current)||!MatrixValid(previous))return false;
    // Invert the x/y/w rows; this also works with the game's existing off-center
    // projection terms. Neither a guessed FOV nor a guessed near plane is used.
    const unsigned rows[]{0,1,3};double a[3][3]{},inverse[3][3]{};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)a[r][c]=current[c*4+rows[r]];
    const double det=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(!std::isfinite(det)||std::fabs(det)<1e-10)return false;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){
        const unsigned r1=(c+1)%3,r2=(c+2)%3,c1=(r+1)%3,c2=(r+2)%3;
        inverse[r][c]=(a[r1][c1]*a[r2][c2]-a[r1][c2]*a[r2][c1])/det;
    }
    double world[4][4]{};world[3][3]=1;
    for(unsigned r=0;r<3;++r){for(unsigned c=0;c<3;++c){world[r][c]=inverse[r][c];world[r][3]-=inverse[r][c]*current[12+rows[c]];}}
    CameraParameters p;p.width=width;p.height=height;p.jitter_x=jx;p.jitter_y=jy;p.normalized_depth_scale=depth_scale;p.raster_origin_x=p.raster_origin_y=origin;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){double value=0;for(unsigned k=0;k<4;++k)value+=static_cast<double>(previous[k*4+r])*world[k][c];p.previous_clip_from_xyw[r*4+c]=static_cast<float>(value);}
    if(!ValidCamera(p))return false;output=p;return true;
}
bool CameraMotion(const CameraParameters& p,std::uint32_t x,std::uint32_t y,float depth,float confidence,Motion& motion,float& linear){
    motion={};linear=0;
    if(!ValidCamera(p)||x>=p.width||y>=p.height||!std::isfinite(depth)||depth<=0||depth>=1)return false;
    linear=depth*p.normalized_depth_scale;if(!std::isfinite(linear)||linear<=0){linear=0;return false;}
    if(!std::isfinite(confidence)||confidence<=0)return true;
    const float current_x=static_cast<float>(x)-p.jitter_x,current_y=static_cast<float>(y)-p.jitter_y;
    const float ndc_x=(current_x+p.raster_origin_x)*2/p.width-1,ndc_y=1-(current_y+p.raster_origin_y)*2/p.height;
    const float in[]{ndc_x*linear,ndc_y*linear,linear,1};float clip[4]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)clip[r]+=p.previous_clip_from_xyw[r*4+c]*in[c];
    for(float value:clip)if(!std::isfinite(value))return true;
    if(clip[3]<=0||clip[2]<0||clip[2]>clip[3])return true;
    const float px=(clip[0]/clip[3]*0.5f+0.5f)*p.width-p.raster_origin_x;
    const float py=(-clip[1]/clip[3]*0.5f+0.5f)*p.height-p.raster_origin_y;
    if(!std::isfinite(px)||!std::isfinite(py))return true;
    motion={px-current_x,py-current_y,clip[3],(std::min)(1.0f,confidence)};return true;
}
bool JitterCamera(const CameraMatrix& input,std::uint32_t w,std::uint32_t h,float x,float y,CameraMatrix& output){
    if(!MatrixValid(input)||!Extent(w,h)||!JitterValid(x,y))return false;
    CameraMatrix result=input;
    for(unsigned c=0;c<4;++c){result[c*4]+=2*x/w*input[c*4+3];result[c*4+1]-=2*y/h*input[c*4+3];}
    output=result;return true;
}
}
