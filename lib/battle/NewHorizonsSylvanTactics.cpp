/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSylvanTactics.h"
#include "NewHorizonsCombatSkills.h"
#include "CBattleInfoCallback.h"
#include "BattleAttackInfo.h"
#include "IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/Limiters.h"
#include "../combatScripts/IDamageCalculatorScript.h"

namespace newHorizonsSylvanTactics
{
bool ordinary(const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost()
		&& unit->unitSlot() != SlotID::WAR_MACHINES_SLOT && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit);
}
bool enabled(const CGHeroInstance * hero)
{
	return hero && newHorizonsHeroes::usesSylvanTactics(hero->getPerkState().rules);
}
void configure(const CGHeroInstance & hero, SylvanLuckState & fortune)
{
	if(!enabled(&hero))
	{
		fortune.serendipity = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.serendipity");
		fortune.naturesProvidence = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.natureSProvidence");
		fortune.fortunateAim = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.fortunateAim");
		fortune.perfectMoment = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.perfectMoment");
		fortune.forestsFavor = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.forestSFavor");
		fortune.luckyRecovery = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.luckyRecovery");
		fortune.sharedFortune = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.sharedFortune");
		fortune.cascadingFortune = hero.hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.cascadingFortune");
		return;
	}
	if(hero.getPerkSkillRank("new-horizons:sylvanLuck") <= 0) return;
	auto & tactics = fortune.tactics;
	tactics.enabled = true;
	const auto perk = [&hero](const char * id)
	{
		return hero.hasActivePerk("new-horizons:sylvanLuck", std::string("new-horizons:sylvanLuck.") + id);
	};
	tactics.fleetfoot = perk("elvenPrecision");
	tactics.patientHunter = perk("forestSFavor");
	tactics.dappledCover = perk("serendipity");
	tactics.measuredAdvance = perk("luckyRecovery");
	tactics.coveringFire = perk("sharedFortune");
	tactics.crossfire = perk("natureSProvidence");
	tactics.disruptiveStrike = perk("fortunateAim");
	tactics.quickRedeployment = perk("wildChance");
	tactics.perfectMoment = perk("perfectMoment");
	tactics.wildHunt = perk("cascadingFortune");
}
std::vector<std::shared_ptr<Bonus>> rankBonuses(const CGHeroInstance & hero,
	SecondarySkill skill, int rank, const std::vector<std::shared_ptr<Bonus>> & current)
{
	if(SecondarySkill::encode(skill.getNum()) != "new-horizons:sylvanLuck") return current;
	const auto source = BonusSourceID(skill);
	if(enabled(&hero))
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_INITIATIVE_FLAT,
			BonusSource::SECONDARY_SKILL, std::clamp(rank, 0, 3), source);
		// Acceptance runs while the receiving stack's cache is being rebuilt.
		// Resolve siege eligibility from the limiter's accepted/pending lists,
		// never by recursively querying that same stack's bonus cache.
		bonus->limiter = std::make_shared<NoneOfLimiter>(std::vector<TLimiterPtr>{
			std::make_shared<HasAnotherBonusLimiter>(BonusType::SIEGE_WEAPON)});
		return {bonus};
	}
	constexpr int legacyDamage[] = {0, 25, 60, 100};
	return {std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
			BonusSource::SECONDARY_SKILL, rank, source),
		std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE,
			BonusSource::SECONDARY_SKILL, legacyDamage[std::clamp(rank, 0, 3)], source)};
}
void finalizeDamage(const CBattleInfoCallback & battle, const BattleAttackInfo & info,
	DamageAttackInfo & payload)
{
	if(!info.physicalDamage || !ordinary(info.attacker) || !info.defender) return;
	const auto side = battle.playerToSide(battle.battleGetOwner(info.attacker));
	const auto targetSide = battle.playerToSide(battle.battleGetOwner(info.defender));
	if(side == BattleSide::NONE || targetSide == BattleSide::NONE) return;
	const auto tactics = battle.getBattle()->getSylvanLuckState(side).tactics;
	if(tactics.enabled)
	{
		if(tactics.perfectMoment && info.luckyStrike) payload.sylvanLuckyStrikeTotalPercent = PERFECT_MOMENT_TOTAL_PERCENT;
		if(tactics.patientHunter && info.attacker->battlecraftWaitBonusAvailable())
			payload.sylvanAttackMultipliers.push_back(100 + PATIENT_HUNTER_PERCENT);
		const bool primaryMelee = !info.shooting && !info.secondaryAttack && !info.retaliation
			&& !info.bracePreemptive && info.preemptiveDamagePercent == 0 && info.cleaveDamagePercent == 0;
		if(tactics.measuredAdvance && primaryMelee && info.chargeDistance >= 1
			&& info.chargeDistance <= static_cast<int>(info.attacker->getMovementRange()) / 2)
			payload.sylvanAttackMultipliers.push_back(100 + MEASURED_ADVANCE_PERCENT);
		if(info.shooting && !info.secondaryAttack && side != targetSide
			&& tactics.crossfireBonus(info.attacker->unitId(), info.defender->unitId()))
			payload.sylvanAttackMultipliers.push_back(100 + SylvanTacticsState::CROSSFIRE_PERCENT);
	}
	if(info.shooting)
		for(int reduction : rangedProtection(battle, info.defender))
			payload.sylvanDefenseMultipliers.push_back(100 - reduction);
}
}
