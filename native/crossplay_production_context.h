#pragma once
#include "crossplay_production_launch.h"
namespace CrossplayProduction {
// Called once from the existing verified suspended-entry initializer, after
// portable-profile preparation. A launch context is immutable and owner-bound.
bool PrepareLaunch(Crossplay::Edition edition,const Crossplay::Digest& verified_executable,
                   const std::wstring& actual_profile);
const Launch* CurrentLaunch();
}
