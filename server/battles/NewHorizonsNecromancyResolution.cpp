/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRaisedArmy.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/entities/hero/NewHorizonsIndividualSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/battle/CBattleInfoEssentials.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
namespace newHorizonsNecromancy
{
PreparedBattleNecromancy prepareBattleNecromancy(const BattleResult & result,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules,
	int32_t initialMana, const CGHeroInstance & hero, const PlayerState * ownerState)
{
	const auto capturedPolicy = policy(hero.getPerkState().rules);
	const auto losingSide = CBattleInfoEssentials::otherSide(result.winner);
	const auto & eligible = result.necromancyEligibilityCaptured
		? result.necromancyEligibleCasualties[losingSide]
		: result.casualties[losingSide];
	const auto eligibleCount = newHorizonsNecromancy::countLivingEligibleCasualties(eligible);
	const auto eligibleCoreCount = newHorizonsNecromancy::countLivingEligibleCoreCasualties(eligible, categoryRules);
	const auto eligibleEliteCount = newHorizonsNecromancy::countLivingEligibleEliteCasualties(eligible, categoryRules);

	const bool boneCollector = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::BONE_COLLECTOR_ID);
	const bool corpsePreservation = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
	const bool darkConversion = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DARK_CONVERSION_ID);
	const bool soulHarvester = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::SOUL_HARVESTER_ID);
	const bool deathLord = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DEATH_LORD_ID);
	const bool graveKnowledge = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	const bool masterOfBones = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::MASTER_OF_BONES_ID);
	const bool ossuary = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::OSSUARY_ID);
	const bool lordOfTheDead = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::LORD_OF_THE_DEAD_ID);
	const bool blackHarvest = hero.hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::BLACK_HARVEST_ID);
	const auto skeletonOutput = masterOfBones
		? newHorizonsNecromancy::availableNecropolisSkeletonUpgrade(ownerState)
		: CreatureID::NONE;
	newHorizonsNecromancy::SpecialCasualtyCounts nonlivingCasualties;
	newHorizonsNecromancy::SpecialCasualtyCounts undeadCasualties;
	if(result.necromancySpecialEligibilityCaptured)
	{
		if(deathLord)
			nonlivingCasualties = newHorizonsNecromancy::countEligibleNonlivingCasualties(
				result.necromancyNonlivingEligibleCasualties[losingSide], categoryRules);
		if(graveKnowledge)
			undeadCasualties = newHorizonsNecromancy::countEligibleUndeadCasualties(
				result.necromancyUndeadEligibleCasualties[losingSide], categoryRules);
	}

	const int32_t postBattleMana = newHorizonsNecromancy::postBattleNormalSpellPoints(hero, initialMana);
	// Black Harvest uses remaining Normal capacity. Combat-only Buffer never
	// suppresses a recovery that fits in Normal.
	auto summary = newHorizonsNecromancy::resolve(hero.getNewHorizonsNecromancyRank(), eligibleCount, eligibleCoreCount,
		boneCollector, corpsePreservation, darkConversion,
		true, true, postBattleMana,
		blackHarvest ? hero.manaLimit() : postBattleMana, eligibleEliteCount, soulHarvester, true, skeletonOutput,
		nonlivingCasualties, undeadCasualties, lordOfTheDead,
		result.necromancyDefeatedArmyHadLivingChampion, true,
		hero.getNewHorizonsNecromancyAmplifierBonusPercent(),
		newHorizonsHeroes::graveReaperPercentagePoints(&hero),
		capturedPolicy.has_value());
	const auto enhanced = reclaimResidualSkeletons(summary,
		newHorizonsHeroes::reclaimerConversionPercent(hero, ownerState), postBattleMana,
		blackHarvest ? hero.manaLimit() : postBattleMana);
	return {summary, enhanced, ossuary};
}
}
