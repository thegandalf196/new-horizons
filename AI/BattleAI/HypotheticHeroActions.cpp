/*
 * HypotheticHeroActions.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/OrderActionTransition.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/TimeStopState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
bool isDivineMandateLightSpell(const CBattleInfoCallback & battle, SpellID spell)
{
	const auto schools = battle.battleGetSpellSchools(spell);
	return std::any_of(schools.begin(), schools.end(), [](const SpellSchool & school)
	{
		return school.serializationKey() == "new-horizons:light";
	});
}

ui8 timeStopSideMask(BattleSide side)
{
	if(side == BattleSide::ATTACKER)
		return 1u;
	if(side == BattleSide::DEFENDER)
		return 2u;
	return 0u;
}

}

std::optional<HypotheticBattle::ProjectedSpellAllowance> HypotheticBattle::prepareHeroSpellAllowance(
	BattleSide side, SpellID spell, bool metamagicFollowup, bool grand) const
{
	if(crisisCommand.choice()) return {};
	const auto & ledger = heroActionAllowances.at(side);
	if(ledger.currentRound < 0)
	{
		// Legacy battles have no typed ledger. Preserve their historical distinction:
		// only an ordinary cast spends the flexible Hero Action.
		ProjectedActionReceipt action;
		action.side = side;
		action.typedLedger = false;
		action.epoch = projectedActionEpochs.at(side);
		action.receipt = {0, HeroActionAllowanceState::ActionKind::SPELL,
			metamagicFollowup ? HeroActionAllowanceState::AllowanceKind::SPELL
				: HeroActionAllowanceState::AllowanceKind::HERO,
			metamagicFollowup ? HeroActionAllowanceState::GrantSource::METAMAGIC
				: HeroActionAllowanceState::GrantSource::OTHER,
			projectedRound};
		ProjectedSpellAllowance prepared;
		prepared.action = action;
		prepared.allowancesBefore = ledger;
		prepared.allowancesAfter = ledger;
		prepared.metamagicBefore = metamagicStates.at(side);
		prepared.spell = spell;
		prepared.metamagicFollowup = metamagicFollowup;
		prepared.grand = grand;
		prepared.usesAfter = metamagicStates.at(side).uses;
		prepared.pendingAfter = metamagicStates.at(side).pending;
		prepared.grandUsedAfter = metamagicStates.at(side).grandUsed;
		return prepared;
	}

	try
	{
		auto nextLedger = ledger;
		auto meta = metamagicStates.at(side);
		if(nextLedger.currentRound != projectedRound)
			return {};
		const auto candidateSelection = battleGetSpellActionAllowance(side, spell);
		if(!candidateSelection)
			return {};
		const auto selectedGrantId = candidateSelection->grantId;
		const auto grantFilter = [selectedGrantId](const HeroActionAllowanceState::Grant & grant)
		{
			return grant.id == selectedGrantId;
		};
		const auto selection = nextLedger.eligibleAllowance(
			HeroActionAllowanceState::ActionKind::SPELL, projectedRound, grantFilter);
		if(!selection || selection->grantId != candidateSelection->grantId)
			return {};
		const auto * hero = getSideHero(side);
		const auto transition = HeroSpellAllowanceTransition::commitAcceptedCast(nextLedger, selection->grantId,
			projectedRound, metamagicFollowup, grand,
			hero ? newHorizonsMagic::metamagicRank(hero) : 0,
			hero && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
			meta.uses, meta.pending, meta.grandUsed, meta.sequence.size(), grantFilter,
			newHorizonsMagic::metamagicCapacity(hero));
		if(!transition)
			return {};

		ProjectedSpellAllowance prepared;
		prepared.action = {side, transition->receipt, true, projectedActionEpochs.at(side)};
		prepared.allowancesBefore = ledger;
		prepared.allowancesAfter = std::move(nextLedger);
		prepared.metamagicBefore = metamagicStates.at(side);
		prepared.spell = spell;
		prepared.metamagicFollowup = metamagicFollowup;
		prepared.grand = grand;
		prepared.usesAfter = meta.uses;
		prepared.pendingAfter = meta.pending;
		prepared.grandUsedAfter = meta.grandUsed;
		return prepared;
	}
	catch(const std::exception &)
	{
		// Malformed or stale candidate state is rejected before any projected effect
		// (including action-boundary cleanup) can touch this clone.
		return {};
	}
}

std::optional<HypotheticBattle::ProjectedOrderAllowance> HypotheticBattle::prepareHeroOrderAllowance(
	BattleSide side) const
{
	const auto & ledger = heroActionAllowances.at(side);
	if(ledger.currentRound < 0)
	{
		ProjectedActionReceipt action;
		action.side = side;
		action.typedLedger = false;
		action.epoch = projectedActionEpochs.at(side);
		action.receipt = {0, HeroActionAllowanceState::ActionKind::ORDER,
			HeroActionAllowanceState::AllowanceKind::HERO,
			HeroActionAllowanceState::GrantSource::OTHER, projectedRound};
		return ProjectedOrderAllowance{action, ledger, ledger, metamagicStates.at(side)};
	}

	try
	{
		auto nextLedger = ledger;
		if(nextLedger.currentRound != projectedRound)
			return {};
		const auto crisisFilter = [this, side](const HeroActionAllowanceState::Grant & grant)
			{ return crisisCommand.allowsGrant(side, grant); };
		const auto selection = orderActionTransition::select(nextLedger, projectedRound, crisisFilter);
		if(!selection)
			return {};
		if(selection->allowance == HeroActionAllowanceState::AllowanceKind::HERO
			&& nextLedger.nextGrantId == std::numeric_limits<uint32_t>::max()
			&& !doubleCommandStates.at(side).used
			&& heroCommands::hasDoubleCommand(battleGetFightingHero(side)))
			return {};
		const auto receipt = orderActionTransition::consume(nextLedger, selection->grantId, projectedRound, crisisFilter);
		if(!receipt)
			return {};
		return ProjectedOrderAllowance{{side, *receipt, true, projectedActionEpochs.at(side)},
			ledger, std::move(nextLedger), metamagicStates.at(side)};
	}
	catch(const std::exception &)
	{
		return {};
	}
}

bool HypotheticBattle::isCurrentPreparedSpellAction(BattleSide side,
	const ProjectedSpellAllowance & prepared, bool requireBegun) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| prepared.action.side != side
		|| prepared.action.epoch != projectedActionEpochs.at(side)
		|| prepared.action.receipt.round != projectedRound
		|| prepared.metamagicBefore != metamagicStates.at(side))
		return false;
	const auto & begunSpell = begunProjectedSpellAllowances.at(side);
	const auto & begunOrder = begunProjectedOrderAllowances.at(side);
	if((requireBegun && (!begunSpell || *begunSpell != prepared || begunOrder.has_value()))
		|| (!requireBegun && (begunSpell.has_value() || begunOrder.has_value())))
		return false;
	const auto current = prepareHeroSpellAllowance(side, prepared.spell,
		prepared.metamagicFollowup, prepared.grand);
	return current && *current == prepared;
}

bool HypotheticBattle::isCurrentPreparedOrderAction(BattleSide side,
	const ProjectedOrderAllowance & prepared, bool requireBegun) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| prepared.action.side != side
		|| prepared.action.epoch != projectedActionEpochs.at(side)
		|| prepared.action.receipt.round != projectedRound
		|| prepared.metamagicBefore != metamagicStates.at(side))
		return false;
	const auto & begunSpell = begunProjectedSpellAllowances.at(side);
	const auto & begunOrder = begunProjectedOrderAllowances.at(side);
	if((requireBegun && (!begunOrder || *begunOrder != prepared || begunSpell.has_value()))
		|| (!requireBegun && (begunSpell.has_value() || begunOrder.has_value())))
		return false;
	const auto current = prepareHeroOrderAllowance(side);
	return current && *current == prepared;
}

bool HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared)
{
	if(!isCurrentPreparedSpellAction(side, prepared, false))
		return false;
	begunProjectedSpellAllowances.at(side) = prepared;
	beginProjectedHeroAction(side, prepared.action);
	return true;
}

bool HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	if(!isCurrentPreparedOrderAction(side, prepared, false))
		return false;
	begunProjectedOrderAllowances.at(side) = prepared;
	beginProjectedHeroAction(side, prepared.action);
	return true;
}

void HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedActionReceipt & action)
{
	if(action.side != side || action.epoch != projectedActionEpochs.at(side))
		return;
	// The complete typed plan was revalidated and recorded before this cleanup
	// boundary; commit must present that same plan, not another same-epoch action.
	if(!action.isHeroAction())
		return;

	expireProjectedTimeStops(side);
	counterspellArmedStates.at(side) = false;
	countersequenceArmedStates.at(side) = false;
}

void HypotheticBattle::finishProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared)
{
	if(begunProjectedSpellAllowances.at(side)
		&& *begunProjectedSpellAllowances.at(side) == prepared)
	{
		begunProjectedSpellAllowances.at(side).reset();
		++projectedActionEpochs.at(side);
	}
}

void HypotheticBattle::finishProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	if(begunProjectedOrderAllowances.at(side)
		&& *begunProjectedOrderAllowances.at(side) == prepared)
	{
		begunProjectedOrderAllowances.at(side).reset();
		begunProjectedOrderEffects.at(side).reset();
		begunProjectedLegacyOrders.at(side).reset();
		++projectedActionEpochs.at(side);
	}
}

void HypotheticBattle::expireProjectedTimeStops(BattleSide casterSide)
{
	const auto sideMask = timeStopSideMask(casterSide);
	if(!sideMask)
		return;

	for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
	{
		auto projected = getForUpdate(unit->unitId());
		projected->removeUnitBonus(CSelector([casterSide](const Bonus * bonus)
		{
			return bonus && timeStopState::isStateBonus(*bonus)
				&& timeStopState::belongsToSide(*bonus, casterSide);
		}));
	}
	pendingTimeStopHeroActionSides &= static_cast<ui8>(~sideMask);
	++bonusTreeVersion;
}

bool HypotheticBattle::projectAcceptedHeroSpell(BattleSide side, SpellID spell, uint32_t target,
	bool metamagicFollowup, bool grand, bool counterspellWardActive, bool counterspellNegated,
	const ProjectedSpellAllowance & prepared, const std::vector<uint32_t> & affectedRecipients)
{
	const auto & action = prepared.action;
	if(action.receipt.action != HeroActionAllowanceState::ActionKind::SPELL
		|| spell != prepared.spell
		|| metamagicFollowup != prepared.metamagicFollowup || grand != prepared.grand
		|| !isCurrentPreparedSpellAction(side, prepared, true))
		return false;
	const bool lightSpell = isDivineMandateLightSpell(*this, spell);
	const auto mandateStatus = battleGetDivineMandateStatus(side);

	auto & meta = metamagicStates.at(side);
	if(action.typedLedger)
	{
		auto nextLedger = prepared.allowancesAfter;
		const auto recipients = lightSpell && !counterspellNegated
			&& newHorizonsDivineMandate::needsRecipientCapture(getSideHero(side))
			? newHorizonsDivineMandate::sharedPurposeFriendlyRecipients(*this, side, affectedRecipients)
			: std::vector<uint32_t>{};
		const auto overlap = DivineMandateTransition::applyAcceptedAction(nextLedger, action.receipt,
			projectedRound, lightSpell, mandateStatus.maximumPairs, recipients);
		heroActionAllowances.at(side) = std::move(nextLedger);
		newHorizonsDivineMandate::applySharedPurpose(*this, *this, side, overlap);
		meta.uses = prepared.usesAfter;
		meta.pending = prepared.pendingAfter;
		meta.grandUsed = prepared.grandUsedAfter;

		if(action.receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO)
		{
			if(meta.pending != 0)
			{
				meta.firstSpell = spell;
				meta.firstTarget = target;
				meta.firstCountered = counterspellNegated;
				meta.sequence = {spell};
			}
			else
			{
				meta.firstSpell = SpellID();
				meta.firstTarget = std::numeric_limits<uint32_t>::max();
				meta.firstCountered = false;
				meta.sequence.clear();
			}
		}
		else if(action.receipt.source == HeroActionAllowanceState::GrantSource::METAMAGIC
			|| action.receipt.source == HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND)
		{
			meta.sequence.push_back(spell);
			if(meta.pending == 0)
			{
				meta.firstSpell = SpellID();
				meta.firstTarget = std::numeric_limits<uint32_t>::max();
				meta.firstCountered = false;
				meta.sequence.clear();
			}
		}
	}

	const auto * hero = getSideHero(side);
	if(action.isHeroAction() && newHorizonsWarcasting::enabled(getMagicRules()))
		warcastingStates.at(side).recordAcceptedAction(AlternatingHeroActionState::Action::SPELL,
			projectedRound, newHorizonsWarcasting::empowerment(hero, AlternatingHeroActionState::Action::SPELL),
			newHorizonsWarcasting::readinessLifetimeRounds(hero));

	const auto enemySide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	if(counterspellWardActive)
	{
		counterspellArmedStates.at(enemySide) = false;
		countersequenceArmedStates.at(enemySide) = false;
	}
	if(!counterspellNegated && newHorizonsMagic::isCounterspell(spell.toSpell()))
	{
		counterspellArmedStates.at(side) = true;
		countersequenceArmedStates.at(side) = metamagicFollowup && newHorizonsMagic::hasMetamagicPerk(
			hero, newHorizonsMagic::METAMAGIC_COUNTERSEQUENCE);
	}
	finishProjectedHeroAction(side, prepared);
	return true;
}


HypotheticBattle::ProjectedCounterspellOutcome HypotheticBattle::resolveProjectedCounterspell(
	BattleSide casterSide, const CSpell * spell) const
{
	ProjectedCounterspellOutcome result;
	if(!spell || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return result;

	const auto wardSide = casterSide == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	result.resolutionKnown = true;
	result.negated = false;
	if(!counterspellArmedStates.at(wardSide))
		return result;

	// The ward flag is public battle state, but an ordinary player callback may
	// deliberately hide the opposing hero's mana and Countermage selection.
	// Preserve the known ward while leaving its cost/negation unresolved.
	result.wardSide = wardSide;
	result.wardActive = true;
	result.resolutionKnown = false;
	result.negated.reset();
	const auto * caster = getSideHero(casterSide);
	const auto * wardingHero = getSideHero(wardSide);
	if(!caster || !wardingHero)
		return result;

	const int listedCost = caster->getListedSpellCost(spell);
	const int manaCost = newHorizonsMagic::counterspellCost(listedCost,
		wardingHero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage"),
		countersequenceArmedStates.at(wardSide));
	result.manaCost = manaCost;
	const auto & pools = projectedMana.at(wardSide);
	result.negated = (pools ? pools->pools.getTotal() : wardingHero->getManaAvailable()) >= manaCost;
	result.resolutionKnown = true;
	return result;
}

bool HypotheticBattle::projectHeroSpellAllowance(BattleSide side, SpellID spell, uint32_t target,
	bool metamagicFollowup, bool grand)
{
	const auto prepared = prepareHeroSpellAllowance(side, spell, metamagicFollowup, grand);
	if(!prepared)
		return false;
	const auto counterspell = resolveProjectedCounterspell(side, spell.toSpell());
	if(!counterspell.resolutionKnown || !counterspell.negated.has_value())
		return false;
	if(!beginProjectedHeroAction(side, *prepared))
		return false;
	return projectAcceptedHeroSpell(side, spell, target, metamagicFollowup, grand,
		counterspell.wardActive, *counterspell.negated, *prepared);
}

bool HypotheticBattle::projectHeroOrderAllowance(BattleSide side)
{
	const auto prepared = prepareHeroOrderAllowance(side);
	if(!prepared || !beginProjectedHeroAction(side, *prepared))
		return false;
	return projectAcceptedHeroOrder(side, *prepared);
}
