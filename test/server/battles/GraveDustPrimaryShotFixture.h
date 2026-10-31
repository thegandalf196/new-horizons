/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/HealingReceipt.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include <algorithm>
class GraveDustShotServer final : public RecordingGameServer
{
public:
	std::vector<uint32_t> dustRecipients;
	std::vector<int32_t> installedReductions;
	void applyPack(CPackForClient & pack) override
	{
		std::vector<uint32_t> incoming;
		if(const auto * effects = dynamic_cast<const SetStackEffect *>(&pack))
			for(const auto & [id, records] : effects->toAdd)
				for(const auto & record : records)
					if(newHorizonsGraveDust::isEffect(&record) && record.type == BonusType::PHYSICAL_AFFLICTION)
						incoming.push_back(id);
		RecordingGameServer::applyPack(pack);
		for(const auto id : incoming)
		{
			const auto & effects = static_cast<const SetStackEffect &>(pack);
			const auto * unit = gameState->getBattle(effects.battleID)->battleGetUnitByID(id);
			dustRecipients.push_back(id);
			installedReductions.push_back(battle::healingReceivedReduction(*unit));
		}
	}
};
class GraveDustPrimaryShotFixture : public HeroCommandFixture
{
protected:
	int captureMode = 0;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	CStack * collateral = nullptr;
	std::unique_ptr<GraveDustShotServer> observed;
	void TearDown() override
	{
		gameHandler.reset(); // Its borrowed IGameServer must remain alive until this completes.
		if(observed) observed->gameState.reset();
		observed.reset();
		server.gameState.reset();
		HeroCommandFixture::TearDown();
	}
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		if(captureMode == 1) policy.Struct().erase("graveDustHealingReductionBasisPoints");
		else policy["graveDustHealingReductionBasisPoints"].Integer() = captureMode == 2 ? 0 : 5000;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(bool upgraded, int mode = 0, bool watch = false, int victimCount = 200, bool melee = false)
	{
		captureMode = mode;
		const auto creature = creatureByName(upgraded ? "core:powerLich" : "core:lich");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:moandor")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creature, 20}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), victimCount}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		observed = std::make_unique<GraveDustShotServer>();
		observed->gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(*observed, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		if(watch)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:battlecraft")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.overwatch"});
			ASSERT_TRUE(newHorizonsBattlecraft::hasOverwatch(attackerSideHero));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = battle()->getBattleID();
		for(const auto * unit : battle()->battleGetAllUnits(false)) remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creature, watch ? BattleHex(14, 5) : BattleHex(3, 5), 20);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), watch ? BattleHex(2, 5) : melee ? BattleHex(4, 5) : BattleHex(12, 5), victimCount);
		collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), watch ? BattleHex(2, 7) : BattleHex(12, 6), 200);
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr); ASSERT_NE(collateral, nullptr);
		for(auto * unit : {actor, victim, collateral})
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
		for(auto * unit : {victim, collateral})
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, unit == victim ? 30 : 10, BonusSourceID(), BonusSubtypeID(), bound));
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
		for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE, BonusSource::OTHER, 2, BonusSourceID(), subtype, bound));
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	}
	bool shoot()
	{
		return gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0), BattleAction::makeShotAttack(actor, victim));
	}
};
