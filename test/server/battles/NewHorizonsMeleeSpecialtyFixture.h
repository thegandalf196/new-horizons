/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/battle/NewHorizonsShieldAndSword.h"
#include "../../../lib/battle/NewHorizonsSepulchralGrip.h"
class MeleeSpecialtyRecordingServer : public RecordingGameServer
{
public:
	uint32_t actorId = 0;
	std::vector<bool> shieldAtAttack;
	int shieldPublications = 0;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack))
		{
			const auto * actor = gameState->getBattle(attack->battleID)->battleGetUnitByID(actorId);
			shieldAtAttack.push_back(actor && actor->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)));
		}
		if(const auto * effect = dynamic_cast<const SetStackEffect *>(&pack))
			for(const auto & [id, bonuses] : effect->toAdd)
				if(id == actorId)
					for(const auto & bonus : bonuses)
						shieldPublications += newHorizonsShieldAndSword::isProtection(&bonus) ? 1 : 0;
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsMeleeSpecialtyFixture : public HeroCommandFixture
{
protected:
	int mode = 0;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	MeleeSpecialtyRecordingServer observed;
	void TearDown() override
	{
		gameHandler.reset();
		observed.gameState.reset();
		HeroCommandFixture::TearDown();
	}
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"shieldAndSwordPhysicalReductionPercent", "sepulchralGripSpeedReduction"})
		{
			if(mode == 1) policy.Struct().erase(key);
			if(mode == 2) policy[key].Integer() = 0;
		}
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero, int captureMode = 0, bool upgraded = false)
	{
		mode = captureMode;
		const bool swordsman = std::string_view(hero) == "core:sorsha";
		const auto creature = creatureByName(swordsman ? (upgraded ? "core:crusader" : "core:swordsman")
			: (upgraded ? "core:zombieLord" : "core:walkingDead"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creature, 50}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		observed.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(observed, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actor : victim) = unit.get();
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr);
		observed.actorId = actor->unitId();
		actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(4, 5));
		for(auto * unit : {actor, victim})
		{
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
					unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
						BonusSource::OTHER, 2, BonusSourceID(), subtype, bound));
		}
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
	}
};
