// SPDX-License-Identifier: MIT
// WePlay Community. Gold-only, opt-in Vulkan graphics candidate.
namespace pre_ui {
using namespace reshade::api;
thread_local bool internal=false;
struct Table {shader_stage stages;pipeline_layout layout;uint32_t first;std::vector<descriptor_table> values;std::vector<uint32_t> offsets;};
struct Constants {shader_stage stages;pipeline_layout layout;uint32_t parameter,first;std::vector<uint32_t> values;};
struct __declspec(uuid("82D1ED58-0AE2-4EA1-8C72-C81D359C1B96")) State {
 bool valid=true,scene=false;unsigned draws=0;resource_view target{0};
 std::unordered_map<pipeline_stage,pipeline> pipelines;
 std::unordered_map<dynamic_state,uint32_t> dynamic;
 std::vector<viewport> viewports;std::vector<rect> scissors;
 std::vector<Table> tables;std::vector<Constants> constants;
 resource index{0};uint64_t index_offset=0;uint32_t index_size=0;
 std::vector<resource> vertices;std::vector<uint64_t> offsets;std::vector<uint32_t> strides;
 void apply(command_list* cmd)const{
  for(const auto& item:pipelines)cmd->bind_pipeline(item.first,item.second);
  for(const auto& item:dynamic)cmd->bind_pipeline_state(item.first,item.second);
  if(!viewports.empty())cmd->bind_viewports(0,static_cast<uint32_t>(viewports.size()),viewports.data());
  if(!scissors.empty())cmd->bind_scissor_rects(0,static_cast<uint32_t>(scissors.size()),scissors.data());
  for(const auto& t:tables)cmd->bind_descriptor_tables2(t.stages,t.layout,t.first,static_cast<uint32_t>(t.values.size()),t.values.data(),static_cast<uint32_t>(t.offsets.size()),t.offsets.data());
  for(const auto& c:constants)cmd->push_constants(c.stages,c.layout,c.parameter,c.first,static_cast<uint32_t>(c.values.size()),c.values.data());
  if(index_size)cmd->bind_index_buffer(index,index_offset,index_size);
  if(!vertices.empty())cmd->bind_vertex_buffers(0,static_cast<uint32_t>(vertices.size()),vertices.data(),offsets.data(),strides.data());
 }
};
void InitCommand(command_list* cmd){cmd->create_private_data<State>();}
void DestroyCommand(command_list* cmd){cmd->destroy_private_data<State>();}
void ResetCommand(command_list* cmd){if(!internal)*cmd->get_private_data<State>()=State{};}
State* Get(command_list* cmd){return internal?nullptr:cmd->get_private_data<State>();}
void PipelineBind(command_list* cmd,pipeline_stage stages,pipeline p){if(auto s=Get(cmd)){if(s->pipelines.size()>=16&&!s->pipelines.count(stages))s->valid=false;else s->pipelines[stages]=p;}}
void DynamicBind(command_list* cmd,uint32_t count,const dynamic_state* states,const uint32_t* values){if(auto s=Get(cmd)){if(count>128){s->valid=false;return;}for(uint32_t i=0;i<count;++i){if(s->dynamic.size()>=128&&!s->dynamic.count(states[i])){s->valid=false;break;}s->dynamic[states[i]]=values[i];}}}
void ViewportBind(command_list* cmd,uint32_t first,uint32_t count,const viewport* values){if(auto s=Get(cmd)){if(first>16||count>16-first){s->valid=false;return;}s->viewports.resize((std::max)(s->viewports.size(),static_cast<size_t>(first+count)));for(uint32_t i=0;i<count;++i)s->viewports[first+i]=values[i];}}
void ScissorBind(command_list* cmd,uint32_t first,uint32_t count,const rect* values){if(auto s=Get(cmd)){if(first>16||count>16-first){s->valid=false;return;}s->scissors.resize((std::max)(s->scissors.size(),static_cast<size_t>(first+count)));for(uint32_t i=0;i<count;++i)s->scissors[first+i]=values[i];}}
void TablesBind(command_list* cmd,shader_stage stages,pipeline_layout layout,uint32_t first,uint32_t count,const descriptor_table* values,uint32_t dynamic_count,const uint32_t* offsets){if(auto s=Get(cmd)){
 if(count>32||first>32-count||dynamic_count>64){s->valid=false;return;}
 // Preserve chronological order for overlapping binds, including dynamic offsets.
 for(auto i=s->tables.begin();i!=s->tables.end();){if(i->stages==stages&&(i->layout!=layout||(i->first==first&&i->values.size()==count)))i=s->tables.erase(i);else ++i;}
 if(s->tables.size()>=32){s->valid=false;return;}Table t{stages,layout,first,{},{}};if(count)t.values.assign(values,values+count);if(dynamic_count)t.offsets.assign(offsets,offsets+dynamic_count);s->tables.push_back(std::move(t));
}}
void ConstantsBind(command_list* cmd,shader_stage stages,pipeline_layout layout,uint32_t parameter,uint32_t first,uint32_t count,const void* values){if(auto s=Get(cmd)){
 if(count>256||first>256-count){s->valid=false;return;}
 for(auto i=s->constants.begin();i!=s->constants.end();){if(i->stages==stages&&(i->layout!=layout||(i->parameter==parameter&&i->first==first&&i->values.size()==count)))i=s->constants.erase(i);else ++i;}
 if(s->constants.size()>=32){s->valid=false;return;}Constants c{stages,layout,parameter,first,{}};if(count){auto data=static_cast<const uint32_t*>(values);c.values.assign(data,data+count);}s->constants.push_back(std::move(c));
}}
void PushDescriptors(command_list* cmd,shader_stage,pipeline_layout,uint32_t,const descriptor_table_update&){if(auto s=Get(cmd))s->valid=false;}
void IndexBind(command_list* cmd,resource b,uint64_t offset,uint32_t size){if(auto s=Get(cmd)){s->index=b;s->index_offset=offset;s->index_size=size;}}
void VertexBind(command_list* cmd,uint32_t first,uint32_t count,const resource* buffers,const uint64_t* offsets,const uint32_t* strides){if(auto s=Get(cmd)){
 if(first>16||count>16-first){s->valid=false;return;}const auto size=(std::max)(s->vertices.size(),static_cast<size_t>(first+count));s->vertices.resize(size);s->offsets.resize(size);s->strides.resize(size);
 for(uint32_t i=0;i<count;++i){s->vertices[first+i]=buffers[i];s->offsets[first+i]=offsets[i];s->strides[first+i]=strides?strides[i]:0;}
}}
}
