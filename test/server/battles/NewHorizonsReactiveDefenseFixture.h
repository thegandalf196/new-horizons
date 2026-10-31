/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/NewHorizonsReactiveDefense.h"
class NewHorizonsReactiveDefenseFixture : public HeroCommandFixture
{
protected:
	int mode = 0;
	int physicalMode = -1;
	CStack * subject = nullptr;
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const auto & [key, value] : {std::pair{"demonicSolidarityReductionPercent", 20},
			std::pair{"ironHideReductionPercent", 30}})
			if(mode == 1) policy.Struct().erase(key);
			else policy[key].Integer() = mode == 2 ? 0 : value;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		if(physicalMode >= 0)
		{
			JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
			if(physicalMode == 1) magic.Struct().erase("physicalDamageReductionCapPercent");
			magic.setOverrideFlag(true);
			map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		}
	}
	void prepare(bool marius, int captureMode = 0, bool upgraded = false, int physicalPolicy = -1)
	{
		mode = captureMode;
		physicalMode = physicalPolicy;
		const auto family = creatureByName(marius ? (upgraded ? "core:hornedDemon" : "core:demon")
			: (upgraded ? "core:mightyGorgon" : "core:gorgon"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(marius ? "core:marius" : "core:alkin")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{family, 100}, {creatureByName("core:pikeman"), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0)) enemy = unit.get();
			else if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0)) subject = unit.get();
			else if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(1)) ally = unit.get();
		ASSERT_NE(subject, nullptr); ASSERT_NE(ally, nullptr); ASSERT_NE(enemy, nullptr);
		subject->setPosition(BattleHex(3, 5)); enemy->setPosition(BattleHex(4, 5)); ally->setPosition(BattleHex(3, 2));
		for(auto * unit : {subject, ally, enemy})
		{
			for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
					unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
						BonusSource::OTHER, 2, BonusSourceID(), subtype, bound));
		}
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
	}
	void injure(std::initializer_list<std::pair<CStack *, int64_t>> damage)
	{
		StacksInjured packet; packet.battleID = BattleID(0);
		for(const auto & [unit, amount] : damage)
		{
			auto & hit = packet.stacks.emplace_back();
			hit.stackAttacked = unit->unitId(); hit.damageAmount = amount;
			unit->prepareAttacked(hit, gameHandler->getRandomGenerator());
		}
		gameHandler->sendAndApply(packet);
	}
};
