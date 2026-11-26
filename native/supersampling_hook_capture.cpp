#include "supersampling_hook_capture.h"
#include "supersampling_hook_config.h"
#include "atmosphere_fp.h"
#include <bcrypt.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace NativeGraphicsCapture {
namespace {
struct LastErrorScope {DWORD saved=GetLastError();~LastErrorScope(){SetLastError(saved);}};
template<class T> struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
};
void Check(HRESULT result,Result& capture) {
    if(FAILED(result)){capture.readback_hresult=result;throw std::runtime_error("readback_failed");}
}
struct Hash {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    Hash() {
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0
            || BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) {
            if(algorithm){BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;}
            throw std::runtime_error("hash_failed");
        }
    }
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    void add(const void* data,size_t bytes) {
        if(bytes>std::numeric_limits<ULONG>::max() || BCryptHashData(hash,
            static_cast<unsigned char*>(const_cast<void*>(data)),static_cast<ULONG>(bytes),0)<0)throw std::runtime_error("hash_failed");
    }
    void finish(std::array<char,65>& output) {
        std::array<unsigned char,32> digest{};
        if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)throw std::runtime_error("hash_failed");
        constexpr char hex[]="0123456789abcdef";
        for(size_t i=0;i<digest.size();++i){output[i*2]=hex[digest[i]>>4];output[i*2+1]=hex[digest[i]&15];}
    }
};
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit File(const std::wstring& path) {
        handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("capture_file_exists_or_unavailable");
    }
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    void write(const void* data,size_t bytes) {
        const auto* next=static_cast<const unsigned char*>(data);
        while(bytes) {
            const DWORD chunk=static_cast<DWORD>(std::min<size_t>(bytes,1024*1024));DWORD written=0;
            if(!WriteFile(handle,next,chunk,&written,nullptr) || written!=chunk)throw std::runtime_error("capture_write_failed");
            next+=chunk;bytes-=chunk;
        }
    }
    void flush(){if(!FlushFileBuffers(handle))throw std::runtime_error("capture_flush_failed");}
};

