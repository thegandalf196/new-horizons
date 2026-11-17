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
	if(name == "BattleAIUnlimitedReference") return Profile::LegacyUnlimited;
	if(name == "BattleAIBeamOne") return Profile::BeamOne;
	if(name == "BattleAIQuick") return Profile::BeamOneTwoActors;
	return std::nullopt;
}
Scope::Scope(Profile profile) : previous(current)
{
	if(profile != Profile::Full && profile != Profile::BeamOne && profile != Profile::BeamOneTwoActors && profile != Profile::LegacyUnlimited)
		throw std::invalid_argument("Unsupported BattleAI search profile");
	current = profile;
}
Scope::~Scope() { current = previous; }
bool forecastBeamOne() { return current == Profile::BeamOne || current == Profile::BeamOneTwoActors; }
std::optional<SearchLimits> workLimits()
{
	if(current == Profile::LegacyUnlimited) return std::nullopt;
	return current == Profile::BeamOneTwoActors ? SearchLimits{} : SearchLimits::full();
}
unsigned heroRefinementLimit() { return current == Profile::BeamOneTwoActors ? 3 : 0; }
unsigned forecastFutureActors() { return current == Profile::BeamOneTwoActors ? 2 : 0; }
}
