/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGameState.h"
#include "../mapping/CMap.h"
#include "../mapObjects/CGHeroInstance.h"

nativeRules::CapturedRulePool * IGameInfoCallback::getCapturedRulePoolForAdmission() const
{
	return nullptr;
}

const nativeRules::CompiledCapabilityRules * IGameInfoCallback::getCompiledHeroCapabilityRules() const
{
	return nullptr;
}

void CGameState::admitWorldRuleDocuments()
{
	const auto admit = [this](auto & reference, nativeRules::CapturedRuleKind kind)
	{
		const auto id = reference.owner() ? capturedRulePool.adopt(reference.owner())
			: capturedRulePool.intern(kind, reference.document());
		reference.bind(capturedRulePool.document(id, kind));
	};
	admit(heroDevelopmentRules, nativeRules::CapturedRuleKind::WorldDevelopment);
	admit(heroCapabilityRules, nativeRules::CapturedRuleKind::WorldCapability);
	admit(heroMasteryRules, nativeRules::CapturedRuleKind::Mastery);
	admit(heroPerkRules, nativeRules::CapturedRuleKind::Perks);
	admit(magicRules, nativeRules::CapturedRuleKind::WorldMagic);
}

void CGameState::prepareCapturedRulePool()
{
	admitWorldRuleDocuments();
	if(!map) return;
	// Includes imprisoned/garrisoned heroes, independently of the visible list.
	for(const auto & object : map->objects)
		if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()))
			hero->admitCapturedRuleSet(capturedRulePool);
	for(const auto identity : map->getHeroesInPool())
		if(auto * hero = map->tryGetFromHeroPool(identity))
			hero->admitCapturedRuleSet(capturedRulePool);
}

void CGameState::replaceCapturedPerkRules(JsonNode rules)
{
	nativeRules::CapturedRuleRef<nativeRules::CapturedRuleKind::Perks> candidate;
	candidate.replace(std::move(rules));
	const auto id = capturedRulePool.adopt(candidate.owner());
	candidate.bind(capturedRulePool.document(id, nativeRules::CapturedRuleKind::Perks));
	heroPerkRules = std::move(candidate);
}