bool Paths(const wchar_t* base_report,DWORD sequence,std::wstring& prefix) {
    if(!base_report || !sequence)return false;
    wchar_t copied[512]{};
    const size_t length=wcsnlen_s(base_report,512);
    if(!length || length>=512)return false;
    std::copy(base_report,base_report+length,copied);
    if(!NativeGraphicsConfig::AbsolutePath(copied,L".json"))return false;
    const std::wstring report(copied);
    const size_t end=report.find_last_of(L"\\/");if(end==std::wstring::npos)return false;
    const std::wstring directory=report.substr(0,end);
    // Retain the configured private directory, rejecting ancestor redirection.
    for(size_t i=3;i<=directory.size();++i)if(i==directory.size() || directory[i]==L'\\' || directory[i]==L'/') {
        const DWORD attributes=GetFileAttributesW(directory.substr(0,i).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)
            || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    prefix=directory+L"\\capture-"+std::to_wstring(sequence);
    return prefix.size()+16<512;
}
bool Absent(const std::wstring& path) {
    SetLastError(ERROR_SUCCESS);
    return GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND;
}

std::vector<unsigned char> Pixels(IDirect3DDevice9* device,IDirect3DSurface9* surface,
                                  D3DSURFACE_DESC& description,Result& capture) {
    Check(surface->GetDesc(&description),capture);
    if(!description.Width || !description.Height || description.Width>3840 || description.Height>2160
        || (description.Format!=D3DFMT_A8R8G8B8 && description.Format!=D3DFMT_X8R8G8B8)
        || description.MultiSampleType!=D3DMULTISAMPLE_NONE)throw std::runtime_error("unsupported_capture_surface");
    Com<IDirect3DSurface9> staging;
    Check(device->CreateOffscreenPlainSurface(description.Width,description.Height,description.Format,
        D3DPOOL_SYSTEMMEM,&staging.value,nullptr),capture);
    Check(device->GetRenderTargetData(surface,staging.value),capture);
    std::vector<unsigned char> pixels(static_cast<size_t>(description.Width)*description.Height*4);
    D3DLOCKED_RECT locked{};
    Check(staging.value->LockRect(&locked,nullptr,D3DLOCK_READONLY),capture);
    if(!locked.pBits || locked.Pitch<0 || static_cast<size_t>(locked.Pitch)<static_cast<size_t>(description.Width)*4) {
        staging.value->UnlockRect();throw std::runtime_error("invalid_capture_pitch_or_pointer");
    }
    for(UINT y=0;y<description.Height;++y)std::memcpy(pixels.data()+static_cast<size_t>(y)*description.Width*4,
        static_cast<const unsigned char*>(locked.pBits)+static_cast<size_t>(y)*locked.Pitch,
        static_cast<size_t>(description.Width)*4);
    Check(staging.value->UnlockRect(),capture);
    return pixels;
}

void Bitmap(const std::wstring& path,const D3DSURFACE_DESC& desc,const std::vector<unsigned char>& pixels,
            std::array<char,65>& digest) {
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    info.biSize=sizeof(info);info.biWidth=static_cast<LONG>(desc.Width);info.biHeight=-static_cast<LONG>(desc.Height);
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=static_cast<DWORD>(pixels.size());
    File output(path);Hash hash;
    for(const auto& bytes:std::array<std::pair<const void*,size_t>,3>{{{&file,sizeof(file)},{&info,sizeof(info)},{pixels.data(),pixels.size()}}}) {
        output.write(bytes.first,bytes.second);hash.add(bytes.first,bytes.second);
    }
    output.flush();hash.finish(digest);
}

std::string Number(double value) {
    if(!std::isfinite(value))return "null";
    char buffer[64]{};
    const auto converted=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,17);
    return converted.ec==std::errc{}?std::string(buffer,converted.ptr):"null";
}
std::string FloatArray(const float* values,size_t count,bool available) {
    if(!available)return "null";
    std::string text="[";
    for(size_t i=0;i<count;++i){if(i)text+=",";text+=Number(values[i]);}
    return text+"]";
}
std::string Hex(const std::array<unsigned char,32>& data) {
    constexpr char hex[]="0123456789abcdef";std::string text;
    for(auto value:data){text+=hex[value>>4];text+=hex[value&15];}return text;
}
std::string LeafPalette(const Result& result) {
    const auto& g=result.graphics;const auto& s=result.material;
    if(g.effects_config_version!=4||!result.material_state_available||s.config_version!=4||s.selected_variant>2)return "null";
    const auto array=[](const std::array<std::uint64_t,3>& values) {
        return "["+std::to_string(values[0])+","+std::to_string(values[1])+","+std::to_string(values[2])+"]";
    };
    const bool drawn=g.effects_active&&g.effects_drawn_variant<3&&g.effects_drawn_frame==g.present_calls;
    return "{\"selected_variant\":"+std::to_string(s.selected_variant)
        +",\"drawn_variant\":"+(drawn?std::to_string(g.effects_drawn_variant):"null")
        +",\"drawn_frame\":"+std::to_string(g.effects_drawn_frame)
        +",\"frame_applications\":"+std::to_string(g.effects_variant_frame_applications)
        +",\"frame_restorations\":"+std::to_string(g.effects_variant_frame_restorations)
        +",\"frame_failed_draws\":"+std::to_string(g.effects_variant_frame_failed_draws)
        +",\"configured_program_sha256\":\""+Hex(s.variant_sha256[s.selected_variant])+"\""
        +",\"drawn_program_sha256\":"+(drawn?"\""+Hex(s.variant_sha256[g.effects_drawn_variant])+"\"":"null")
        +",\"module_epoch\":"+std::to_string(s.enable_changes)
        +",\"leaf_recipe_variant\":"+std::to_string(5+s.selected_variant)
        +",\"variant_applications\":"+array(s.variant_applications)
        +",\"variant_restorations\":"+array(s.variant_restorations)
        +",\"variant_successful_draws\":"+array(s.variant_successful_draws)
        +",\"variant_failed_draws\":"+array(s.variant_failed_draws)
        +",\"program_creations\":"+array(s.variant_creations)
        +",\"faulted\":"+std::to_string(s.faulted)+",\"shader_failures\":"+std::to_string(s.shader_failures)
        +",\"set_failures\":"+std::to_string(s.set_failures)+",\"restore_failures\":"+std::to_string(s.restore_failures)
        +",\"exceptions\":"+std::to_string(s.exceptions)+",\"active_transactions\":"+std::to_string(s.active_transactions)
        +",\"active_original_refs\":"+std::to_string(s.active_original_refs)+"}";
}
std::string SceneUniforms(const Result& result) {
    if(!result.scene_uniforms_available)return "null";
    const auto& s=result.scene_uniforms;
    return "{\"source\":\"original_river_shader_uniforms\",\"frame\":"+std::to_string(s.frame)
        +",\"available\":"+std::to_string(s.available)+",\"context_valid\":"+std::to_string(s.context_valid)
        +",\"successful_draw\":"+std::to_string(s.successful_draw)
        +",\"vertex_id\":"+std::to_string(s.vertex_id)+",\"pixel_id\":"+std::to_string(s.pixel_id)
        +",\"vertex_aliases\":"+std::to_string(s.vertex_aliases)+",\"pixel_aliases\":"+std::to_string(s.pixel_aliases)
        +",\"catalog_sha256\":\""+Hex(s.catalog_sha256)+"\",\"vertex_sha256\":\""+Hex(s.vertex_sha256)
        +"\",\"pixel_sha256\":\""+Hex(s.pixel_sha256)+"\",\"target\":["+std::to_string(s.target_width)+","+std::to_string(s.target_height)
        +"],\"viewport\":["+std::to_string(s.viewport_width)+","+std::to_string(s.viewport_height)
        +"],\"target_format\":"+std::to_string(s.target_format)+",\"alpha_blend\":"+std::to_string(s.alpha_blend)
        +",\"alpha_test\":"+std::to_string(s.alpha_test)+",\"srgb_write\":"+std::to_string(s.srgb_write)
        +",\"vs_constants_12_15\":"+FloatArray(s.vs_constants_12_15.data(),16,true)
        +",\"vs16_eye\":"+FloatArray(s.vs16_eye.data(),3,true)
        +",\"ps5_sun_direction\":"+FloatArray(s.ps5_sun_direction.data(),3,true)
        +",\"ps7_sun_specular\":"+FloatArray(s.ps7_sun_specular.data(),3,true)+"}";
}
std::string Comparison(const Result& result) {
    if(!result.comparison_state_available)return "null";
    const auto& s=result.comparison;
    return "{\"diagnostic_only\":true,\"frame\":"+std::to_string(s.frames)+",\"requested\":"+std::to_string(s.requested)
        +",\"active\":"+std::to_string(s.active)+",\"pinned\":"+std::to_string(s.pinned)
        +",\"faulted\":"+std::to_string(s.faulted)+",\"unsafe\":"+std::to_string(s.unsafe_state)
        +",\"epoch\":"+std::to_string(s.epoch)+",\"pinned_ps80\":"+FloatArray(s.pinned_ps80.data(),4,s.pinned!=0)
        +",\"pin_committed_frame\":"+std::to_string(s.pin_committed_frame)
        +",\"completed_frame_applications\":"+std::to_string(s.completed_frame_applications)
        +",\"completed_frame_restorations\":"+std::to_string(s.completed_frame_restorations)
        +",\"last_applied_frame\":"+std::to_string(s.last_applied_frame)
        +",\"last_restored_frame\":"+std::to_string(s.last_restored_frame)
        +",\"draws\":"+std::to_string(s.draws)+",\"matched\":"+std::to_string(s.matched)
        +",\"context_rejections\":"+std::to_string(s.context_rejections)+",\"input_rejections\":"+std::to_string(s.input_rejections)
        +",\"query_failures\":"+std::to_string(s.query_failures)+",\"cache_overflows\":"+std::to_string(s.cache_overflows)
        +",\"cached_refs\":"+std::to_string(s.cached_refs)+",\"applications\":"+std::to_string(s.applications)
        +",\"restorations\":"+std::to_string(s.restorations)+",\"failed_draws\":"+std::to_string(s.failed_draws)
        +",\"set_failures\":"+std::to_string(s.set_failures)+",\"restore_failures\":"+std::to_string(s.restore_failures)
        +",\"exceptions\":"+std::to_string(s.exceptions)+"}";
}
std::string Water(const Result& result) {
    if(!result.graphics.water_protocol_received)return "null";
    if(!result.water_state_available)return "null";
    const auto& s=result.water;
    const auto frame=[&](std::uint64_t value){return value?value+result.graphics.water_frame_origin:0;};
    return "{\"frame\":"+std::to_string(frame(s.completed_frame))+",\"requested\":"+std::to_string(s.requested)
        +",\"enabled\":"+std::to_string(s.enabled)+",\"variant\":"+std::to_string(s.variant)+",\"configured\":"+std::to_string(s.configured)
        +",\"epoch\":"+std::to_string(s.configuration_epoch)+",\"faulted\":"+std::to_string(s.faulted)
        +",\"unsafe\":"+std::to_string(s.unsafe_state)+",\"completed_applications\":"+std::to_string(s.completed_applications)
        +",\"completed_restorations\":"+std::to_string(s.completed_restorations)
        +",\"last_applied_frame\":"+std::to_string(frame(s.last_applied_frame))
        +",\"last_restored_frame\":"+std::to_string(frame(s.last_restored_frame))
        +",\"shader_creations\":"+std::to_string(s.shader_creations)+",\"shader_failures\":"+std::to_string(s.shader_failures)
        +",\"set_failures\":"+std::to_string(s.set_failures)+",\"restore_failures\":"+std::to_string(s.restore_failures)
        +",\"exceptions\":"+std::to_string(s.exceptions)+",\"pending_original_refs\":"+std::to_string(s.pending_original_refs)+"}";
}
std::string WaterPalette(const Result& result) {
    if(!result.water_state_available||result.water.config_version!=2||result.water.selected_variant>2)return "null";
    const auto& s=result.water;const auto frame=s.completed_frame?s.completed_frame+result.graphics.water_frame_origin:0;
    const bool drawn=s.enabled&&s.completed_drawn_variant<3&&frame==result.graphics.present_calls;
    const auto array=[](const std::array<std::uint64_t,3>& values){
        return "["+std::to_string(values[0])+","+std::to_string(values[1])+","+std::to_string(values[2])+"]";};
    return "{\"config_version\":2,\"selected_variant\":"+std::to_string(s.selected_variant)
        +",\"drawn_variant\":"+(drawn?std::to_string(s.completed_drawn_variant):"null")
        +",\"configured_program_sha256\":\""+Hex(s.variant_sha256[s.selected_variant])+"\""
        +",\"drawn_program_sha256\":"+(drawn?"\""+Hex(s.variant_sha256[s.completed_drawn_variant])+"\"":"null")
        +",\"frame\":"+std::to_string(frame)+",\"epoch\":"+std::to_string(s.configuration_epoch)
        +",\"completed_applications\":"+std::to_string(s.completed_applications)
        +",\"completed_restorations\":"+std::to_string(s.completed_restorations)
        +",\"completed_successful_draws\":"+std::to_string(s.completed_successful_draws)
        +",\"completed_failed_draws\":"+std::to_string(s.completed_failed_draws)
        +",\"variant_applications\":"+array(s.variant_applications)
        +",\"variant_restorations\":"+array(s.variant_restorations)
        +",\"variant_successful_draws\":"+array(s.variant_successful_draws)
        +",\"variant_failed_draws\":"+array(s.variant_failed_draws)
        +",\"program_creations\":"+array(s.variant_creations)
        +",\"faulted\":"+std::to_string(s.faulted)+",\"unsafe\":"+std::to_string(s.unsafe_state)
        +",\"shader_failures\":"+std::to_string(s.shader_failures)+",\"set_failures\":"+std::to_string(s.set_failures)
        +",\"restore_failures\":"+std::to_string(s.restore_failures)+",\"exceptions\":"+std::to_string(s.exceptions)
        +",\"active_transactions\":"+std::to_string(s.active_transactions)
        +",\"pending_original_refs\":"+std::to_string(s.pending_original_refs)+",\"cached_refs\":"+std::to_string(s.cached_refs)+"}";
}
std::string Shadow(const Result& result) {
    if(!result.graphics.shadow_protocol_received||!result.shadow_state_available)return "null";
    const auto& s=result.shadow;
    const auto frame=[&](std::uint64_t value){return value?value+result.graphics.shadow_frame_origin:0;};
    const auto words=[](const std::array<DWORD,4>& value){return "["+std::to_string(value[0])+","+
        std::to_string(value[1])+","+std::to_string(value[2])+","+std::to_string(value[3])+"]";};
    return "{\"diagnostic_only\":true,\"frame\":"+std::to_string(frame(s.completed_frame))
        +",\"requested\":"+std::to_string(s.requested)+",\"enabled\":"+std::to_string(s.enabled)
        +",\"epoch\":"+std::to_string(s.epoch)+",\"faulted\":"+std::to_string(s.faulted)
        +",\"unsafe\":"+std::to_string(s.unsafe_state)+",\"active_transactions\":"+std::to_string(s.active_transactions)
        +",\"completed_applications\":"+std::to_string(s.completed_applications)
        +",\"completed_restorations\":"+std::to_string(s.completed_restorations)
        +",\"last_applied_frame\":"+std::to_string(frame(s.last_applied_frame))
        +",\"last_restored_frame\":"+std::to_string(frame(s.last_restored_frame))
        +",\"frame_good\":"+std::to_string(result.graphics.shadow_frame_good)
        +",\"frame_successful_draws\":"+std::to_string(result.graphics.shadow_frame_successful_draws)
        +",\"frame_failed_draws\":"+std::to_string(result.graphics.shadow_frame_failed_draws)
        +",\"successful_draws\":"+std::to_string(s.successful_draws)+",\"failed_draws\":"+std::to_string(s.failed_draws)
        +",\"set_failures\":"+std::to_string(s.set_failures)+",\"restore_failures\":"+std::to_string(s.restore_failures)
        +",\"exceptions\":"+std::to_string(s.exceptions)+",\"query_failures\":"+std::to_string(s.query_failures)
        +",\"policy_rejections\":"+std::to_string(s.policy_rejections)
        +",\"original_ps57_bits\":"+words(s.last_original_bits)+",\"applied_ps57_bits\":"+words(s.last_applied_bits)
        +",\"sampler_available\":"+std::to_string(s.sampler_available)
        +",\"sampler_min\":"+std::to_string(s.sampler_min)+",\"sampler_mag\":"+std::to_string(s.sampler_mag)
        +",\"sampler_mip\":"+std::to_string(s.sampler_mip)
        +",\"sampler_address_u\":"+std::to_string(s.sampler_address_u)
        +",\"sampler_address_v\":"+std::to_string(s.sampler_address_v)+"}";
}
std::string Ambient(const Result& result) {
    if(!result.graphics.ambient_protocol_received||!result.ambient_state_available)return "null";
    const auto& s=result.ambient;
    const auto frame=[&](std::uint64_t value){return value?value+result.graphics.ambient_frame_origin:0;};
    const auto words=[](const std::array<DWORD,4>& value){return "["+std::to_string(value[0])+","+
        std::to_string(value[1])+","+std::to_string(value[2])+","+std::to_string(value[3])+"]";};
    return "{\"diagnostic_only\":true,\"frame\":"+std::to_string(frame(s.completed_frame))
        +",\"profile_version\":"+std::to_string(s.profile_version)
        +",\"requested\":"+std::to_string(s.requested)+",\"enabled\":"+std::to_string(s.enabled)
        +",\"epoch\":"+std::to_string(s.epoch)+",\"faulted\":"+std::to_string(s.faulted)
        +",\"unsafe\":"+std::to_string(s.unsafe_state)+",\"active_transactions\":"+std::to_string(s.active_transactions)
        +",\"completed_applications\":"+std::to_string(s.completed_applications)
        +",\"completed_restorations\":"+std::to_string(s.completed_restorations)
        +",\"last_applied_frame\":"+std::to_string(frame(s.last_applied_frame))
        +",\"last_restored_frame\":"+std::to_string(frame(s.last_restored_frame))
        +",\"frame_good\":"+std::to_string(result.graphics.ambient_frame_good)
        +",\"frame_successful_draws\":"+std::to_string(result.graphics.ambient_frame_successful_draws)
        +",\"frame_failed_draws\":"+std::to_string(result.graphics.ambient_frame_failed_draws)
        +",\"successful_draws\":"+std::to_string(s.successful_draws)+",\"failed_draws\":"+std::to_string(s.failed_draws)
        +",\"set_failures\":"+std::to_string(s.set_failures)+",\"restore_failures\":"+std::to_string(s.restore_failures)
        +",\"exceptions\":"+std::to_string(s.exceptions)+",\"query_failures\":"+std::to_string(s.query_failures)
        +",\"policy_rejections\":"+std::to_string(s.policy_rejections)
        +",\"original_ps81_bits\":"+words(s.last_original_bits)+",\"applied_ps81_bits\":"+words(s.last_applied_bits)
        +",\"sampler_available\":"+std::to_string(s.sampler_available)
        +",\"sampler_min\":"+std::to_string(s.sampler_min)+",\"sampler_mag\":"+std::to_string(s.sampler_mag)
        +",\"sampler_mip\":"+std::to_string(s.sampler_mip)
        +",\"sampler_address_u\":"+std::to_string(s.sampler_address_u)
        +",\"sampler_address_v\":"+std::to_string(s.sampler_address_v)+"}";
}
}

