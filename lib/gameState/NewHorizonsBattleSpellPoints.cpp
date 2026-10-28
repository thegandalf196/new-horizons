/*
 * NewHorizonsBattleSpellPoints.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBattleSpellPoints.h"
#include "CGameState.h"
#include "../entities/hero/NewHorizonsRecoverySpecialties.h"
#include "../entities/hero/NewHorizonsManaLeeches.h"
#include "../battle/BattleInfo.h"
#include "../spells/NewHorizonsMagic.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include <limits>
namespace newHorizonsHeroes
{
void applyManaLeechRecovery(CGHeroInstance & hero, SideInBattle * side, int32_t amount)
{
	const auto policy = hasManaLeeches(&hero) ? manaLeechesAmount(hero.getPrimaryGrowthRules()).value_or(0) : 0;
	if(!side || side->heroID != hero.id || policy <= 0 || amount < 0 || amount > policy)
		throw std::runtime_error("Mana Leeches recovery requires an entitled active battle hero");
	side->validateManaLeechesSerialization(true);
	const auto before = hero.getNormalSpellPoints();
	const auto room = std::max<int64_t>(0, static_cast<int64_t>(hero.manaLimit()) - before);
	const auto actual = static_cast<int32_t>(std::min<int64_t>(amount, room));
	// A zero grant must not clamp unrelated over-cap Normal Mana.
	if(actual > 0 && !hero.restoreNormalSpellPoints(actual))
		throw std::runtime_error("Invalid Mana Leeches Normal restoration");
	side->manaLeechesRecoveredNormal = static_cast<int32_t>(std::min<int64_t>(
		std::numeric_limits<int32_t>::max(), static_cast<int64_t>(side->manaLeechesRecoveredNormal)
			+ std::max<int32_t>(0, hero.getNormalSpellPoints() - before)));
}

BattleSpellPointEntry validateBattleSpellPointEntry(const CGameState & state, const BattleInfo & battle)
{
	const auto attacker = battle.getSide(BattleSide::ATTACKER).heroID;
	const auto defender = battle.getSide(BattleSide::DEFENDER).heroID;
	if(attacker.hasValue() && attacker == defender)
		throw std::runtime_error("BattleStart assigns the same hero to both sides");
	BattleSpellPointEntry result;
	result.acceptedDay = state.day;
	// Preserve the existing complete pool validation before inspecting new policy.
	for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & side = battle.getSide(sideID);
		side.validateManaFurnaceSerialization(true);
		side.validateManaLeechesSerialization(true);
		if(side.manaLeechesRecoveredNormal != 0)
			throw std::runtime_error("BattleStart contains already-applied Mana Leeches recovery");
		side.validateRecoverySpecialtySerialization(true);
		if(side.deepTranceConsumed || side.initialAcceptedCombatDay != -1 || side.deepTranceRecoveredNormal != 0)
			throw std::runtime_error("BattleStart contains already-applied Deep Trance recovery");
		if(side.manaFurnaceRecoveredNormal != 0)
			throw std::runtime_error("BattleStart contains already-applied Mana Furnace recovery");
		if(side.initialNormalSpellPoints < 0 || side.initialBufferSpellPoints < 0 || side.temporaryBufferRemaining < 0)
			throw std::runtime_error("Invalid negative battle Spell Point snapshot");
		const int64_t buffer = static_cast<int64_t>(side.initialBufferSpellPoints) + side.temporaryBufferRemaining;
		if(buffer > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Battle temporary Spell Point Buffer overflows its persistent snapshot");
	}
	for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(const auto * army = battle.getSideArmy(sideID); army && army->battle)
			throw std::runtime_error("BattleStart army is already engaged in combat");
		const auto & side = battle.getSide(sideID);
		if(!side.heroID.hasValue())
			continue;
		const auto * hero = state.getHero(side.heroID);
		if(!hero)
			throw std::runtime_error("BattleStart references a missing hero");
		hero->validateRecoverySpecialtyStateSerialization(true);
		if(hero->getNewHorizonsLastAcceptedCombatDay() > result.acceptedDay)
			throw std::runtime_error("Accepted combat receipt is ahead of the world day");
		result.deepTrance[sideID] = hasCapturedDeepTrance(*hero)
			&& hero->getNewHorizonsLastAcceptedCombatDay() != result.acceptedDay;
		result.manaFurnace[sideID] = manaFurnaceRules(hero->getPrimaryGrowthRules());
	}
	return result;
}

void applyAcceptedBattleSpellPointEntry(CGameState & state, BattleInfo & battle, const BattleSpellPointEntry & spellPointEntry)
{
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if (battle.getSide(i).heroID.hasValue())
		{
			CGHeroInstance * hero = state.getHero(battle.getSide(i).heroID);
			auto & side = battle.getSide(i);
			if(newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
			{
				hero->restoreSpellPointSnapshot(side.initialNormalSpellPoints, side.initialBufferSpellPoints);
				if(side.temporaryBufferRemaining > 0 && !hero->grantBufferSpellPoints(side.temporaryBufferRemaining))
					throw std::runtime_error("Failed to grant temporary combat Spell Points");
				if(side.additionalMana < 0)
				{
					// Preserve signed combat-mana penalties, consuming available
					// energy Buffer-first just like any other Mana drain.
					const int64_t drain = std::min(hero->getManaAvailable(), -static_cast<int64_t>(side.additionalMana));
					if(!hero->spendSpellPoints(drain))
						throw std::runtime_error("Failed to apply combat Spell Point penalty");
				}
			}
			else
			{
				const auto initial = std::clamp<int64_t>(static_cast<int64_t>(side.initialMana) + side.additionalMana,
					0, std::numeric_limits<int32_t>::max());
				hero->setNormalSpellPoints(static_cast<int32_t>(initial));
			}
			if(const auto & furnace = spellPointEntry.manaFurnace[i])
			{
				const auto beforeRecovery = hero->getNormalSpellPoints();
				const auto restored = newHorizonsHeroes::manaFurnaceRestoration(hero, *furnace);
				if(restored > 0 && !hero->restoreNormalSpellPoints(restored))
					throw std::runtime_error("Failed to apply accepted Mana Furnace recovery");
				side.manaFurnaceRecoveredNormal = hero->getNormalSpellPoints() - beforeRecovery;
			}
			if(spellPointEntry.deepTrance[i])
			{
				side.initialAcceptedCombatDay = hero->getNewHorizonsLastAcceptedCombatDay();
				side.deepTranceConsumed = true;
				hero->setNewHorizonsLastAcceptedCombatDay(spellPointEntry.acceptedDay);
				const auto beforeRecovery = hero->getNormalSpellPoints();
				const auto restored = firstCombatMysticismRecovery(*hero);
				if(restored > 0 && !hero->restoreNormalSpellPoints(restored))
					throw std::runtime_error("Failed to apply accepted Deep Trance recovery");
				side.deepTranceRecoveredNormal = hero->getNormalSpellPoints() - beforeRecovery;
			}
		}
	}

}

}
