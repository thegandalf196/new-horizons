/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../nativeRules/CapturedRulePool.h"
#include "../callback/IGameInfoCallback.h"

namespace
{
using nativeRules::CapturedRuleKind;
using nativeRules::CapturedRuleSet;
using nativeRules::CapturedRulePool;
}

void CGHeroInstance::admitCapturedRuleSet(CapturedRulePool & pool)
{
	CapturedRuleSet composition;
	const auto admit = [&pool](auto & reference, CapturedRuleKind kind)
	{
		const auto id = reference.owner() ? pool.adopt(reference.owner()) : pool.intern(kind, reference.document());
		reference.bind(pool.document(id, kind));
		return id;
	};
	composition.documents[0] = admit(masteryState.rules, CapturedRuleKind::Mastery);
	const auto perkId = perkState.capturedOwner()
		? pool.adopt(nativeRules::CapturedRuleDocument::wrapPerks(perkState.capturedOwner()))
		: pool.intern(CapturedRuleKind::Perks, perkState.getRules());
	perkState.bindCapturedRules(pool.document(perkId, CapturedRuleKind::Perks)->perks());
	composition.documents[1] = perkId;
	composition.documents[2] = admit(capabilityRules, CapturedRuleKind::ResolvedCapability);
	composition.documents[3] = admit(primaryGrowthRules, CapturedRuleKind::ResolvedDevelopment);
	capturedRuleSetId = pool.intern(composition);
}

nativeRules::CapturedRuleSetId CGHeroInstance::findCapturedRuleSet(const CapturedRulePool & pool) const
{
	CapturedRuleSet composition;
	composition.documents[0] = masteryState.rules.owner()
		? pool.find(*masteryState.rules.owner()) : pool.find(CapturedRuleKind::Mastery, masteryState.rules.document());
	composition.documents[1] = perkState.capturedOwner()
		? pool.find(*nativeRules::CapturedRuleDocument::wrapPerks(perkState.capturedOwner()))
		: pool.find(CapturedRuleKind::Perks, perkState.getRules());
	composition.documents[2] = capabilityRules.owner()
		? pool.find(*capabilityRules.owner()) : pool.find(CapturedRuleKind::ResolvedCapability, capabilityRules.document());
	composition.documents[3] = primaryGrowthRules.owner()
		? pool.find(*primaryGrowthRules.owner()) : pool.find(CapturedRuleKind::ResolvedDevelopment, primaryGrowthRules.document());
	return pool.find(composition);
}

void CGHeroInstance::restoreCapturedRuleSet(const CapturedRulePool & pool, nativeRules::CapturedRuleSetId id)
{
	// Component references are decoded transactionally by their own codecs. The
	// envelope verifies they agree with this composition after the hero payload.
	pool.ruleSet(id);
	capturedRuleSetId = id;
}

void CGHeroInstance::replaceCapturedPrimaryGrowthRules(JsonNode rules)
{
	nativeRules::CapturedRuleRef<CapturedRuleKind::ResolvedDevelopment> candidate;
	candidate.replace(std::move(rules));
	auto prepared = candidate.development()->preparedCreature(getHeroTypeID());
	if(cb)
		if(auto * pool = cb->getCapturedRulePoolForAdmission())
		{
			const auto id = pool->adopt(candidate.owner());
			candidate.bind(pool->document(id, CapturedRuleKind::ResolvedDevelopment));
		}
	primaryGrowthRules = std::move(candidate);
	primaryGrowthCaptured = true;
	preparedCreatureMechanics = std::move(prepared);
	nodeHasChanged();
}

void CGHeroInstance::captureDefaultCreatureLineTarget()
{
	if(const auto target = compiledDevelopment()->defaultCreature(getHeroTypeID()))
	{
		JsonNode replacement = primaryGrowthRules.document();
		replacement["creatureLineSpecialtyTarget"].String() = CreatureID::encode(target->getNum());
		replaceCapturedPrimaryGrowthRules(std::move(replacement));
	}
}
