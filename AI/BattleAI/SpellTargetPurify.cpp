/*
 * SpellTargetsEvaluator.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhysicalCleanseValue.h"
#include "CreatureActivationValue.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/battle/NewHorizonsConfusionResolution.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "AttackPossibility.h"
#include "PotentialTargets.h"
#include "StackWithBonuses.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/effects/BattleForm.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsFireWall.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../lib/spells/NewHorizonsPandemonium.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CRandomGenerator.h"
#include "SpellTargetsEvaluator.h"
#include "SearchBudget.h"
#include "SummonEvaluation.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/spells/Spell.h>

#include <set>
#include <tuple>
#include "HypotheticBattle.h"

using namespace spells;

namespace
{
float purifyPoisonValue(const battle::Unit * unit, DamageCache & damageCache,
	const std::shared_ptr<CBattleInfoCallback> & battleState, battleSearch::SearchBudget * budget)
{
	float physicalPoisonValue = 0.0f;

			const auto * liveState = dynamic_cast<const battle::CUnitState *>(unit);
			if(liveState)
			{
				auto poisonState = liveState->acquireState();
				int64_t remainingHealth = std::max<int64_t>(0, unit->getAvailableHealth());
				int64_t preventedDamage = 0;
				while(remainingHealth > 0 && poisonState->physicalPoisonActivationsRemaining > 0)
				{
					battleSearch::require(budget, battleSearch::WorkKind::Generation);
					const auto tick = newHorizonsBulwark::physicalPoisonTickDamage(poisonState.get());
					const auto applied = std::min(remainingHealth, tick);
					preventedDamage += applied;
					remainingHealth -= applied;
					newHorizonsBulwark::advancePhysicalPoison(poisonState.get());
				}
				if(preventedDamage > 0)
				{
					physicalPoisonValue = AttackPossibility::calculateDamageReduce(nullptr, unit,
						static_cast<uint64_t>(preventedDamage), damageCache, battleState);
					if(physicalPoisonValue <= 0.0f)
						physicalPoisonValue = static_cast<float>(preventedDamage);
				}
			}

	return physicalPoisonValue;
}
}

bool SpellTargetEvaluator::isCanonicalPurify(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	const auto * callback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = callback ? callback->getBattle() : nullptr;
	return spell && battle
		&& spell->getJsonKey() == newHorizonsPurify::SPELL_ID
		&& newHorizonsPurify::enabled(battle->getMagicRules(), spell->getId());
}

std::vector<spells::Target> SpellTargetEvaluator::purifyTargets(const spells::Mechanics * spellMechanics, battleSearch::SearchBudget * budget)
{
	std::vector<Target> result;
	try
	{

			physicalCleanseAI::MobilityQuery mobility;
			for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
			{
				battleSearch::require(budget, battleSearch::WorkKind::Generation);
				const BattleHex center(index);
				if(!center.isValid())
					continue;

				Target target{Destination(center)};
				detail::ProblemImpl problem;
				if(spellMechanics->canBeCastAt(target, problem)
					&& purifySelection(spellMechanics, target, &mobility, budget).value > 0.0f)
					result.push_back(std::move(target));
			}
			return result;

	}
	catch(const battleSearch::BudgetExhausted &) { return result; }
}

SpellTargetEvaluator::PurifySelection SpellTargetEvaluator::purifySelection(
	const Mechanics * spellMechanics, const Target & target, physicalCleanseAI::MobilityQuery * mobility, battleSearch::SearchBudget * budget)
{
	physicalCleanseAI::MobilityQuery localMobility;
	if(!mobility) mobility = &localMobility;
	PurifySelection selection;
	if(!isCanonicalPurify(spellMechanics) || target.size() != 1
		|| target.front().unitValue != nullptr || !target.front().hexValue.isValid())
		return selection;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return selection;

	const auto * callback = spellMechanics->battle();
	const auto * hero = spellMechanics->getHeroCaster();
	const int32_t spellPower = hero
		? std::max<int32_t>(0, hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER))
		: std::max<int32_t>(0, spellMechanics->getEffectPower());
	const bool purifierPerk = newHorizonsPurify::hasPurifierPerk(hero);
	const auto eligibleStacks = newHorizonsPurify::eligibleStacks(*callback,
		spellMechanics->getCasterSide(), target.front().hexValue, spellPower, purifierPerk);
	auto battleState = std::shared_ptr<CBattleInfoCallback>(
		const_cast<CBattleInfoCallback *>(callback), [](CBattleInfoCallback *) {});
	DamageCache damageCache;

	struct RankedGroup
	{
		SpellID spell = SpellID::NONE;
		float value = 0.0f;
	};

	for(const auto & eligible : eligibleStacks)
	{
		battleSearch::require(budget, battleSearch::WorkKind::Generation);
		const auto * unit = callback->battleGetUnitByID(static_cast<uint32_t>(eligible.unitId));
		// Shared eligibility already resolves effective allegiance (including Hypnotize).
		if(!unit || !unit->alive())
			continue;

		if(eligible.stunAutomaticallyCleared && !newHorizonsFrozen::isFrozen(*unit))
			selection.value += creatureActivationValue::expectedTargetActivationValue(unit, damageCache, battleState, true);
		if(eligible.crippleAutomaticallyCleared)
			selection.value += mobility->crippleValue(*callback, *unit);
		const float physicalPoisonValue = eligible.physicalPoison || eligible.physicalPoisonAutomaticallyCleared
			? purifyPoisonValue(unit, damageCache, battleState, budget) : 0.0f;
		if(eligible.physicalPoisonAutomaticallyCleared && physicalPoisonValue > 0.0f)
		{
			selection.value += physicalPoisonValue;
			selection.physicalPoisonStackIds.push_back(eligible.unitId);
		}

		std::vector<RankedGroup> groups;
		if(eligible.physicalPoison && !eligible.physicalPoisonAutomaticallyCleared
			&& physicalPoisonValue > 0.0f)
			groups.push_back({newHorizonsPurify::physicalPoisonChoiceID(), physicalPoisonValue});
		for(const auto effectSpell : eligible.spellEffectGroups)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			const auto * statusSpell = effectSpell.toSpell();
			if(!statusSpell || !statusSpell->isMagical() || !statusSpell->isNegative())
				continue;

			const auto bonuses = newHorizonsPurify::spellEffectGroupBonuses(unit, effectSpell);
			if(bonuses.empty())
				continue;
			// Cleansing the transfer itself would return this recruited enemy to
			// its original hostile side. Do not score that group as a benefit.
			// Inspect actual bonuses, not a particular spell's authored identity.
			if(unit->unitSide() != spellMechanics->getCasterSide()
				&& std::ranges::any_of(bonuses, [](const Bonus & bonus)
				{
					return bonus.type == BonusType::HYPNOTIZED;
				}))
				continue;

			int remainingRounds = 0;
			bool timed = false;
			for(const auto & bonus : bonuses)
				{
					battleSearch::require(budget, battleSearch::WorkKind::Generation);
					if(Bonus::NTurns(&bonus))
				{
					timed = true;
					remainingRounds = std::max(remainingRounds,
						std::max<int32_t>(0, bonus.turnsRemain));
				}
				}

			// longer-lived groups when a stack has more eligible effects than its cap.
			const float health = static_cast<float>(std::max<int64_t>(1, unit->getAvailableHealth()));
			const float levelFactor = 0.75f + 0.05f
				* static_cast<float>(std::clamp(statusSpell->getLevel(), 1, 5));
			const float durationFactor = timed
				? std::clamp(0.4f + 0.2f * static_cast<float>(remainingRounds), 0.4f, 1.0f)
				: 1.0f;
			groups.push_back({effectSpell, health * 0.9f * levelFactor * durationFactor});
		}
		std::sort(groups.begin(), groups.end(), [](const RankedGroup & lhs, const RankedGroup & rhs)
		{
			if(lhs.value != rhs.value)
				return lhs.value > rhs.value;
			return lhs.spell.getNum() < rhs.spell.getNum();
		});

		const auto choiceCount = std::min<size_t>(
			static_cast<size_t>(std::max(0, eligible.maximumSpellEffectChoices)), groups.size());
		for(size_t index = 0; index < choiceCount; ++index)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			selection.spellEffectGroups.emplace_back(eligible.unitId, groups[index].spell);
			selection.value += groups[index].value;
			if(groups[index].spell == newHorizonsPurify::physicalPoisonChoiceID())
				selection.physicalPoisonStackIds.push_back(eligible.unitId);
		}
	}

	std::sort(selection.spellEffectGroups.begin(), selection.spellEffectGroups.end(),
		[](const auto & lhs, const auto & rhs)
		{
			if(lhs.first != rhs.first)
				return lhs.first < rhs.first;
			return lhs.second.getNum() < rhs.second.getNum();
		});
	std::sort(selection.physicalPoisonStackIds.begin(), selection.physicalPoisonStackIds.end());
	selection.physicalPoisonStackIds.erase(std::unique(selection.physicalPoisonStackIds.begin(),
		selection.physicalPoisonStackIds.end()), selection.physicalPoisonStackIds.end());
	return selection;
}
