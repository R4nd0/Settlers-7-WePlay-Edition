// SPDX-License-Identifier: MIT
// WePlay Community. Gold-only, opt-in Vulkan graphics candidate.
namespace gold_pipeline {
std::mutex lock;
struct Pipeline { uint64_t vs=0,ps=0;unsigned blend=0,src=0,dst=0;bool has_blend=false;reshade::api::device* owner=nullptr; };
std::unordered_map<uint64_t,Pipeline> pipelines;
std::unordered_map<uintptr_t,uint64_t> bound;
uint64_t Hash(const reshade::api::shader_desc& shader){if(!shader.code||shader.code_size>1024*1024)return 0;uint64_t h=14695981039346656037ull;auto bytes=static_cast<const unsigned char*>(shader.code);for(size_t i=0;i<shader.code_size;++i){h^=bytes[i];h*=1099511628211ull;}return h;}
void InitPipeline(reshade::api::device* owner,reshade::api::pipeline_layout,uint32_t count,const reshade::api::pipeline_subobject* sub,reshade::api::pipeline pipe){
 Pipeline p{};p.owner=owner;using reshade::api::pipeline_subobject_type;
 for(uint32_t i=0;i<count;++i){if(!sub[i].count||!sub[i].data)continue;
  if(sub[i].type==pipeline_subobject_type::vertex_shader)p.vs=Hash(*static_cast<const reshade::api::shader_desc*>(sub[i].data));
  if(sub[i].type==pipeline_subobject_type::pixel_shader)p.ps=Hash(*static_cast<const reshade::api::shader_desc*>(sub[i].data));
  if(sub[i].type==pipeline_subobject_type::blend_state){auto& b=*static_cast<const reshade::api::blend_desc*>(sub[i].data);p.has_blend=true;p.blend=b.blend_enable[0];p.src=static_cast<unsigned>(b.source_color_blend_factor[0]);p.dst=static_cast<unsigned>(b.dest_color_blend_factor[0]);}}
 {std::lock_guard<std::mutex> guard(lock);
  for(uint32_t i=0;i<count;++i)if(sub[i].type==pipeline_subobject_type::libraries&&sub[i].data&&sub[i].count<=64){
   auto libraries=static_cast<const reshade::api::pipeline*>(sub[i].data);
   for(uint32_t j=0;j<sub[i].count;++j){auto found=pipelines.find(libraries[j].handle);if(found==pipelines.end()||found->second.owner!=owner)continue;const auto& inherited=found->second;
    if(!p.vs)p.vs=inherited.vs;if(!p.ps)p.ps=inherited.ps;
    if(!p.has_blend&&inherited.has_blend){p.has_blend=true;p.blend=inherited.blend;p.src=inherited.src;p.dst=inherited.dst;}
   }
  }
 }
 std::lock_guard<std::mutex> guard(lock);if(pipelines.size()<16384||pipelines.count(pipe.handle))pipelines[pipe.handle]=p;
}
void BindPipeline(reshade::api::command_list* cmd,reshade::api::pipeline_stage,reshade::api::pipeline pipe){std::lock_guard<std::mutex> guard(lock);const auto key=reinterpret_cast<uintptr_t>(cmd);if(bound.size()<128||bound.count(key))bound[key]=pipe.handle;}
void ClearCommand(reshade::api::command_list* cmd){std::lock_guard<std::mutex> guard(lock);bound.erase(reinterpret_cast<uintptr_t>(cmd));}
void DestroyPipeline(reshade::api::device* owner,reshade::api::pipeline pipe){std::lock_guard<std::mutex> guard(lock);auto i=pipelines.find(pipe.handle);if(i!=pipelines.end()&&i->second.owner==owner)pipelines.erase(i);}
void DestroyDevice(reshade::api::device* owner){
 std::lock_guard<std::mutex> guard(lock);
 for(auto i=pipelines.begin();i!=pipelines.end();)if(i->second.owner==owner)i=pipelines.erase(i);else ++i;
}
#ifndef WEPLAY_GOLD_PRE_UI_PIPELINE_FIXTURE
void Register(){using reshade::addon_event;
#define GOLD_PIPE_EVENT(e,f) reshade::register_event<addon_event::e>(f)
 GOLD_PIPE_EVENT(init_pipeline,InitPipeline);GOLD_PIPE_EVENT(destroy_pipeline,DestroyPipeline);GOLD_PIPE_EVENT(destroy_device,DestroyDevice);
 GOLD_PIPE_EVENT(bind_pipeline,BindPipeline);GOLD_PIPE_EVENT(reset_command_list,ClearCommand);GOLD_PIPE_EVENT(close_command_list,ClearCommand);GOLD_PIPE_EVENT(destroy_command_list,ClearCommand);
#undef GOLD_PIPE_EVENT
}
void Unregister(){using reshade::addon_event;
#define GOLD_PIPE_EVENT(e,f) reshade::unregister_event<addon_event::e>(f)
 GOLD_PIPE_EVENT(init_pipeline,InitPipeline);GOLD_PIPE_EVENT(destroy_pipeline,DestroyPipeline);GOLD_PIPE_EVENT(destroy_device,DestroyDevice);
 GOLD_PIPE_EVENT(bind_pipeline,BindPipeline);GOLD_PIPE_EVENT(reset_command_list,ClearCommand);GOLD_PIPE_EVENT(close_command_list,ClearCommand);GOLD_PIPE_EVENT(destroy_command_list,ClearCommand);
#undef GOLD_PIPE_EVENT
}
#endif
}
