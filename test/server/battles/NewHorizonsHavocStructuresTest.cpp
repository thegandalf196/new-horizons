/*
 * NewHorizonsHavocStructuresTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/ObstacleHandler.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../SpellPointTestUtils.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view meteorShowerKey = "core:meteorShower";
constexpr std::string_view armageddonKey = "core:armageddon";
const BattleHex meteorCenter(8, 5);
constexpr int32_t meteorUnitDamageAtZeroPower = 110;
constexpr int32_t meteorStructuralDamageAtZeroPower = 55;
constexpr int32_t armageddonUnitDamageAtZeroPower = 150;
constexpr int32_t armageddonStructuralDamageAtZeroPower = 150;

SpellID spellNamed(const std::string_view name)
{
	return SpellID(SpellID::decode(std::string(name)));
}

int findObstacleID(const std::string_view key)
{
	int result = -1;
	LIBRARY->obstacles()->forEach([&](const ObstacleInfo * obstacle, bool & stop)
	{
		if(obstacle->getJsonKey() == key)
		{
			result = obstacle->getIndex();
			stop = true;
		}
	});
	return result;
}

JsonNode magicRulesForFixture(const bool omitStructures)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(omitStructures)
	{
		// A saved-v3 profile predating the structural producer has no `structures`
		// payload. Keep the direct-damage rows and version intact to exercise that
		// compatibility case rather than dropping back to the pre-v2 spell rules.
		rules[std::string("spells")][std::string(meteorShowerKey)].Struct().erase("structures");
		rules[std::string("spells")][std::string(armageddonKey)].Struct().erase("structures");
		newHorizonsMagic::validateRules(rules);
	}
	return rules;
}

class HavocPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HavocPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHavocStructuresTest : public HeroCommandFixture
{
protected:
	bool omitStructures = false;
	const CSpell * activeSpell = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRulesForFixture(omitStructures));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const std::string_view spellKey, const bool siege = false, const bool legacy = false)
	{
		omitStructures = legacy;
		startGame(siege);
		activeSpell = spellNamed(spellKey).toSpell();
		ASSERT_NE(activeSpell, nullptr);

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(activeSpell->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		if(siege)
		{
			const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
			ASSERT_EQ(towns.size(), 1u);
			ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
			startBattle(towns.front());
		}
		else
			startBattle();

		removeDeployedUnits();
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void beginScenario()
	{
		beginCombat();
		advanceToAttackerAction();
	}

	void advanceToAttackerAction()
	{
		for(int index = 0; index < 100; ++index)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			const auto owner = battle()->battleGetOwner(active);
			if(owner == attackerSideHero->getOwner())
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), owner,
				BattleAction::makeDefend(active)));
		}
		FAIL() << "No legal attacker-side action became available";
	}

	bool castAtHex(const BattleHex & target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = activeSpell->getId();
		action.aimToHex(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}

	bool castGlobally()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = activeSpell->getId();
		action.stackNumber = -1;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}

	std::shared_ptr<CObstacleInstance> addPhysicalObstacle(const int uniqueID, const BattleHex & position)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = findObstacleID("core:0");
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::USUAL;
		EXPECT_GE(obstacle->ID, 0);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<CObstacleInstance> addAbsoluteObstacle(const int uniqueID)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = findObstacleID("core:101");
		obstacle->pos = BattleHex::INVALID;
		obstacle->obstacleType = CObstacleInstance::ABSOLUTE_OBSTACLE;
		EXPECT_GE(obstacle->ID, 0);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<SpellCreatedObstacle> addMoat(const int uniqueID, const BattleHex & position)
	{
		// Non-dispellable town moats are still represented by a spell-obstacle
		// descriptor with MOAT behavior and an explicit hex footprint.
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = SpellID::decode("core:castleMoat");
		obstacle->trigger = SpellID(SpellID::decode("core:castleMoatTrigger"));
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::MOAT;
		obstacle->casterSide = BattleSide::DEFENDER;
		obstacle->minimalDamage = 70;
		obstacle->passable = true;
		obstacle->trap = true;
		obstacle->customSize.insert(position);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<SpellCreatedObstacle> addMagicObstacle(const int uniqueID, const BattleHex & position)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = SpellID::QUICKSAND;
		obstacle->pos = position;
		obstacle->passable = true;
		obstacle->customSize.insert(position);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::map<EWallPart, int32_t> structuralHP() const
	{
		std::map<EWallPart, int32_t> result;
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			const auto hp = battle()->getWallStructuralHP(part);
			if(hp > 0)
				result.emplace(part, hp);
		}
		return result;
	}

	template <typename Obstacles>
	static bool containsObstacle(const Obstacles & obstacles, const int uniqueID)
	{
		return std::any_of(obstacles.begin(), obstacles.end(), [uniqueID](const auto & obstacle)
		{
			return obstacle && obstacle->uniqueID == uniqueID;
		});
	}

	BattleInfo * battle() const
	{
		return HeroCommandFixture::battle();
	}
};
}

TEST_F(NewHorizonsHavocStructuresTest, MeteorShowerRemovesOnlyPartiallyImpactedOrdinaryObstacle)
{
	prepare(meteorShowerKey);
	auto ordinary = addPhysicalObstacle(200, BattleHex(9, 5));
	auto outside = addPhysicalObstacle(201, BattleHex(4, 0));
	auto absolute = addAbsoluteObstacle(202);
	auto moat = addMoat(203, BattleHex(7, 5));
	auto magic = addMagicObstacle(204, meteorCenter);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(outside, nullptr);
	ASSERT_NE(absolute, nullptr);
	ASSERT_NE(moat, nullptr);
	ASSERT_NE(magic, nullptr);

	const auto ordinaryFootprint = ordinary->getAffectedTiles();
	ASSERT_EQ(ordinaryFootprint.size(), 2u);
	const auto inArea = std::count_if(ordinaryFootprint.begin(), ordinaryFootprint.end(), [](const BattleHex & hex)
	{
		return BattleHex::getDistance(meteorCenter, hex) <= 1;
	});
	ASSERT_EQ(inArea, 1) << "Meteor Shower removes a footprint if any, but not all, of it intersects the blast";

	bool absoluteOverlapsBlast = false;
	for(const auto hex : absolute->getAffectedTiles())
		absoluteOverlapsBlast = absoluteOverlapsBlast || BattleHex::getDistance(meteorCenter, hex) <= 1;
	ASSERT_TRUE(absoluteOverlapsBlast);
	ASSERT_LE(BattleHex::getDistance(meteorCenter, moat->pos), 1);
	ASSERT_LE(BattleHex::getDistance(meteorCenter, magic->pos), 1);

	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 0), 1000);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(hostile, nullptr);
	ASSERT_GT(BattleHex::getDistance(meteorCenter, friendly->getPosition()), 1);
	ASSERT_GT(BattleHex::getDistance(meteorCenter, hostile->getPosition()), 1);
	beginScenario();
	const auto friendlyHealth = friendly->getAvailableHealth();
	const auto hostileHealth = hostile->getAvailableHealth();

	ASSERT_TRUE(castAtHex(meteorCenter));
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyHealth);
	EXPECT_EQ(hostile->getAvailableHealth(), hostileHealth)
		<< "An obstacle-only Meteor Shower remains a legal, accepted location cast";
	EXPECT_FALSE(containsObstacle(battle()->obstacles, ordinary->uniqueID));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, outside->uniqueID));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, absolute->uniqueID));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, moat->uniqueID));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, magic->uniqueID));
}

TEST_F(NewHorizonsHavocStructuresTest, SpellLockDoesNotSuppressMeteorObstacleEffectsOrExtendThemFromDoubleWideFootprint)
{
	prepare(meteorShowerKey);
	const BattleHex center(12, 5);
	auto ordinaryInBlast = addPhysicalObstacle(205, BattleHex(13, 5));
	auto ordinaryOutside = addPhysicalObstacle(206, BattleHex(9, 4));
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * lockedDoubleWide = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(10, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 4), 1000);
	ASSERT_NE(ordinaryInBlast, nullptr);
	ASSERT_NE(ordinaryOutside, nullptr);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(lockedDoubleWide, nullptr);
	ASSERT_NE(hostile, nullptr);
	ASSERT_TRUE(lockedDoubleWide->doubleWide());
	ASSERT_TRUE(lockedDoubleWide->getHexes().contains(BattleHex(10, 5)));
	ASSERT_TRUE(lockedDoubleWide->getHexes().contains(BattleHex(11, 5)));
	ASSERT_GT(BattleHex::getDistance(center, BattleHex(10, 5)), 1)
		<< "The double-wide stack's primary hex is outside Meteor Shower's area";
	ASSERT_LE(BattleHex::getDistance(center, BattleHex(11, 5)), 1)
		<< "Only its rear hex is inside Meteor Shower's area";
	for(const auto hex : ordinaryInBlast->getAffectedTiles())
		EXPECT_FALSE(lockedDoubleWide->getHexes().contains(hex));
	for(const auto hex : ordinaryOutside->getAffectedTiles())
		EXPECT_FALSE(lockedDoubleWide->getHexes().contains(hex));

	const SpellID spellLock(SpellID::decode("new-horizons:spellLock"));
	auto lock = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(spellLock));
	lock->turnsRemain = 3;
	lockedDoubleWide->addNewBonus(std::move(lock));
	ASSERT_GT(BattleHex::getDistance(center, friendly->getPosition()), 1);
	beginScenario();
	const auto lockedHealth = lockedDoubleWide->getAvailableHealth();
	const auto hostileHealth = hostile->getAvailableHealth();

	ASSERT_TRUE(castAtHex(center));
	EXPECT_EQ(lockedDoubleWide->getAvailableHealth(), lockedHealth)
		<< "The Spell-Locked stack is stripped from creature damage recipients";
	EXPECT_EQ(hostileHealth - hostile->getAvailableHealth(), meteorUnitDamageAtZeroPower);
	EXPECT_FALSE(containsObstacle(battle()->obstacles, ordinaryInBlast->uniqueID));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, ordinaryOutside->uniqueID))
		<< "The double-wide stack's primary hex and rear hex must not widen the structural blast area";
}

TEST_F(NewHorizonsHavocStructuresTest, MeteorShowerDealsConfiguredHalfDamageToSiegeSectionsWithoutChangingUnitDamage)
{
	prepare(meteorShowerKey, true);
	ASSERT_TRUE(battle()->hasFortifications());
	ASSERT_TRUE(battle()->si.canonicalStructuralHP);
	const auto gateHex = battle()->wallPartToBattleHex(EWallPart::GATE);
	ASSERT_TRUE(gateHex.isValid());
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_EQ(friendly->unitSide(), BattleSide::ATTACKER);
	ASSERT_GT(BattleHex::getDistance(gateHex, friendly->getPosition()), 1);
	ASSERT_EQ(battle()->battleHexToWallPart(defender->getPosition()), EWallPart::INVALID);
	ASSERT_EQ(BattleHex::getDistance(gateHex, defender->getPosition()), 1);
	const auto before = structuralHP();
	ASSERT_GE(before.size(), 4u);
	ASSERT_TRUE(before.contains(EWallPart::GATE));
	const auto gateBefore = before.at(EWallPart::GATE);
	const auto unitBefore = defender->getAvailableHealth();
	beginScenario();

	ASSERT_TRUE(castAtHex(gateHex));
	int impactedSections = 0;
	for(const auto & [part, hpBefore] : before)
	{
		const auto partHex = battle()->wallPartToBattleHex(part);
		const auto damage = hpBefore - battle()->getWallStructuralHP(part);
		if(battle()->isWallPartAttackable(part) && BattleHex::getDistance(gateHex, partHex) <= 1)
		{
			++impactedSections;
			EXPECT_EQ(damage, meteorStructuralDamageAtZeroPower)
				<< "Meteor Shower applies the configured 50% fortification fraction to its raw 110 damage output";
		}
		else
			EXPECT_EQ(damage, 0) << "Unimpacted fort part " << static_cast<int>(part);
	}
	EXPECT_GT(impactedSections, 0);
	EXPECT_EQ(gateBefore - battle()->getWallStructuralHP(EWallPart::GATE), meteorStructuralDamageAtZeroPower);
	EXPECT_EQ(unitBefore - defender->getAvailableHealth(), meteorUnitDamageAtZeroPower)
		<< "Fortification damage is separate from creature damage";
}

TEST_F(NewHorizonsHavocStructuresTest, ArmageddonDamagesEveryFortificationAndBothSidesWhileRemovingOrdinaryObstacle)
{
	prepare(armageddonKey, true);
	auto ordinary = addPhysicalObstacle(210, BattleHex(8, 5));
	ASSERT_NE(ordinary, nullptr);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(hostile, nullptr);
	const auto before = structuralHP();
	ASSERT_GE(before.size(), 4u);
	const auto friendlyHealth = friendly->getAvailableHealth();
	const auto hostileHealth = hostile->getAvailableHealth();
	beginScenario();

	ASSERT_TRUE(castGlobally());
	EXPECT_FALSE(containsObstacle(battle()->obstacles, ordinary->uniqueID));
	EXPECT_EQ(friendlyHealth - friendly->getAvailableHealth(), armageddonUnitDamageAtZeroPower);
	EXPECT_EQ(hostileHealth - hostile->getAvailableHealth(), armageddonUnitDamageAtZeroPower);
	for(const auto & [part, hpBefore] : before)
		EXPECT_EQ(hpBefore - battle()->getWallStructuralHP(part),
			std::min(hpBefore, armageddonStructuralDamageAtZeroPower)) << "Fort part " << static_cast<int>(part);
}

TEST_F(NewHorizonsHavocStructuresTest, SavedV3WithoutStructuresRetainsArmageddonLegacyStructureBehavior)
{
	prepare(armageddonKey, true, true);
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	ASSERT_FALSE(battle()->getMagicRules()["spells"][std::string(meteorShowerKey)].Struct().contains("structures"));
	ASSERT_FALSE(battle()->getMagicRules()["spells"][std::string(armageddonKey)].Struct().contains("structures"));
	ASSERT_TRUE(battle()->hasFortifications());
	ASSERT_TRUE(battle()->si.canonicalStructuralHP);
	const auto before = structuralHP();
	ASSERT_GE(before.size(), 4u);
	auto ordinary = addPhysicalObstacle(220, BattleHex(8, 5));
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(hostile, nullptr);
	const auto friendlyHealth = friendly->getAvailableHealth();
	const auto hostileHealth = hostile->getAvailableHealth();
	beginScenario();

	ASSERT_TRUE(castGlobally());
	EXPECT_TRUE(containsObstacle(battle()->obstacles, ordinary->uniqueID));
	EXPECT_EQ(friendlyHealth - friendly->getAvailableHealth(), armageddonUnitDamageAtZeroPower)
		<< "A missing v3 `structures` block must not disable the legacy global creature-damage cast";
	EXPECT_EQ(hostileHealth - hostile->getAvailableHealth(), armageddonUnitDamageAtZeroPower);
	for(const auto & [part, hpBefore] : before)
		EXPECT_EQ(battle()->getWallStructuralHP(part), hpBefore) << "Fort part " << static_cast<int>(part);
}

TEST_F(NewHorizonsHavocStructuresTest, DetachedMeteorPredictionMatchesObstacleAndUnitResolutionWithoutMutatingLiveBattle)
{
	prepare(meteorShowerKey);
	auto ordinary = addPhysicalObstacle(230, BattleHex(9, 5));
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 4), 1000);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(hostile, nullptr);
	beginScenario();

	const auto liveFriendlyHealth = friendly->getAvailableHealth();
	const auto liveHostileHealth = hostile->getAvailableHealth();
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), attackerSideHero->getOwner());
	HavocPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto * projectedHostile = projected.battleGetUnitByID(hostile->unitId());
	ASSERT_NE(projectedHostile, nullptr);
	const auto projectedHostileHealth = projectedHostile->getAvailableHealth();

	spells::BattleCast castEvent(&projected, attackerSideHero, spells::Mode::HERO, activeSpell);
	const auto mechanics = activeSpell->battleMechanics(&castEvent);
	spells::detail::ProblemImpl problem;
	const spells::Target aim{spells::Destination(meteorCenter)};
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(aim, problem));
	mechanics->castEval(projected.getServerCallback(), aim);

	const auto projectedObstacles = projected.battleGetAllObstacles();
	EXPECT_FALSE(containsObstacle(projectedObstacles, ordinary->uniqueID));
	EXPECT_EQ(projectedHostileHealth - projected.battleGetUnitByID(hostile->unitId())->getAvailableHealth(),
		meteorUnitDamageAtZeroPower);
	EXPECT_EQ(friendly->getAvailableHealth(), liveFriendlyHealth);
	EXPECT_EQ(hostile->getAvailableHealth(), liveHostileHealth);
	EXPECT_TRUE(containsObstacle(battle()->obstacles, ordinary->uniqueID));

	ASSERT_TRUE(castAtHex(meteorCenter));
	EXPECT_EQ(liveHostileHealth - hostile->getAvailableHealth(),
		projectedHostileHealth - projected.battleGetUnitByID(hostile->unitId())->getAvailableHealth());
	EXPECT_FALSE(containsObstacle(battle()->obstacles, ordinary->uniqueID));
}
