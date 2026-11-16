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
// Original guest-page presentation only. Full root/content/Ready/start
// agreements remain separate and cannot be constructed from this predicate.
bool GuestPresentationBound();
// Explicit mixed NEO control namespace. Success returns a string; mutable
// stage commands return "1" only after the owned host selection is accepted.
// Fresh lobby selection never grants full root/start or saved-source authority.
bool NeoCommand(const std::string& command,std::string& output);
}
