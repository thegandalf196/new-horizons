/*
 * SideInBattle.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "SideInBattle.h"

#include "../callback/IGameInfoCallback.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../entities/hero/NewHorizonsLeadership.h"

void SideInBattle::init(const CGHeroInstance * Hero, const CArmedInstance * Army, const CGTownInstance * town)
{
	leadershipComposition = Hero ? newHorizonsHeroes::captureLeadershipComposition(*Hero, Army)
		: newHorizonsHeroes::LeadershipComposition{};
	commandResources = newHorizonsCommand::initialState(Hero);
	elementalAttunementMask = 0;
	elementalAttunementProfile = newHorizonsElementalAttunement::captureProfile(Hero);
	passiveDivineMandateUses = 0;
	manaFurnaceRecoveredNormal = 0;
	manaLeechesRecoveredNormal = 0;
	initialAcceptedCombatDay = -1;
	deepTranceConsumed = false;
	deepTranceRecoveredNormal = 0;
	armyObjectID = Army->id;
	if (Hero)
	{
		heroID = Hero->id;
		initialMana = Hero->getNormalSpellPoints();
		initialNormalSpellPoints = Hero->getNormalSpellPoints();
		initialBufferSpellPoints = Hero->getBufferSpellPoints();
		// NOTE: hero is not attached to town directly at this point, only indirectly via townAndVis
		int64_t additionalManaTotal = Hero->valOfBonuses(BonusType::COMBAT_MANA_BONUS);
		additionalManaTotal += Hero->getArcanePreparationBufferBonus();
		demonicReserve = Hero->getDemonicReserve();
		if (town)
			additionalManaTotal += town->valOfBonuses(BonusType::COMBAT_MANA_BONUS);
		additionalMana = static_cast<int32_t>(std::clamp<int64_t>(additionalManaTotal,
			std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
		if(newHorizonsMagic::spellPointRulesActive(Hero->getMagicRules()))
			temporaryBufferRemaining = std::min<int64_t>(std::max<int64_t>(0, additionalManaTotal),
				static_cast<int64_t>(std::numeric_limits<int32_t>::max()) - initialBufferSpellPoints);
	}

	switch(Army->ID.toEnum())
	{
		case Obj::CREATURE_GENERATOR1:
		case Obj::CREATURE_GENERATOR2:
		case Obj::CREATURE_GENERATOR3:
		case Obj::CREATURE_GENERATOR4:
			color = PlayerColor::NEUTRAL;
			break;
		default:
			color = Army->getOwner();
	}

	if(color == PlayerColor::UNFLAGGABLE)
		color = PlayerColor::NEUTRAL;
}

const CArmedInstance * SideInBattle::getArmy() const
{
	if (armyObjectID.hasValue())
		return dynamic_cast<const CArmedInstance*>(cb->getObjInstance(armyObjectID));
	return nullptr;
}

const CGHeroInstance * SideInBattle::getHero() const
{
	if (heroID.hasValue())
		return cb->getHero(heroID);
	return nullptr;
}

void SideInBattle::validateManaFurnaceSerialization(bool supported) const
{
	if(manaFurnaceRecoveredNormal < 0 || manaFurnaceRecoveredNormal > 1000000)
		throw std::runtime_error("Invalid Mana Furnace entry recovery");
	if(!supported && manaFurnaceRecoveredNormal != 0)
		throw std::runtime_error("Cannot discard Mana Furnace entry recovery");
}

void SideInBattle::validateRecoverySpecialtySerialization(bool supported) const
{
	if(initialAcceptedCombatDay < -1 || initialAcceptedCombatDay > std::numeric_limits<uint32_t>::max()
		|| deepTranceRecoveredNormal < 0
		|| (!deepTranceConsumed && (initialAcceptedCombatDay != -1 || deepTranceRecoveredNormal != 0)))
		throw std::runtime_error("Invalid Deep Trance entry provenance");
	if(!supported && (deepTranceConsumed || initialAcceptedCombatDay != -1 || deepTranceRecoveredNormal != 0))
		throw std::runtime_error("Cannot discard Deep Trance entry provenance");
}

void SideInBattle::validateManaLeechesSerialization(bool supported) const
{
	if(manaLeechesRecoveredNormal < 0 || (!supported && manaLeechesRecoveredNormal != 0))
		throw std::runtime_error("Invalid or unrepresentable Mana Leeches recovery");
}

int32_t SideInBattle::legacySpellPointCompletionCeiling() const
{
	validateManaFurnaceSerialization(true);
	validateRecoverySpecialtySerialization(true);
	validateManaLeechesSerialization(true);
	return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(initialMana) + manaFurnaceRecoveredNormal + deepTranceRecoveredNormal + manaLeechesRecoveredNormal,
		0, std::numeric_limits<int32_t>::max()));
}

void SideInBattle::restoreHeroActionAllowancesAfterLoad(int32_t round, bool usesSharedActionBudget,
	bool typedAllowancesSupported)
{
	auto & allowances = heroActionAllowances;
	if(!usesSharedActionBudget)
	{
		if(allowances != HeroActionAllowanceState{})
			throw std::runtime_error("Saved Hero Action allowance state requires shared action rules");
		return;
	}
	const bool noPreRoundActionHistory = !heroCommandUsed && castSpellsCount == 0
		&& metamagicPendingCount == 0 && metamagicUsesConsumed == 0
		&& activeOrder == HeroCommand::NONE && orderStates.empty() && !focusFire
		&& metamagicSequenceSpells.empty();
	const bool cleanUninitializedLedger = allowances == HeroActionAllowanceState{};

	if(typedAllowancesSupported)
	{
		if(round <= 0)
		{
			if(!noPreRoundActionHistory)
				throw std::runtime_error("Action history exists before the first playable battle round");
			if(!cleanUninitializedLedger)
				throw std::runtime_error("Hero Action allowances exist before the first playable battle round");
			return;
		}
		if(allowances.currentRound != round)
			throw std::runtime_error("Hero Action allowance round does not match battle round");
		allowances.validateShape();

	}
	else if(round <= 0)
	{
		if(!noPreRoundActionHistory)
			throw std::runtime_error("Action history exists before the first playable battle round");
		if(!cleanUninitializedLedger)
			throw std::runtime_error("Hero Action allowances exist before the first playable battle round");
	}
	else
	{
		// Old saves represented both a consumed Hero Action and an available
		// Metamagic continuation with independent counters. Reconstruct the
		// typed grants once from that authoritative legacy snapshot.
		allowances.resetForRound(round);
		if(heroCommandUsed || castSpellsCount > 0)
		{
			const auto baseAction = allowances.eligibleAllowance(
				HeroActionAllowanceState::ActionKind::SPELL, round);
			if(!baseAction || baseAction->allowance != HeroActionAllowanceState::AllowanceKind::HERO
				|| !allowances.consumeAllowance(baseAction->grantId,
					HeroActionAllowanceState::ActionKind::SPELL, round))
				throw std::runtime_error("Could not migrate spent legacy Hero Action");
		}
		if(metamagicPendingCount > 1)
			throw std::runtime_error("Unsupported legacy pending Metamagic allowance count");
		if(metamagicPendingCount == 1)
		{
			if(castSpellsCount == 0 && !heroCommandUsed)
				throw std::runtime_error("Legacy Metamagic grant has no consumed Hero Action");
			const bool grandContinuation = metamagicGrandUsed && metamagicSequenceSpells.size() == 2;
			allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
				grandContinuation ? HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND
					: HeroActionAllowanceState::GrantSource::METAMAGIC,
				round);
		}
	}

	uint32_t metamagicGrantCount = 0;
	uint32_t grandGrantCount = 0;
	for(const auto & grant : allowances.grants)
	{
		if(grant.source == HeroActionAllowanceState::GrantSource::METAMAGIC)
			++metamagicGrantCount;
		else if(grant.source == HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND)
			++grandGrantCount;
	}
	if(metamagicGrantCount + grandGrantCount != metamagicPendingCount
		|| metamagicGrantCount > 1 || grandGrantCount > 1
		|| (metamagicGrantCount != 0 && metamagicSequenceSpells.size() != 1)
		|| (grandGrantCount != 0 && (!metamagicGrandUsed || metamagicSequenceSpells.size() != 2)))
		throw std::runtime_error("Metamagic metadata does not match typed Spell grants");
}
