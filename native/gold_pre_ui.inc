// SPDX-License-Identifier: MIT
// WePlay Community. Gold-only, opt-in Vulkan graphics candidate.
#pragma comment(lib,"user32.lib")
namespace pre_ui {
std::recursive_mutex runtime_lock;
effect_runtime* runtime=nullptr;bool ambiguous=false,injected=false;
struct Views {device* owner;resource image;resource_view linear,srgb;};
std::vector<Views> views;
unsigned successes=0,failures=0;
void InitRuntime(effect_runtime* candidate){std::lock_guard<std::recursive_mutex> guard(runtime_lock);if(candidate->get_device()->get_api()!=device_api::vulkan)return;DWORD pid=0;GetWindowThreadProcessId(static_cast<HWND>(candidate->get_hwnd()),&pid);if(pid!=GetCurrentProcessId())return;if(runtime&&runtime!=candidate){ambiguous=true;return;}runtime=candidate;}
void DestroyRuntime(effect_runtime* candidate){std::lock_guard<std::recursive_mutex> guard(runtime_lock);if(runtime==candidate)runtime=nullptr;}
void DestroyResource(device* owner,resource image){std::lock_guard<std::recursive_mutex> guard(runtime_lock);for(auto i=views.begin();i!=views.end();){if(i->owner==owner&&i->image==image){owner->destroy_resource_view(i->linear);owner->destroy_resource_view(i->srgb);i=views.erase(i);}else ++i;}}
void DestroyDevice(device* owner){
 std::lock_guard<std::recursive_mutex> guard(runtime_lock);
 for(auto i=views.begin();i!=views.end();){if(i->owner==owner){owner->destroy_resource_view(i->linear);owner->destroy_resource_view(i->srgb);i=views.erase(i);}else ++i;}
}
bool BeginPass(command_list* cmd,uint32_t count,const render_pass_render_target_desc* rt,const render_pass_depth_stencil_desc* ds,render_pass_flags flags){
 std::lock_guard<std::recursive_mutex> runtime_guard(runtime_lock);
 auto s=Get(cmd);if(!s)return false;s->target={0};s->draws=0;s->scene=false;
 if(!runtime||ambiguous||flags!=render_pass_flags::none||count!=1||!rt||!rt[0].view.handle||(ds&&ds->view.handle)||cmd->get_device()!=runtime->get_device())return false;
 uint32_t w=0,h=0;runtime->get_screenshot_width_and_height(&w,&h);auto desc=cmd->get_device()->get_resource_desc(cmd->get_device()->get_resource_from_view(rt[0].view));
 if(w==1920&&h==1080&&desc.texture.width==w&&desc.texture.height==h&&desc.texture.samples==1)s->target=rt[0].view;
 return false;
}
bool EndPass(command_list* cmd){if(auto s=Get(cmd)){s->target={0};s->scene=false;}return false;}
void Present(effect_runtime* candidate){std::lock_guard<std::recursive_mutex> guard(runtime_lock);if(candidate==runtime)injected=false;}
void DrawCommon(command_list* cmd,unsigned count,bool indexed){
 std::lock_guard<std::recursive_mutex> runtime_guard(runtime_lock);
 atmosphere::FpScope fp;if(!fp.valid)return;
 EnsureInitialized();
 auto s=Get(cmd);if(!s||!s->target.handle||!profile||profile!=&atmosphere::Gold||injected||!runtime||ambiguous)return;
 gold_pipeline::Pipeline p{};{std::lock_guard<std::mutex> guard(gold_pipeline::lock);auto b=gold_pipeline::bound.find(reinterpret_cast<uintptr_t>(cmd));if(b!=gold_pipeline::bound.end()){auto found=gold_pipeline::pipelines.find(b->second);if(found!=gold_pipeline::pipelines.end()&&found->second.owner==cmd->get_device())p=found->second;}}
 if(s->draws++==0){s->scene=indexed&&count==4&&p.vs==0xEB119BE0B02B6DFBull&&p.ps==0x864F3C8922061465ull&&!p.blend;return;}
 if(!s->scene||indexed||p.vs!=0x9B76C89F01E9C5E0ull||p.ps!=0x70C7F2C6B3BB3CFCull||p.blend!=1||p.src!=6||p.dst!=7)return;
 s->scene=false;
 if(!s->valid||s->viewports.empty()||s->scissors.empty()||s->tables.empty()){if(++failures<=3){char line[256];sprintf_s(line,"GOLD-PREUI state capture rejected valid=%u pipelines=%u dynamic=%u viewports=%u scissors=%u tables=%u constants=%u",s->valid?1u:0u,static_cast<unsigned>(s->pipelines.size()),static_cast<unsigned>(s->dynamic.size()),static_cast<unsigned>(s->viewports.size()),static_cast<unsigned>(s->scissors.size()),static_cast<unsigned>(s->tables.size()),static_cast<unsigned>(s->constants.size()));reshade::log::message(reshade::log::level::info,line);}return;}
 auto device=cmd->get_device();auto image=device->get_resource_from_view(s->target);Views* selected=nullptr;
 for(auto& v:views)if(v.owner==device&&v.image==image)selected=&v;
 if(!selected){if(views.size()>=8)return;const auto desc=device->get_resource_desc(image);Views v{device,image,{0},{0}};
  if(!device->create_resource_view(image,resource_usage::render_target,resource_view_desc(format_to_default_typed(desc.texture.format,0)),&v.linear))return;
  if(!device->create_resource_view(image,resource_usage::render_target,resource_view_desc(format_to_default_typed(desc.texture.format,1)),&v.srgb)){device->destroy_resource_view(v.linear);return;}
  views.push_back(v);selected=&views.back();}
 const auto saved=*s;const auto pair=*selected;
 internal=true;injected=true;
 cmd->end_render_pass();
 runtime->render_effects(cmd,pair.linear,pair.srgb);
 render_pass_render_target_desc resume{};resume.view=saved.target;resume.load_op=render_pass_load_op::load;resume.store_op=render_pass_store_op::store;
 cmd->begin_render_pass(1,&resume,nullptr);saved.apply(cmd);
 internal=false;
 if(++successes<=3||successes%300==0){char line[160];sprintf_s(line,"GOLD-PREUI applied=%u descriptors=%u constants=%u",successes,static_cast<unsigned>(saved.tables.size()),static_cast<unsigned>(saved.constants.size()));reshade::log::message(reshade::log::level::info,line);}
}
bool Draw(command_list* cmd,uint32_t count,uint32_t,uint32_t,uint32_t){DrawCommon(cmd,count,false);return false;}
bool DrawIndexed(command_list* cmd,uint32_t count,uint32_t,uint32_t,int32_t,uint32_t){DrawCommon(cmd,count,true);return false;}
void Register(){using reshade::addon_event;
#define PREUI_EVENT(e,f) reshade::register_event<addon_event::e>(f)
 PREUI_EVENT(init_command_list,InitCommand);PREUI_EVENT(destroy_command_list,DestroyCommand);PREUI_EVENT(reset_command_list,ResetCommand);
 PREUI_EVENT(bind_pipeline,PipelineBind);PREUI_EVENT(bind_pipeline_states,DynamicBind);PREUI_EVENT(bind_viewports,ViewportBind);PREUI_EVENT(bind_scissor_rects,ScissorBind);
 PREUI_EVENT(bind_descriptor_tables,TablesBind);PREUI_EVENT(push_constants,ConstantsBind);PREUI_EVENT(push_descriptors,PushDescriptors);PREUI_EVENT(bind_index_buffer,IndexBind);PREUI_EVENT(bind_vertex_buffers,VertexBind);
 PREUI_EVENT(init_effect_runtime,InitRuntime);PREUI_EVENT(destroy_effect_runtime,DestroyRuntime);PREUI_EVENT(destroy_resource,DestroyResource);PREUI_EVENT(destroy_device,DestroyDevice);
 PREUI_EVENT(begin_render_pass,BeginPass);PREUI_EVENT(end_render_pass,EndPass);PREUI_EVENT(draw,Draw);PREUI_EVENT(draw_indexed,DrawIndexed);PREUI_EVENT(reshade_present,Present);
#undef PREUI_EVENT
}
void Unregister(){using reshade::addon_event;
#define PREUI_EVENT(e,f) reshade::unregister_event<addon_event::e>(f)
 PREUI_EVENT(init_command_list,InitCommand);PREUI_EVENT(destroy_command_list,DestroyCommand);PREUI_EVENT(reset_command_list,ResetCommand);
 PREUI_EVENT(bind_pipeline,PipelineBind);PREUI_EVENT(bind_pipeline_states,DynamicBind);PREUI_EVENT(bind_viewports,ViewportBind);PREUI_EVENT(bind_scissor_rects,ScissorBind);
 PREUI_EVENT(bind_descriptor_tables,TablesBind);PREUI_EVENT(push_constants,ConstantsBind);PREUI_EVENT(push_descriptors,PushDescriptors);PREUI_EVENT(bind_index_buffer,IndexBind);PREUI_EVENT(bind_vertex_buffers,VertexBind);
 PREUI_EVENT(init_effect_runtime,InitRuntime);PREUI_EVENT(destroy_effect_runtime,DestroyRuntime);PREUI_EVENT(destroy_resource,DestroyResource);PREUI_EVENT(destroy_device,DestroyDevice);
 PREUI_EVENT(begin_render_pass,BeginPass);PREUI_EVENT(end_render_pass,EndPass);PREUI_EVENT(draw,Draw);PREUI_EVENT(draw_indexed,DrawIndexed);PREUI_EVENT(reshade_present,Present);
#undef PREUI_EVENT
}
}
