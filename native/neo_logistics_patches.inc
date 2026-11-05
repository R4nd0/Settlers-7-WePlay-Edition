unsigned char NeoMapBytes[3][5]{};
const Patch NeoMapPatches[]={
    {0x782ee0,{0x55,0x8b,0xec,0x6a,0xff},NeoMapBytes[0],false,5},
    {0x78f380,{0xa1,0x20,0x52,0x3a,0x01},NeoMapBytes[1],false,5},
    {0x782280,{0x55,0x8b,0xec,0x6a,0xff},NeoMapBytes[2],false,5},
#ifndef S7_CASTLE_WIRE_PROTOTYPE
    {0x668365,{0xe8,0xc6,0xfa,0xff,0xff},reinterpret_cast<const void*>(&VisitNeoPlayerField)},
    {0x113f5fc,{0x60,0x80,0x66,0},reinterpret_cast<const void*>(&VisitNeoJoin),true},
    {0xed201f,{0xe8,0x9c,0x6a,0x79,0xff},reinterpret_cast<const void*>(&ConstructNeoJoin)}
#endif
};
const Patch NeoMapConsolePatch={0x137e6b0,{0x50,0x15,0x5d,0},reinterpret_cast<const void*>(&NeoLogisticsConsole),true};
const unsigned char NeoMapTaskUbiGuard[]={0x55,0x8b,0xec,0x6a,0xff,0x68,0x70,0x15,0xf9,0};
const unsigned char NeoMapTaskSteamGuard[]={0x55,0x8b,0xec,0x6a,0xff,0x68,0xe0,0x07,0xf9,0};
const unsigned char NeoMapGroupUbiGuard[]={0x55,0x8b,0xec,0x6a,0xff,0x68,0x56,0xe9,0xf7,0};
const unsigned char NeoMapGroupSteamGuard[]={0x55,0x8b,0xec,0x6a,0xff,0x68,0xc6,0xdb,0xf7,0};
const unsigned char NeoMapTailGuard[]={0x56,0x57,0x8b,0xfa,0x8b,0x80,0x4c,0x01,0x00,0x00};
bool InitializeNeoMapPatches() {
    if(memcmp(Native<const void*>(0x782ee0),steam_history?NeoMapTaskSteamGuard:NeoMapTaskUbiGuard,sizeof(NeoMapTaskUbiGuard))
        || memcmp(Native<const void*>(0x782280),steam_history?NeoMapGroupSteamGuard:NeoMapGroupUbiGuard,sizeof(NeoMapGroupUbiGuard))
        || memcmp(reinterpret_cast<const unsigned char*>(NativeVA(0x78f380))+5,NeoMapTailGuard,sizeof(NeoMapTailGuard)))return false;
    lab_priority_task_resume=NativeVA(0x782ee0)+5;
    neo_group_resume=NativeVA(0x782280)+5;
    neo_compare_resume=NativeVA(0x78f380)+5;neo_compare_global=NativeVA(0x13a5220);
    const uintptr_t targets[]={reinterpret_cast<uintptr_t>(&LabPriorityTaskBridge),reinterpret_cast<uintptr_t>(&CompareNeoFifoTransfersBridge),reinterpret_cast<uintptr_t>(&NeoGroupBridge)};
    for(unsigned i=0;i<3;++i) {
        const auto relative=static_cast<uint32_t>(targets[i]-(NativeVA(NeoMapPatches[i].va)+5));
        NeoMapBytes[i][0]=0xe9;memcpy(NeoMapBytes[i]+1,&relative,4);
    }
    return true;
}
