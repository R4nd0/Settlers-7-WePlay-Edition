// Match configuration travels through Storm's existing reliable lobby message.
// A join capability field rejects peers without this protocol before play.
// No simulation state is touched on a transport thread.
std::atomic<uint32_t> neo_wire_local{0},neo_wire_received{2};
std::atomic<bool> neo_wire_confirmed{false};
std::atomic<uint32_t> neo_wire_start_choice{2};
constexpr uint32_t NeoWireVersion=0x324f454e; // NEO2: lock the choice on the real native start event
struct NeoWireResult { int32_t error;uint32_t detail; };
enum class NeoWireKind:uint32_t {Join,Lobby};
struct NeoWireField { const uintptr_t* type;uint32_t touched;bool* global_touched;NeoWireKind kind; };
void ReportNeoWireOnce(uint32_t bit,const char* label) {
    static std::atomic<uint32_t> reported{0};if(!(reported.fetch_or(bit)&bit))ReportLocalStatus(label);
}
NeoWireResult* __fastcall WriteNeoWire(NeoWireField* field,void*,NeoWireResult* result,void* stream) {
    using Write=bool (__thiscall*)(void*,uint32_t);
    bool okay=Native<Write>(0x6679e0)(stream,NeoWireVersion);
    if(okay && field->kind==NeoWireKind::Lobby)okay=Native<Write>(0x6679e0)(stream,neo_wire_local.load());
    *result={okay?1:static_cast<int32_t>(0xfff0bdc0),0};return result;
}
NeoWireResult* __fastcall ReadNeoWire(NeoWireField* field,void*,NeoWireResult* result,void* stream) {
    using Read=bool (__thiscall*)(void*,uint32_t*);
    uint32_t version=0,choice=2;
    bool okay=Native<Read>(0x667a10)(stream,&version) && version==NeoWireVersion;
    if(okay && field->kind==NeoWireKind::Lobby)okay=Native<Read>(0x667a10)(stream,&choice) && choice<=1;
    if(field->kind==NeoWireKind::Lobby) {
        if(neo_wire_confirmed.load() && choice!=neo_wire_received.load())okay=false;
        neo_wire_received.store(okay?choice:2);
        if(!okay) {neo_wire_confirmed=false;neo_wire_start_choice=2;}
    }
    const auto bit=1u<<(static_cast<uint32_t>(field->kind)+(okay?0:3));
    ReportNeoWireOnce(bit,okay?(field->kind==NeoWireKind::Join?"neo-logistics-peer-compatible":
        "neo-logistics-lobby-received"):"neo-logistics-wire-rejected");
    *result={okay?1:static_cast<int32_t>(0xfff0bdc0),0};return result;
}
void VisitNeoWireKind(void* visitor,NeoWireKind kind) {
    uintptr_t table[7];memcpy(table,Native<const void*>(0x1086d9c),sizeof(table));
    table[4]=reinterpret_cast<uintptr_t>(&WriteNeoWire);table[5]=reinterpret_cast<uintptr_t>(&ReadNeoWire);
    bool touched=true;NeoWireField field{table,1,&touched,kind};
    Native<void (__thiscall*)(void*,void*)>(0xcb16d0)(&field,visitor);
}
void VisitNeoWire(void* visitor,bool setup) {VisitNeoWireKind(visitor,setup?NeoWireKind::Lobby:NeoWireKind::Join);}
void __fastcall VisitNeoJoin(void* self,void*,void* visitor) {
    Native<void (__thiscall*)(void*,void*)>(0x668060)(self,visitor);
    const auto error=GetLastError();
    if(*static_cast<uintptr_t*>(self)==NativeVA(0x113f5f4))VisitNeoWire(visitor,false);
    SetLastError(error);
}
void* __fastcall ConstructNeoJoin(void* self,void*) {
    neo_wire_received=2;neo_wire_confirmed=false;neo_wire_start_choice=2;
    return Native<void* (__thiscall*)(void*)>(0x668ac0)(self);
}
bool NeoNetworkChoice(uint32_t& choice,bool starting=false) {
    auto* session=Native<void* (__cdecl*)()>(0xed2b30)();
    if(!session)return false;
    const bool host=Native<bool (__thiscall*)(void*)>(0xed2af0)(session);
    choice=host?neo_wire_local.load():neo_wire_received.load();
    return choice<=1 && (!starting || (neo_wire_confirmed.load() && neo_wire_start_choice.load()==choice));
}
bool AcceptNeoNetworkStart() {
    // The original receivedStartGameMessage branch calls this on the owner
    // thread, after the engine delivers its real start event. Only our own
    // configuration is locked; native Ready and acknowledgements stay native.
    uint32_t choice=2;if(!NeoNetworkChoice(choice))return false;
    neo_wire_start_choice=choice;neo_wire_confirmed=true;
    ReportLocalStatus("neo-logistics-native-start-accepted");return true;
}
void __fastcall VisitNeoPlayerField(void* self,void*,void* visitor) {
    Native<void (__thiscall*)(void*,void*)>(0x667e30)(self,visitor);
    const auto error=GetLastError();auto* message=static_cast<unsigned char*>(self)-0xd8;
    if(*reinterpret_cast<uintptr_t*>(message)==NativeVA(0x113f5dc))VisitNeoWire(visitor,true);
    SetLastError(error);
}
