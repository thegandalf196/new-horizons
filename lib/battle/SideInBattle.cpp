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

void SideInBattle::init(const CGHeroInstance * Hero, const CArmedInstance * Army, const CGTownInstance * town)
{
	manaFurnaceRecoveredNormal = 0;
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

int32_t SideInBattle::legacySpellPointCompletionCeiling() const
{
	validateManaFurnaceSerialization(true);
	validateRecoverySpecialtySerialization(true);
	return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(initialMana) + manaFurnaceRecoveredNormal + deepTranceRecoveredNormal,
		0, std::numeric_limits<int32_t>::max()));
}