bool TargetAvailable(const wchar_t* base_report,DWORD sequence) noexcept {
    LastErrorScope error;
    try {
        std::wstring prefix;
        return Paths(base_report,sequence,prefix) && Absent(prefix+L"-source.bmp")
            && Absent(prefix+L"-output.bmp") && Absent(prefix+L".json");
    } catch(...){return false;}
}

bool Read(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* output,
          const wchar_t* base_report,DWORD sequence,const SupersamplingStats& graphics,Result& result) noexcept {
    result={};result.sequence=sequence;result.graphics=graphics;
    LastErrorScope error;
    atmosphere::FpScope fp;
    if(!fp.valid){result.status=3;return false;}
    QueryPerformanceFrequency(&result.frequency);QueryPerformanceCounter(&result.started);
    try {
        std::wstring prefix;
        if(!Paths(base_report,sequence,prefix) || !TargetAvailable(base_report,sequence))throw std::runtime_error("invalid_capture_target");
        result.receipt_path=prefix+L".json";
        GraphicsObservation::Read(result.observation);
        result.scene_uniforms_available=MaterialShaderObserver::CopyLatestFrameSnapshot(graphics.present_calls,&result.scene_uniforms)
            && result.scene_uniforms.available && result.scene_uniforms.context_valid
            && result.scene_uniforms.frame==graphics.present_calls;
        result.comparison_state_available=MaterialComparison::ReadState(&result.comparison)
            && result.comparison.initialized && result.comparison.frames==graphics.present_calls;
        result.water_state_available=graphics.water_protocol_received&&WaterShaderEffects::ReadStats(&result.water)
            &&result.water.initialized;
        result.shadow_state_available=graphics.shadow_protocol_received&&ShadowFilterEffects::ReadStats(&result.shadow)
            &&result.shadow.initialized;
        result.ambient_state_available=graphics.ambient_protocol_received&&AmbientOcclusionEffects::ReadStats(&result.ambient)
            &&result.ambient.initialized;
        result.material_state_available=graphics.effects_config_version==4&&MaterialShaderEffects::ReadStats(&result.material)
            &&result.material.config_version==4&&result.material.initialized_flags==MaterialShaderEffects::Leaf;
        Hash state;state.add(&graphics,sizeof(graphics));state.finish(result.state_sha256);
        D3DSURFACE_DESC high{},low{};
        auto source_pixels=Pixels(device,source,high,result);
        result.source_width=high.Width;result.source_height=high.Height;result.format=high.Format;
        Bitmap(prefix+L"-source.bmp",high,source_pixels,result.source_sha256);
        if(source==output) {
            low=high;Bitmap(prefix+L"-output.bmp",low,source_pixels,result.output_sha256);
        } else {
            auto output_pixels=Pixels(device,output,low,result);
            Bitmap(prefix+L"-output.bmp",low,output_pixels,result.output_sha256);
        }
        result.output_width=low.Width;result.output_height=low.Height;
        QueryPerformanceCounter(&result.finished);return true;
    } catch(...) {
        result.status=1;QueryPerformanceCounter(&result.finished);return false;
    }
}

