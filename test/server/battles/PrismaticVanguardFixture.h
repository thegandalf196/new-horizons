/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/battle/NewHorizonsPrismaticVanguard.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
class PrismaticVanguardFixture : public HeroCommandFixture
{
protected:
	int capture = 20; // -1 absent,0 captured replacement without effect.
	CStack * actor = nullptr;
	CStack * upgraded = nullptr;
	CStack * victim = nullptr;
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		if(capture < 0) policy.Struct().erase("prismaticVanguardPhysicalDamagePercent");
		else policy["prismaticVanguardPhysicalDamagePercent"].Integer() = capture;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:monere")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 10},
				{creatureByName("core:halberdier"), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1000}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		// SOD encodes only stock IDs. Install resolved module bodies before real battle setup.
		attackerSideHero->clearSlots();
		for(const auto & [slot, key] : {std::pair{0, "new-horizons:wisp"}, std::pair{1, "new-horizons:wispUpgrade"}})
		{
			const auto creature = creatureByName(key);
			ASSERT_TRUE(attackerSideHero->setCreature(SlotID(slot), creature, 10));
			const auto * body = attackerSideHero->getStackPtr(SlotID(slot));
			ASSERT_NE(body, nullptr);
			ASSERT_EQ(body->getCreatureID(), creature);
			ASSERT_EQ(body->getCount(), 10);
		}
		ASSERT_NO_FATAL_FAILURE(startBattle());
		for(const auto & stack : battle()->stacks)
		{
			if(stack->unitSide() == BattleSide::ATTACKER && stack->unitSlot() == SlotID(0)) actor = stack.get();
			if(stack->unitSide() == BattleSide::ATTACKER && stack->unitSlot() == SlotID(1)) upgraded = stack.get();
			if(stack->unitSide() == BattleSide::DEFENDER && stack->unitSlot() == SlotID(0)) victim = stack.get();
		}
		ASSERT_NE(actor, nullptr); ASSERT_NE(upgraded, nullptr); ASSERT_NE(victim, nullptr);
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
		for(auto * unit : {actor, upgraded, victim})
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	}
	static TConstBonusListPtr markers(const battle::Unit & unit)
	{
		return unit.getUnstackedBonuses(CSelector(newHorizonsPrismaticVanguard::isEffect));
	}
};
