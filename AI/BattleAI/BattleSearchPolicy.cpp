/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleSearchPolicy.h"
#include <stdexcept>

namespace battleSearch
{
namespace
{
thread_local Profile current = Profile::Full;
}
std::optional<Profile> profileForAIName(std::string_view name)
{
	if(name == "BattleAI") return Profile::Full;
	if(name == "BattleAIBeamOne") return Profile::BeamOne;
	if(name == "BattleAIQuick") return Profile::BeamOneTwoActors;
	return std::nullopt;
}
Scope::Scope(Profile profile) : previous(current)
{
	if(profile != Profile::Full && profile != Profile::BeamOne && profile != Profile::BeamOneTwoActors)
		throw std::invalid_argument("Unsupported BattleAI search profile");
	current = profile;
}
Scope::~Scope() { current = previous; }
bool forecastBeamOne() { return current != Profile::Full; }
unsigned forecastFutureActors() { return current == Profile::BeamOneTwoActors ? 2 : 0; }
}
