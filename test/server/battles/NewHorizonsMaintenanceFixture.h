/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/callback/GameRandomizer.h"
class NewHorizonsMaintenanceFixture : public HeroCommandFixture
{
protected:
	int captureMode = 0;
	CStack * actor = nullptr;
	CStack * upgraded = nullptr;
	CStack * victim = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations"})
		{
			if(captureMode == 1) policy.Struct().erase(key);
			if(captureMode == 2) policy[key].Integer() = 0;
		}
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero, int mode = 0)
	{
		captureMode = mode;
		const bool golem = std::string_view(hero) == "core:josephine";
		const bool ogre = std::string_view(hero) == "core:krellion";
		const auto base = CreatureID(CreatureID::decode(golem ? "core:ironGolem" : ogre ? "core:ogre" : "core:pikeman"));
		const auto upgrade = CreatureID(CreatureID::decode(golem ? "core:stoneGolem" : ogre ? "core:ogreMage" : "core:halberdier"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{base, 10}, {upgrade, 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		if(!golem && !ogre) giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		startBattle();
		for(const auto & stack : battle()->stacks)
		{
			if(stack->unitSide() == BattleSide::DEFENDER && stack->unitSlot() == SlotID(0)) victim = stack.get();
			if(stack->unitSide() != BattleSide::ATTACKER) continue;
			if(stack->unitSlot() == SlotID(0) && (golem || ogre)) actor = stack.get();
			if(stack->isBallista() && !golem && !ogre) actor = stack.get();
			if(stack->unitSlot() == SlotID(1)) upgraded = stack.get();
		}
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr);
		actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(8, 5));
	}
	void forceActorFirst()
	{
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
		for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	}
};
