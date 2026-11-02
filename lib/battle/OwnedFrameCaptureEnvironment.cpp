/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameCaptureEnvironment.h"
#include "LiveUnitEnvironmentContext.h"
#include "UnitOwnershipQuery.h"
#include "../GameLibrary.h"
#include "../IGameSettings.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsNecromancy.h"
namespace battle::ownedFrameCapture
{
OwnedUnitEnvironment::Facts environmentFacts(const CBattleInfoCallback & callback)
{
	const LiveUnitEnvironmentContext source(callback);
	OwnedUnitEnvironment::Facts facts; facts.magicRules = source.magicRules();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		facts.colors.at(side) = source.ownership().sidePlayer(side);
	// Static explanation identity, independent of any Hero/perk presence.
	const SecondarySkill necromancy(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID));
	if(necromancy.hasValue()) facts.necromancyLuckSource = necromancy;
	facts.skeletonUpgrades = CreatureID(CreatureID::SKELETON).toCreature()->upgrades;
	const auto & settings = *LIBRARY->engineSettings();
	facts.legacyLuckLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE).size()),
		static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE).size())};
	facts.legacyMoraleLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size()),
		static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size())};
	return facts; // No public hidden-Hero policy/flags are populated in Facts::inputs.
}
}
