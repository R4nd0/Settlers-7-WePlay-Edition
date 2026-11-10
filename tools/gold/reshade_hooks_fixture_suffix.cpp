int main(){
 fixture_device._dispatch_table={ForwardViewport,ForwardScissor,ForwardTables,ForwardConstants,Lookup};
 auto command=reinterpret_cast<VkCommandBuffer>(0x12340000);
 VkViewport viewport[2]={{1,2,300,400,.1f,.9f},{5,600,700,-500,0,1}};
 auto viewport_before=std::vector<unsigned char>(reinterpret_cast<unsigned char*>(viewport),reinterpret_cast<unsigned char*>(viewport)+sizeof(viewport));
 vkCmdSetViewportWithCount(command,2,viewport);
 assert(forwards==1&&reshade::events==1&&forwarded_pointer==viewport&&reshade::observed_first==0&&reshade::observed_count==2);
 assert(reshade::observed_viewports[0].y==402&&reshade::observed_viewports[0].height==-400&&reshade::observed_viewports[1].y==100&&reshade::observed_viewports[1].height==500);
 assert(!std::memcmp(viewport,viewport_before.data(),sizeof(viewport)));
 VkRect2D scissors[2]={{{-7,9},{320,200}},{{100,-5},{40,80}}};
 vkCmdSetScissorWithCount(command,2,scissors);
 assert(forwards==2&&reshade::events==2&&forwarded_pointer==scissors&&reshade::observed_first==0&&reshade::observed_count==2);
 assert(reshade::observed_scissors[0].left==-7&&reshade::observed_scissors[0].right==313&&reshade::observed_scissors[1].top==-5&&reshade::observed_scissors[1].bottom==75);
 VkDescriptorSet sets[2]={(VkDescriptorSet)0x1000,(VkDescriptorSet)0x2000};uint32_t offsets[4]={256,512,768,1024};
 VkBindDescriptorSetsInfo bind{};bind.sType=VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO;bind.layout=(VkPipelineLayout)0x3000;bind.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;bind.firstSet=3;bind.descriptorSetCount=2;bind.pDescriptorSets=sets;bind.dynamicOffsetCount=4;bind.pDynamicOffsets=offsets;
 vkCmdBindDescriptorSets2(command,&bind);
 assert(forwards==3&&reshade::events==3&&forwarded_pointer==&bind&&reshade::observed_layout==0x3000&&reshade::observed_first==3&&reshade::observed_count==2&&reshade::observed_values==sets&&reshade::observed_dynamic_count==4&&reshade::observed_dynamic==offsets);
 uint32_t values[2]={0xdecafbad,0x12345678};VkPushConstantsInfo push{};push.sType=VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;push.layout=(VkPipelineLayout)0x3000;push.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;push.offset=12;push.size=8;push.pValues=values;
 vkCmdPushConstants2(command,&push);
 assert(forwards==4&&reshade::events==4&&forwarded_pointer==&push&&reshade::observed_layout==0x3000&&reshade::observed_parameter==3&&reshade::observed_first==3&&reshade::observed_count==2&&reshade::observed_values==values);
 reshade::enabled=false;
 vkCmdSetViewportWithCount(command,2,viewport);vkCmdSetScissorWithCount(command,2,scissors);vkCmdBindDescriptorSets2(command,&bind);vkCmdPushConstants2(command,&push);
 assert(forwards==8&&reshade::events==4&&values[0]==0xdecafbad&&offsets[3]==1024);
 reshade::enabled=true;fixture_device._dispatch_table.CmdBindDescriptorSets2=nullptr;fixture_device._dispatch_table.CmdPushConstants2=nullptr;
 vkCmdBindDescriptorSets2(command,&bind);vkCmdPushConstants2(command,&push);
 assert(forwards==10&&reshade::events==6&&lookups==2&&forwarded_pointer==&push&&reshade::observed_values==values);
 std::puts("Vulkan 1.3/1.4 wrapper fixtures passed: four unchanged forwards, viewport/scissor conversion, dynamic descriptor offsets, push constant range, disabled-event forwards.");
}
