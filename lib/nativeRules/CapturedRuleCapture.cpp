/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedRulePool.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/NewHorizonsHeroRules.h"

namespace nativeRules
{
std::shared_ptr<const CapturedRuleDocument> CapturedRulePool::captureCapability(CapturedDocumentId world, HeroClassID heroClass)
{
	const auto key = std::pair(world, heroClass);
	const auto found = classCapabilities.find(key);
	if(found != classCapabilities.end()) return document(found->second, CapturedRuleKind::ResolvedCapability);
	const auto & source = document(world, CapturedRuleKind::WorldCapability)->document();
	const auto resolved = newHorizonsHeroes::resolveCapabilityRules(source, heroClass);
	const auto id = intern(CapturedRuleKind::ResolvedCapability, resolved);
	classCapabilities.emplace(key, id);
	return document(id, CapturedRuleKind::ResolvedCapability);
}

std::shared_ptr<const CapturedRuleDocument> CapturedRulePool::captureDevelopment(CapturedDocumentId world, HeroClassID heroClass)
{
	const auto key = std::pair(world, heroClass);
	const auto found = classDevelopment.find(key);
	if(found != classDevelopment.end()) return document(found->second, CapturedRuleKind::ResolvedDevelopment);
	const auto & source = document(world, CapturedRuleKind::WorldDevelopment)->document();
	const auto resolved = newHorizonsHeroes::resolveHeroRules(source, heroClass);
	const auto id = intern(CapturedRuleKind::ResolvedDevelopment, resolved);
	classDevelopment.emplace(key, id);
	return document(id, CapturedRuleKind::ResolvedDevelopment);
}
}
