/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandate.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace newHorizonsPassiveDivineMandate
{
void Policy::validate() const
{
	if(rank > 3 || (perks & ~uint16_t{0x3ff}) != 0 || (rank == 0 && perks != 0))
		throw std::runtime_error("Invalid passive Divine Mandate policy snapshot");
}

bool Policy::has(Perk perk) const
{
	return rank != 0 && (perks & static_cast<uint16_t>(perk)) != 0;
}

void Usage::validate() const
{
	if(completedCasts > 5)
		throw std::runtime_error("Invalid passive Divine Mandate combat usage");
}

int orderEfficiencyPercent(const Policy & policy)
{
	policy.validate();
	return 5 * policy.rank;
}

uint8_t activationLimit(const Policy & policy)
{
	policy.validate();
	return policy.rank == 0 ? 0 : policy.rank + (policy.has(Perk::MANDATE_OF_HEAVEN) ? 2 : 0);
}

uint8_t remainingActivations(const Policy & policy, Usage used)
{
	used.validate();
	const auto limit = activationLimit(policy);
	if(used.completedCasts > limit)
		throw std::runtime_error("Passive Divine Mandate usage exceeds captured capacity");
	return limit - used.completedCasts;
}

std::optional<Usage> afterAcceptedCast(const Policy & policy, Usage before)
{
	if(remainingActivations(policy, before) == 0)
		return std::nullopt;
	return Usage{static_cast<uint8_t>(before.completedCasts + 1)};
}

int manaRecovery(const Policy & policy)
{
	policy.validate();
	return policy.has(Perk::CHAPLAINS_RESERVE) ? 3 : 0;
}

int inspiringSpeed(const Policy & policy)
{
	policy.validate();
	return policy.has(Perk::INSPIRING_LEADERSHIP) ? 2 : 0;
}

OrderRiders orderRiders(const Policy & policy)
{
	policy.validate();
	OrderRiders result;
	if(policy.has(Perk::SACRED_COMMAND))
		result.morale = result.luck = 1;
	if(policy.has(Perk::KNIGHTLY_VALOR))
		result.meleeDamagePercent = 10;
	if(policy.has(Perk::SHIELD_OF_FAITH))
		result.enemyAttackReductionPercent = 10;
	if(policy.has(Perk::RIGHTEOUS_DISCIPLINE))
		result.extraRetaliations = 1;
	result.ignoreNegativeMorale = policy.has(Perk::UNWAVERING_STANDARD);
	if(policy.has(Perk::CONSECRATED_ARMS))
		result.meleeDefenseIgnorePercent = 10;
	result.fury = policy.has(Perk::DIVINE_FURY);
	return result;
}

std::vector<SpellChoice> finalizeSpellPool(std::vector<SpellChoice> choices,
	const std::vector<uint32_t> & orderRecipientUnitIds)
{
	std::set<SpellID> eligibleMassFamilies;
	std::set<SpellID> seenSpells;
	for(const auto & choice : choices)
	{
		if(!choice.spell.hasValue() || !choice.family.hasValue()
			|| !seenSpells.insert(choice.spell).second)
			throw std::runtime_error("Invalid or duplicate passive Divine Mandate spell choice");
		if(choice.massive && choice.targets.size() > 1)
			throw std::runtime_error("Passive Divine Mandate mass spell has more than one native aim");
		if(choice.massive && !choice.targets.empty())
			eligibleMassFamilies.insert(choice.family);
	}
	std::erase_if(choices, [&eligibleMassFamilies](const SpellChoice & choice)
	{
		return choice.targets.empty()
			|| (!choice.massive && eligibleMassFamilies.contains(choice.family));
	});
	for(auto & choice : choices)
	{
		if(choice.massive)
			continue;
		std::vector<TargetChoice> preferred;
		for(const auto & target : choice.targets)
			if(std::find(orderRecipientUnitIds.begin(), orderRecipientUnitIds.end(), target.aimUnitId) != orderRecipientUnitIds.end())
				preferred.push_back(target);
		if(!preferred.empty())
			choice.targets = std::move(preferred);
	}
	return choices;
}

SelectedSpell selectSpell(const std::vector<SpellChoice> & pool, std::size_t spellIndex, std::size_t targetIndex)
{
	if(spellIndex >= pool.size() || targetIndex >= pool[spellIndex].targets.size()
		|| (pool[spellIndex].massive && targetIndex != 0))
		throw std::runtime_error("Passive Divine Mandate random selection is outside the eligible pool");
	return {pool[spellIndex].spell, pool[spellIndex].targets[targetIndex]};
}

bool furyEligible(const Policy & policy, bool underOrder, bool ordinaryPrimaryCreatureAttack,
	bool hostileSurvivingTarget, bool legalHolyWrathTarget)
{
	policy.validate();
	return policy.has(Perk::DIVINE_FURY) && underOrder && ordinaryPrimaryCreatureAttack
		&& hostileSurvivingTarget && legalHolyWrathTarget;
}
}
