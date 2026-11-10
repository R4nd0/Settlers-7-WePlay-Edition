// SPDX-License-Identifier: MIT
// WePlay Community. Gold-only, opt-in Vulkan graphics candidate.
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <cstdio>
namespace reshade::api {
enum class shader_stage{graphics=17,compute=32};enum class pipeline_stage{graphics};enum class dynamic_state{one,two};
struct pipeline_layout{uint64_t handle;bool operator!=(pipeline_layout b)const{return handle!=b.handle;}};
struct pipeline{uint64_t handle;};struct descriptor_table{uint64_t handle;};struct resource{uint64_t handle;};struct resource_view{uint64_t handle;};
struct viewport{float x,y,width,height,min_depth,max_depth;};struct rect{int32_t left,top,right,bottom;};struct descriptor_table_update{};
struct TableCall{shader_stage stage;uint64_t layout;uint32_t first;std::vector<descriptor_table> tables;std::vector<uint32_t> offsets;};
struct command_list{
 void* data=nullptr;std::vector<TableCall> table_calls;uint32_t pushed[256]{};std::vector<viewport> viewport_values;std::vector<rect> scissor_values;uint64_t index=0;uint32_t index_size=0;uint64_t index_offset=0;unsigned vertices=0,pipeline_calls=0,dynamic_calls=0;
 template<class T>void create_private_data(){data=new T;}
 template<class T>T* get_private_data(){return static_cast<T*>(data);}
 template<class T>void destroy_private_data(){delete static_cast<T*>(data);data=nullptr;}
 void bind_pipeline(pipeline_stage,pipeline){++pipeline_calls;}
 void bind_pipeline_state(dynamic_state,uint32_t){++dynamic_calls;}
 void bind_viewports(uint32_t,uint32_t count,const viewport* values){viewport_values.assign(values,values+count);}
 void bind_scissor_rects(uint32_t,uint32_t count,const rect* values){scissor_values.assign(values,values+count);}
 void bind_descriptor_tables2(shader_stage stages,pipeline_layout layout,uint32_t first,uint32_t count,const descriptor_table* values,uint32_t offset_count,const uint32_t* offsets){TableCall c{stages,layout.handle,first,{},{}};if(count)c.tables.assign(values,values+count);if(offset_count)c.offsets.assign(offsets,offsets+offset_count);table_calls.push_back(c);}
 void push_constants(shader_stage,pipeline_layout,uint32_t,uint32_t first,uint32_t count,const void* values){auto p=static_cast<const uint32_t*>(values);for(uint32_t i=0;i<count;++i)pushed[first+i]=p[i];}
 void bind_index_buffer(resource b,uint64_t offset,uint32_t size){index=b.handle;index_offset=offset;index_size=size;}
 void bind_vertex_buffers(uint32_t,uint32_t count,const resource*,const uint64_t*,const uint32_t*){vertices=count;}
};
}
#include "gold_pre_ui_state.inc"
int main(){using namespace pre_ui;
 command_list source,sink;InitCommand(&source);auto s=source.get_private_data<State>();
 PipelineBind(&source,pipeline_stage::graphics,{7});dynamic_state key=dynamic_state::one;uint32_t dynamic=0;DynamicBind(&source,1,&key,&dynamic);
 viewport v{0,1080,1920,-1080,0,1};rect r{0,0,1920,1080};ViewportBind(&source,0,1,&v);ScissorBind(&source,0,1,&r);
 descriptor_table table[2]={{11},{22}};uint32_t offsets[2]={256,512};TablesBind(&source,shader_stage::graphics,{1},0,2,table,2,offsets);
 descriptor_table second{33};uint32_t offset=768;TablesBind(&source,shader_stage::graphics,{1},1,1,&second,1,&offset);
 uint32_t full[4]={1,2,3,4},partial[2]={88,99};ConstantsBind(&source,shader_stage::graphics,{1},2,0,4,full);ConstantsBind(&source,shader_stage::graphics,{1},2,1,2,partial);
 IndexBind(&source,{44},32,2);resource vertex{55};uint64_t vertex_offset=64;uint32_t stride=24;VertexBind(&source,0,1,&vertex,&vertex_offset,&stride);
 const auto snapshot=*s;snapshot.apply(&sink);
 assert(sink.pipeline_calls==1&&sink.dynamic_calls==1&&sink.viewport_values[0].height==-1080&&sink.scissor_values[0].right==1920);
 assert(sink.table_calls.size()==2&&sink.table_calls[0].first==0&&sink.table_calls[0].tables[0].handle==11&&sink.table_calls[0].offsets[0]==256&&sink.table_calls[0].offsets[1]==512&&sink.table_calls[1].first==1&&sink.table_calls[1].tables[0].handle==33&&sink.table_calls[1].offsets[0]==768);
 assert(sink.pushed[0]==1&&sink.pushed[1]==88&&sink.pushed[2]==99&&sink.pushed[3]==4&&sink.index==44&&sink.index_offset==32&&sink.index_size==2&&sink.vertices==1);
 ConstantsBind(&source,shader_stage::graphics,{1},2,0,4,full);s->apply(&sink);assert(sink.pushed[1]==2&&sink.pushed[2]==3);
 offsets[0]=999;full[0]=999;assert(snapshot.tables[0].offsets[0]==256&&snapshot.constants[0].values[0]==1);
 internal=true;PipelineBind(&source,pipeline_stage::graphics,{999});internal=false;assert(s->pipelines[pipeline_stage::graphics].handle==7);
 ResetCommand(&source);assert(s->valid&&s->tables.empty()&&s->constants.empty()&&s->viewports.empty()&&s->index_size==0);
 PushDescriptors(&source,shader_stage::graphics,{1},0,{});assert(!s->valid);ResetCommand(&source);
 ViewportBind(&source,16,1,&v);assert(!s->valid);ResetCommand(&source);
 TablesBind(&source,shader_stage::graphics,{1},0,33,nullptr,0,nullptr);assert(!s->valid&&s->tables.empty());ResetCommand(&source);
 ConstantsBind(&source,shader_stage::graphics,{1},0,255,2,full);assert(!s->valid&&s->constants.empty());ResetCommand(&source);
 TablesBind(&source,shader_stage::graphics,{1},0,1,table,0,nullptr);TablesBind(&source,shader_stage::graphics,{2},0,1,&second,0,nullptr);assert(s->tables.size()==1&&s->tables[0].layout.handle==2);
 DestroyCommand(&source);assert(!source.data);
 std::puts("Pre-UI state fixtures passed: dynamic offsets, overlapping constants, snapshot ownership, internal-event isolation, layout transition, buffer bindings, reset and fail-closed bounds.");
}
