// A state-4 production task can retain an unfinished input request. Native
// production admission (79DD10) rejects it, but the pickup selector (7E3FA8)
// treats its mere existence as a reason not to fetch inputs. Keep the native
// production preference only when those same input-completion gates pass.
bool NeoWorkshopPickupBlocked(uintptr_t priority,uint32_t id,uint32_t workshop) {
    const auto task=LabPriorityResolve(priority,id);
    if(!task || LabPriorityWord(task)!=NativeVA(0x1148b6c)
        || LabPriorityWord(task+8)!=id || LabPriorityWord(task+4)!=4
        || LabPriorityWord(task+0x48)!=13 || LabPriorityWord(task+0x4c)!=workshop
        || *reinterpret_cast<const unsigned char*>(task+0x54))return true;
    const auto b=LabPriorityWord(task+0x3c),e=LabPriorityWord(task+0x40);
    if(b==e)return true;
    if(b<0x10000 || e<b || (e-b)%4 || (e-b)/4>256)return true;
    bool unfinished=false;
    for(auto p=b;p<e;p+=4) {
        const auto child=LabPriorityWord(p);
        if(child<0x10000 || LabPriorityWord(child)!=NativeVA(0x1148a68)
            || LabPriorityResolve(priority,LabPriorityWord(child+8))!=child
            || LabPriorityWord(child+0x38)!=task)return true;
        if(!*reinterpret_cast<const unsigned char*>(child+0x85))unfinished=true;
    }
    return !unfinished;
}
uint32_t* __fastcall SelectNeoWorkshopProduction(void* self,void*,uint32_t* out,uint32_t workshop) {
    const auto result=Native<uint32_t* (__thiscall*)(void*,uint32_t*,uint32_t)>(0x79d6e0)(self,out,workshop);
    const auto error=GetLastError();
    __try {
        uint32_t turn=0;const auto priority=NeoFifoOwner()?NeoFifoWorld(turn):0;
        if(priority) {
            NeoFifoEpoch(priority,turn);
            if(neo_fifo_enabled && neo_fifo_fresh && result==out && out && *out
                && !NeoWorkshopPickupBlocked(priority,*out,workshop))*out=0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Retain the native result on a failed object guard.
    }
    SetLastError(error);return result;
}
