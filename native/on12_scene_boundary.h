#pragma once
#include "water_on12_identity.h"
#include "on12_scene_projection.h"
#include "on12_projection_binding.h"
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <vector>

namespace On12Graphics {
// Exact original programs, not blend-state or draw-count guesses.
class SceneBoundary {
public:
    enum class Role { Other, Ui, Composite };
    void Vertex(IDirect3DVertexShader9* value){vertex_=Classify(value,true,&geometry_,&projection_);}
    void Pixel(IDirect3DPixelShader9* value){pixel_=Classify(value,false);}
    bool CachedVertex(IDirect3DVertexShader9* value){return Cached(value,true,vertex_,&geometry_,&projection_);}
    bool CachedPixel(IDirect3DPixelShader9* value){return Cached(value,false,pixel_);}
    void Refresh(IDirect3DDevice9* device){Microsoft::WRL::ComPtr<IDirect3DVertexShader9> v;Microsoft::WRL::ComPtr<IDirect3DPixelShader9> p;
        if(SUCCEEDED(device->GetVertexShader(&v)))Vertex(v.Get());else{vertex_=Role::Other;geometry_=false;projection_=0;}
        if(SUCCEEDED(device->GetPixelShader(&p)))Pixel(p.Get());else pixel_=Role::Other;}
    Role Current()const{return vertex_==pixel_?vertex_:Role::Other;}
    bool Geometry()const{return geometry_;}
    UINT ProjectionRegister()const{return projection_;}
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
    void FixtureUi(IDirect3DVertexShader9* v,IDirect3DPixelShader9* p){fixture_vertex_=v;fixture_pixel_=p;}
#endif
    void Reset(){for(auto& x:cache_)x={};count_=0;vertex_=pixel_=Role::Other;geometry_=false;projection_=0;}
    bool NativeUi(IDirect3DDevice9* device,UINT width,UINT height)const{
        if(Current()!=Role::Ui)return false;
        std::array<float,16> m{};if(FAILED(device->GetVertexShaderConstantF(64,m.data(),4)))return false;
        const float x=2.0f/width,y=-2.0f/height;
        return std::isfinite(m[0])&&std::isfinite(m[5])&&std::fabs(m[0]-x)<1e-7f&&std::fabs(m[5]-y)<1e-7f;
    }
private:
    struct Entry{Microsoft::WRL::ComPtr<IUnknown> object;Role role=Role::Other;bool vertex=false,geometry=false;UINT projection=0;};
    std::array<Entry,512> cache_{};size_t count_=0;Role vertex_=Role::Other,pixel_=Role::Other;
    bool geometry_=false;
    UINT projection_=0;
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
    Microsoft::WRL::ComPtr<IDirect3DVertexShader9> fixture_vertex_;
    Microsoft::WRL::ComPtr<IDirect3DPixelShader9> fixture_pixel_;
#endif
    bool Cached(IUnknown* object,bool vertex,Role& role,bool* geometry=nullptr,UINT* projection=nullptr){if(!object){role=Role::Other;if(geometry)*geometry=false;if(projection)*projection=0;return true;}
        for(size_t i=0;i<count_;++i)if(cache_[i].object.Get()==object&&cache_[i].vertex==vertex){role=cache_[i].role;if(geometry)*geometry=cache_[i].geometry;if(projection)*projection=cache_[i].projection;return true;}return false;}
    template<class T> Role Classify(T* shader,bool vertex,bool* geometry=nullptr,UINT* projection=nullptr){
        if(geometry)*geometry=false;
        if(projection)*projection=0;
        if(!shader)return Role::Other;
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
        if((vertex&&static_cast<IUnknown*>(shader)==fixture_vertex_.Get())||(!vertex&&static_cast<IUnknown*>(shader)==fixture_pixel_.Get()))return Role::Ui;
#endif
        for(size_t i=0;i<count_;++i)if(cache_[i].object.Get()==shader&&cache_[i].vertex==vertex){if(geometry)*geometry=cache_[i].geometry;if(projection)*projection=cache_[i].projection;return cache_[i].role;}
        UINT bytes=0;Role role=Role::Other;bool geometry_candidate=false;UINT projection_register=0;
        if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes&&bytes<=65536&&(vertex||bytes==1108||bytes==2872)){
            std::vector<unsigned char> code(bytes);std::string hash;
            if(SUCCEEDED(shader->GetFunction(code.data(),&bytes))&&bytes==code.size()){
                if(vertex){projection_register=ProjectionRegisterForShader(code.data(),bytes);geometry_candidate=projection_register!=0;}
                if(((vertex&&(bytes==484||bytes==824))||(!vertex&&(bytes==1108||bytes==2872)))&&WaterOn12Identity::Hash(code.data(),bytes,hash)){
                if(hash==(vertex?"0fafd8564b812ef5d8609329052df4be741f015c3c3b51b0946420e0fe6880f3":"69b5a31786c16faf4c06b1592c6925f745230ac27d7729db78d330fdf084aae6"))role=Role::Ui;
                if(hash==(vertex?"8b74b5b4a2057fc68df5d35f0a9599a18907ad81990f1ee9ac509010086df51e":"9907ad7dca3ced9b1e3e331bee8065ed6c7b606e5275a7aab52fe30ca96bb58b"))role=Role::Composite;
                }
            }
        }
        if(role!=Role::Other){geometry_candidate=false;projection_register=0;}if(geometry)*geometry=geometry_candidate;if(projection)*projection=projection_register;
        if(count_<cache_.size()){auto& e=cache_[count_++];e.object=shader;e.vertex=vertex;e.role=role;e.geometry=geometry_candidate;e.projection=projection_register;}return role;
    }
};
}
