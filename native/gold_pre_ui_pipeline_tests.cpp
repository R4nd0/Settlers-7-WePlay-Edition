// SPDX-License-Identifier: MIT
// WePlay Community. Mock GPU API, actual pipeline cache implementation.
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <cassert>
#include <cstdio>
namespace reshade::api {struct device{};struct command_list{};enum class pipeline_stage{graphics};struct pipeline{uint64_t handle;};struct pipeline_layout{uint64_t handle;};
enum class pipeline_subobject_type{vertex_shader,pixel_shader,blend_state,libraries};struct shader_desc{const void* code;size_t code_size;};struct blend_desc{bool blend_enable[8]{};unsigned source_color_blend_factor[8]{},dest_color_blend_factor[8]{};};struct pipeline_subobject{pipeline_subobject_type type;uint32_t count;void* data;};}
#define WEPLAY_GOLD_PRE_UI_PIPELINE_FIXTURE
#include "gold_pre_ui_pipeline.inc"

int main(){using namespace reshade::api;using namespace gold_pipeline;command_list commands[129];for(unsigned i=0;i<128;++i)BindPipeline(&commands[i],pipeline_stage::graphics,{i+1});assert(bound.size()==128);
BindPipeline(&commands[0],pipeline_stage::graphics,{900});assert(bound.at(reinterpret_cast<uintptr_t>(&commands[0]))==900);
BindPipeline(&commands[128],pipeline_stage::graphics,{901});assert(bound.size()==128&&!bound.count(reinterpret_cast<uintptr_t>(&commands[128])));
ClearCommand(&commands[0]);BindPipeline(&commands[128],pipeline_stage::graphics,{902});assert(bound.size()==128&&bound.at(reinterpret_cast<uintptr_t>(&commands[128]))==902);
ClearCommand(&commands[128]);assert(!bound.count(reinterpret_cast<uintptr_t>(&commands[128])));
device a,b;pipelines[7].owner=&a;DestroyPipeline(&b,{7});assert(pipelines.count(7));DestroyPipeline(&a,{7});assert(!pipelines.count(7));
unsigned char vertex[]={1,3,5},pixel[]={2,4,6},override_code[]={7,8};shader_desc vs{vertex,sizeof(vertex)},ps{pixel,sizeof(pixel)},replacement{override_code,sizeof(override_code)};blend_desc blend{};blend.blend_enable[0]=true;blend.source_color_blend_factor[0]=6;blend.dest_color_blend_factor[0]=7;
pipeline_subobject vsub{pipeline_subobject_type::vertex_shader,1,&vs};InitPipeline(&a,{0},1,&vsub,{101});
pipeline_subobject psub[]={{pipeline_subobject_type::pixel_shader,1,&ps},{pipeline_subobject_type::blend_state,1,&blend}};InitPipeline(&a,{0},2,psub,{102});
pipeline libraries[]={{101},{102}};pipeline_subobject linked{pipeline_subobject_type::libraries,2,libraries};InitPipeline(&a,{0},1,&linked,{103});
assert(pipelines[103].vs==Hash(vs)&&pipelines[103].ps==Hash(ps)&&pipelines[103].blend==1&&pipelines[103].src==6&&pipelines[103].dst==7);
DestroyPipeline(&a,{101});assert(pipelines[103].vs==Hash(vs));
InitPipeline(&b,{0},1,&linked,{104});assert(!pipelines[104].vs&&!pipelines[104].ps);
pipeline_subobject overridden[]={{pipeline_subobject_type::vertex_shader,1,&replacement},linked};InitPipeline(&a,{0},2,overridden,{105});assert(pipelines[105].vs==Hash(replacement)&&pipelines[105].ps==Hash(ps));
linked.count=65;InitPipeline(&a,{0},1,&linked,{106});assert(!pipelines[106].vs&&!pipelines[106].ps);
std::puts("Pipeline lifetime fixtures passed: update at capacity, reject excess, reset/destroy removal, slot reuse and device owner guard.");}
