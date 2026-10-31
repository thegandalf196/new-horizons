/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <string_view>
#include "HeroCommandFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../../lib/battle/NewHorizonsInfernoStrikeDebuffs.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
class InfernoStrikeDebuffFixture : public HeroCommandFixture
{
protected:
	int captureMode = 0;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	CStack * collateral = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction"})
			if(captureMode == 1) policy.Struct().erase(key);
			else policy[key].Integer() = captureMode == 2 ? 0
				: std::string_view(key) == "cruelOverseerDefenseReduction" ? 3 : 20;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(bool calh, bool upgraded = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		const char * creature = calh ? (upgraded ? "core:magog" : "core:gog")
			: (upgraded ? "core:pitLord" : "core:pitFiend");
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(calh ? "core:calh" : "core:nymus")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName(creature), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			const auto skills = hero->secSkills;
			for(const auto & [skill, rank] : skills) hero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
			for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
				hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
		}
		startBattle();
		BattleUnitsChanged remove; remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false)) remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(calh ? 8 : 4, 5), 1000);
		collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(calh ? 8 : 4, 6), 1000);
		for(auto * unit : {actor, victim, collateral})
		{
			forceMaximumDamage(unit);
			for(auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE}) unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
			for(auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX}) unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, unit == actor ? 60 : unit == victim ? 20 : 10, BonusSourceID(), BonusSubtypeID(), bound));
		}
		blockRetaliation(actor);
	}
	void publish(const newHorizonsOrdinaryStrike::EffectPlan & plan)
	{
		SetStackEffect effect; effect.battleID = BattleID(0);
		if(!plan.remove.empty()) effect.toRemove.emplace_back(plan.target, plan.remove);
		if(!plan.add.empty()) effect.toAdd.emplace_back(plan.target, plan.add);
		gameHandler->sendAndApply(effect);
	}
	static auto effects(const battle::Unit & unit) { return unit.getUnstackedBonuses(CSelector(newHorizonsInfernoStrikeDebuffs::isEffect)); }
};
template<typename Random> auto infernoRandomBytes(Random & random)
{
	CMemorySerializer memory; memory.oser & random; return memory.extractBuffer();
}