void Finish(Result& result,HRESULT attempted_path_result,HRESULT final_caller_result,
            bool fallback_used,bool filtered_path,bool diagnostic_overlay_drawn) noexcept {
    LastErrorScope error;
    atmosphere::FpScope fp;
    if(!fp.valid){result.status=3;return;}
    try {
        if(result.receipt_path.empty())return;
        const auto& o=result.observation;const bool camera=(o.available&GraphicsObservation::CameraReady)!=0;
        const bool context=(o.available&GraphicsObservation::ContextSizeReady)!=0;
        const bool lod=(o.available&GraphicsObservation::LodReady)!=0;
        const bool sun=(o.available&GraphicsObservation::SunAzimuthReady)!=0;
        const double milliseconds=result.frequency.QuadPart>0?static_cast<double>(result.finished.QuadPart-result.started.QuadPart)*1000/result.frequency.QuadPart:0;
        std::string text="{\"format\":\"s7-native-frame-capture-v1\",\"draw_counter_semantics\":\"successful_nonzero\",\"sequence\":"+std::to_string(result.sequence)
            +",\"complete\":"+(result.status?"false":"true")+",\"status\":"+std::to_string(result.status)
            +",\"frame\":"+std::to_string(result.graphics.present_calls)+",\"edition\":"+std::to_string(result.graphics.edition)
            +",\"source\":["+std::to_string(result.source_width)+","+std::to_string(result.source_height)
            +"],\"output\":["+std::to_string(result.output_width)+","+std::to_string(result.output_height)
            +"],\"d3d9_format\":"+std::to_string(result.format)+",\"source_bmp_sha256\":\""+result.source_sha256.data()
            +"\",\"output_bmp_sha256\":\""+result.output_sha256.data()+"\",\"state_sha256\":\""+result.state_sha256.data()
            +"\",\"present_hresult\":"+std::to_string(static_cast<DWORD>(final_caller_result))
            +",\"attempted_path_hresult\":"+std::to_string(static_cast<DWORD>(attempted_path_result))
            +",\"filtered_present_hresult\":"+(filtered_path?std::to_string(static_cast<DWORD>(attempted_path_result)):"null")
            +",\"final_present_hresult\":"+std::to_string(static_cast<DWORD>(final_caller_result))
            +",\"fallback_used\":"+(fallback_used?"true":"false")
            +",\"presentation_path\":\""+(fallback_used?"fallback_original":(filtered_path?"filtered_output":"original"))+"\""
            +",\"filtered_api_succeeded\":"+((filtered_path && !fallback_used && SUCCEEDED(attempted_path_result))?"true":"false")
            +",\"readback_hresult\":"+std::to_string(static_cast<DWORD>(result.readback_hresult))
            +",\"capture_stall_ms\":"+Number(milliseconds)+",\"exclude_from_pacing\":true,\"desktop_capture\":false"
            +",\"diagnostic_overlay_drawn_after_capture\":"+(diagnostic_overlay_drawn?"true":"false")
            +",\"diagnostic_overlay_excluded_from_capture\":"+(diagnostic_overlay_drawn?"true":"false")
            +",\"client\":["+std::to_string(result.graphics.client_width)+","+std::to_string(result.graphics.client_height)
            +"],\"viewport\":["+std::to_string(result.graphics.viewport_width)+","+std::to_string(result.graphics.viewport_height)
            +"],\"selected_target\":["+std::to_string(result.graphics.target_width)+","+std::to_string(result.graphics.target_height)
            +"],\"high_resolution_draws\":"+std::to_string(result.graphics.high_resolution_draws)
            +",\"indexed_draws\":"+std::to_string(result.graphics.indexed_draws)
            +",\"prior_output_present_calls\":"+std::to_string(result.graphics.output_present_calls)
            +",\"effects_requested\":"+std::to_string(result.graphics.effects_requested)
            +",\"water\":"+Water(result)
            +",\"water_palette\":"+WaterPalette(result)
            +",\"shadow\":"+Shadow(result)
            +",\"ambient\":"+Ambient(result)
            +",\"effects_configured\":"+std::to_string(result.graphics.effects_configured)
            +",\"effects_config_version\":"+std::to_string(result.graphics.effects_config_version)
            +",\"effects_selected_variant\":"+(result.graphics.effects_config_version==4&&result.graphics.effects_selected_variant<3?std::to_string(result.graphics.effects_selected_variant):"null")
            +",\"effects_drawn_variant\":"+(result.graphics.effects_config_version==4&&result.graphics.effects_drawn_variant<3?std::to_string(result.graphics.effects_drawn_variant):"null")
            +",\"effects_drawn_frame\":"+std::to_string(result.graphics.effects_drawn_frame)
            +",\"effects_variant_frame_applications\":"+std::to_string(result.graphics.effects_variant_frame_applications)
            +",\"effects_variant_frame_restorations\":"+std::to_string(result.graphics.effects_variant_frame_restorations)
            +",\"effects_variant_frame_failed_draws\":"+std::to_string(result.graphics.effects_variant_frame_failed_draws)
            +",\"leaf_palette\":"+LeafPalette(result)
            +",\"effects_active\":"+std::to_string(result.graphics.effects_active)
            +",\"effects_revision\":"+std::to_string(result.graphics.effects_revision)
            +",\"effects_unsafe\":"+std::to_string(result.graphics.effects_unsafe)
            +",\"effects_drawn_revision\":"+std::to_string(result.graphics.effects_drawn_revision)
            +",\"effects_drawn_active\":"+std::to_string(result.graphics.effects_drawn_active)
            +",\"effects_frame_has_geometry\":"+std::to_string(result.graphics.effects_frame_has_geometry)
            +",\"effects_faulted\":"+std::to_string(result.graphics.effects_faulted)
            +",\"effects_disabled_reason\":"+std::to_string(result.graphics.effects_disabled_reason)
            +",\"effects_module_faulted\":"+std::to_string(result.graphics.effects_module_faulted)
            +",\"effects_module_shader_failures\":"+std::to_string(result.graphics.effects_module_shader_failures)
            +",\"effects_module_set_failures\":"+std::to_string(result.graphics.effects_module_set_failures)
            +",\"effects_module_restore_failures\":"+std::to_string(result.graphics.effects_module_restore_failures)
            +",\"effects_module_exceptions\":"+std::to_string(result.graphics.effects_module_exceptions)
            +",\"observation_available\":"+std::to_string(o.available)+",\"observation_status\":"+std::to_string(o.status)
            +",\"scene_uniforms\":"+SceneUniforms(result)
            +",\"comparison\":"+Comparison(result)
            +",\"camera_world_source\":\"vision_main_context_export\",\"camera_world\":"+FloatArray(o.camera_world,16,camera)+",\"fov_native\":"+FloatArray(o.fov_native,2,camera)
            +",\"clip_native\":"+FloatArray(o.clip_native,2,camera)+",\"context_size\":"
            +(context?("["+std::to_string(o.context_width)+","+std::to_string(o.context_height)+"]"):"null")
            +",\"lod_scale\":"+(lod?Number(o.lod_scale):"null")+",\"sun_azimuth\":"+(sun?Number(o.sun_azimuth):"null")
            +",\"sun_clock_ms\":"+(sun?Number(o.sun_clock_ms):"null")+",\"sun_elevation\":null,\"exposure\":null,\"simulation_turn\":null}\n";
        File file(result.receipt_path);file.write(text.data(),text.size());file.flush();
    } catch(...) {result.status=2;}
}
}
