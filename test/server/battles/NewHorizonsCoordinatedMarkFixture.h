/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
class CoordinatedMarkRecordingServer : public RecordingGameServer
{
public:
	std::vector<BattleHex> sourcePositionsAtPublication;
	std::vector<bool> sourceAliveAtPublication;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * effect = dynamic_cast<const SetStackEffect *>(&pack))
			for(const auto & [target, bonuses] : effect->toAdd)
				for(const auto & bonus : bonuses)
					if(newHorizonsCoordinatedMarks::isMark(&bonus))
					{
						const auto sourceId = bonus.parameters->toVector().at(1);
						const auto * source = gameState->getBattle(effect->battleID)->battleGetUnitByID(sourceId);
						sourcePositionsAtPublication.push_back(source ? source->getPosition() : BattleHex::INVALID);
						sourceAliveAtPublication.push_back(source && source->alive());
					}
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsCoordinatedMarkFixture : public HeroCommandFixture
{
protected:
	int mode = 0;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	CoordinatedMarkRecordingServer observed;
	void TearDown() override
	{
		gameHandler.reset(); observed.gameState.reset(); HeroCommandFixture::TearDown();
	}
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true; HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent"})
		{
			if(mode == 0) policy[key].Integer() = std::string_view(key) == "consecratedTargetDefenseIgnorePercent" ? 25 : 20;
			if(mode == 1) policy.Struct().erase(key);
			if(mode == 2) policy[key].Integer() = 0;
		}
		rules.setOverrideFlag(true); map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void addReturnMoat(BattleHex origin)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = 731; obstacle->ID = SpellID::decode("core:castleMoat");
		obstacle->trigger = SpellID(SpellID::decode("core:castleMoatTrigger"));
		obstacle->pos = origin; obstacle->obstacleType = CObstacleInstance::MOAT;
		obstacle->casterSide = BattleSide::DEFENDER; obstacle->minimalDamage = 70;
		obstacle->passable = true; obstacle->trap = true; obstacle->customSize.insert(origin);
		battle()->obstacles.push_back(obstacle);
	}
	void prepare(const char * hero, int captureMode = 0, bool upgraded = false)
	{
		mode = captureMode;
		const bool shot = std::string_view(hero) == "core:ingham";
		const auto creature = creatureByName(shot ? (upgraded ? "core:zealot" : "core:monk")
			: (upgraded ? "core:harpyHag" : "core:harpy"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creature, 20}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 100}});
		startWithMap(std::move(builder)); server.gameState = gameState(); observed.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(observed, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0)); defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0)) (unit->unitSide() == BattleSide::ATTACKER ? actor : victim) = unit.get();
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr);
		actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(shot ? 10 : 5, 5));
		for(auto * unit : {actor, victim})
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
	}
};
