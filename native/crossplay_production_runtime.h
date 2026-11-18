#pragma once
#include "crossplay_production_context.h"
namespace CrossplayProduction {
enum class Phase { Dormant,LobbyCaptured,Connecting,Admitting,RosterBound,Closed,Failed };
// The ordinary entry point retains its mixed-mode gate until world/event/NEO
// integration and final package qualification are complete.
bool PrepareRuntime();
void PumpRuntime();
bool CloseRuntime();
Phase RuntimePhase();
bool RoomConfigurationApplied();
bool RoomGuestReadyActionAvailable();
bool RoomAllHumansReadyObserved();
std::string BootstrapMap();
bool BootstrapChoices(uint32_t& flags,std::string& map_template);
// Actual owned History values after exact configuration application. This is
// read-only metadata, not complete setup/root agreement or start permission.
bool AppliedGuestChoices(uint32_t& flags,std::string& map_template);
// Original guest-page presentation only. Full root/content/Ready/start
// agreements remain separate and cannot be constructed from this predicate.
bool GuestPresentationBound();
// Opt-in automation only: survives Lua page recreation. It does not activate
// a widget, enqueue a command or substitute an original readiness result.
bool GuestReadyActionAvailable();
// Read-only routing for the original SessionStarted UI callback. Requires an
// actual queued original start and its owned started session; never starts it.
bool StartUsesSavedWorld(bool& saved);
// Ownership of an actually started original Gold session on the admitted
// mixed channel. Read-only ownership may survive channel failure while that
// exact original world tears down; event and NEO authority do not. This does
// not compute or acknowledge native Ready.
bool GoldWorldOwnsStartedSession(uintptr_t session);
// Exact AI rows admitted by the original chooser and common complete root.
// This predicate does not replace the original Ready result.
bool GoldWorldAIPlayer(uintptr_t session,uintptr_t rows,unsigned row);
unsigned GoldWorldExpectedAIPlayers(uintptr_t session);
// Explicit mixed NEO control namespace. Success returns a string; mutable
// stage commands return "1" only after the owned host selection is accepted.
// Fresh lobby selection never grants full root/start or saved-source authority.
bool NeoCommand(const std::string& command,std::string& output);
}
