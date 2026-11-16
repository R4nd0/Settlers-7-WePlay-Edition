#pragma once
#include "crossplay_production_context.h"
namespace CrossplayProduction {
enum class Phase { Dormant,LobbyCaptured,Connecting,Admitting,RosterBound,Closed,Failed };
// The ordinary entry point retains its mixed-mode gate until world/event/NEO
// integration is complete. This core only owns native lobby admission.
bool PrepareRuntime();
void PumpRuntime();
bool CloseRuntime();
Phase RuntimePhase();
std::string BootstrapMap();
bool BootstrapChoices(uint32_t& flags,std::string& map_template);
// Actual owned History values after exact configuration application. This is
// read-only metadata, not complete setup/root agreement or start permission.
bool AppliedGuestChoices(uint32_t& flags,std::string& map_template);
}
