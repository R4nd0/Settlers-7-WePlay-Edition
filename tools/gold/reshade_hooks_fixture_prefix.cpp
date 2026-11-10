#define WIN32_LEAN_AND_MEAN
#define RESHADE_ADDON 2
#include <Windows.h>
#include <glad/vulkan.h>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
template<class T> struct temp_mem {std::vector<T> data;T* p;temp_mem(size_t n):data(n),p(data.data()){}T& operator[](size_t i){return data[i];}};
namespace reshade {
namespace api {struct viewport{float x,y,width,height,min_depth,max_depth;};struct rect{int32_t left,top,right,bottom;};enum class shader_stage:uint32_t{};struct pipeline_layout{uint64_t handle;};struct descriptor_table{uint64_t handle;};}
enum class addon_event{bind_viewports,bind_scissor_rects,bind_descriptor_tables,push_constants};
bool enabled=true;unsigned events=0;
std::vector<api::viewport> observed_viewports;std::vector<api::rect> observed_scissors;
uint32_t observed_first=0,observed_count=0,observed_dynamic_count=0,observed_parameter=0;uint64_t observed_layout=0;const void* observed_values=nullptr;const void* observed_dynamic=nullptr;
template<addon_event> bool has_addon_event(){return enabled;}
template<addon_event E,class C> void invoke_addon_event(C*,uint32_t first,uint32_t count,const api::viewport* values){static_assert(E==addon_event::bind_viewports);++events;observed_first=first;observed_count=count;observed_viewports.assign(values,values+count);}
template<addon_event E,class C> void invoke_addon_event(C*,uint32_t first,uint32_t count,const api::rect* values){static_assert(E==addon_event::bind_scissor_rects);++events;observed_first=first;observed_count=count;observed_scissors.assign(values,values+count);}
template<addon_event E,class C> void invoke_addon_event(C*,api::shader_stage,api::pipeline_layout layout,uint32_t first,uint32_t count,const api::descriptor_table* values,uint32_t dynamic_count,const uint32_t* offsets){static_assert(E==addon_event::bind_descriptor_tables);++events;observed_layout=layout.handle;observed_first=first;observed_count=count;observed_values=values;observed_dynamic_count=dynamic_count;observed_dynamic=offsets;}
template<addon_event E,class C> void invoke_addon_event(C*,api::shader_stage,api::pipeline_layout layout,uint32_t parameter,uint32_t first,uint32_t count,const uint32_t* values){static_assert(E==addon_event::push_constants);++events;observed_layout=layout.handle;observed_parameter=parameter;observed_first=first;observed_count=count;observed_values=values;}
namespace vulkan {
struct command_list_impl{};struct layout_data{std::vector<uint64_t> set_layouts{1,2,3};};
struct device_impl{struct Dispatch{PFN_vkCmdSetViewportWithCount CmdSetViewportWithCount;PFN_vkCmdSetScissorWithCount CmdSetScissorWithCount;PFN_vkCmdBindDescriptorSets2 CmdBindDescriptorSets2;PFN_vkCmdPushConstants2 CmdPushConstants2;PFN_vkGetDeviceProcAddr GetDeviceProcAddr;} _dispatch_table{};command_list_impl command;layout_data layout;uint64_t get_native(){return 0x12340000;}
template<VkObjectType Type,class T> auto get_private_data_for_object(T){if constexpr(Type==VK_OBJECT_TYPE_COMMAND_BUFFER)return &command;else return &layout;}};
}}
reshade::vulkan::device_impl fixture_device;
struct Devices{reshade::vulkan::device_impl* at(void*){return &fixture_device;}} g_vulkan_devices;
template<class T> void* dispatch_key_from_handle(T){return nullptr;}
#define RESHADE_VULKAN_GET_DEVICE_DISPATCH_PTR(name, device) PFN_vk##name trampoline=(device)->_dispatch_table.name
unsigned forwards=0;const void* forwarded_pointer=nullptr;
void VKAPI_CALL ForwardViewport(VkCommandBuffer,uint32_t,const VkViewport* p){++forwards;forwarded_pointer=p;}
void VKAPI_CALL ForwardScissor(VkCommandBuffer,uint32_t,const VkRect2D* p){++forwards;forwarded_pointer=p;}
void VKAPI_CALL ForwardTables(VkCommandBuffer,const VkBindDescriptorSetsInfo* p){++forwards;forwarded_pointer=p;}
void VKAPI_CALL ForwardConstants(VkCommandBuffer,const VkPushConstantsInfo* p){++forwards;forwarded_pointer=p;}
unsigned lookups=0;
PFN_vkVoidFunction VKAPI_CALL Lookup(VkDevice device,const char* name){assert(reinterpret_cast<uintptr_t>(device)==0x12340000);++lookups;if(!std::strcmp(name,"vkCmdBindDescriptorSets2KHR"))return reinterpret_cast<PFN_vkVoidFunction>(ForwardTables);assert(!std::strcmp(name,"vkCmdPushConstants2KHR"));return reinterpret_cast<PFN_vkVoidFunction>(ForwardConstants);}
