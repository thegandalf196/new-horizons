/*
 * NewHorizonsHavocStructuresAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license is available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/ObstacleHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../server/battles/BattleProcessor.h"
#include "../SpellPointTestUtils.h"

#include <vcmi/Environment.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view meteorShowerKey = "core:meteorShower";
constexpr std::string_view armageddonKey = "core:armageddon";
	const BattleHex meteorCenter(8, 5);

SpellID spellNamed(std::string_view key)
{
	return SpellID(SpellID::decode(std::string(key)));
}

JsonNode currentV3MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::CURRENT_RULESET_VERSION;
	newHorizonsMagic::validateRules(rules);
	return rules;
}

int obstacleId(std::string_view key)
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

class HavocAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HavocAIEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class HavocAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	HavocAICallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::set<EWallPart> wallPartsInArea(const spells::Mechanics & mechanics,
	const spells::Target & target,
	const CBattleInfoCallback & battle)
{
	std::set<EWallPart> result;
	if(mechanics.isMassive())
	{
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			if(battle.isWallPartAttackable(part) && battle.getWallStructuralHP(part) > 0
				&& battle.wallPartToBattleHex(part).isValid())
				result.insert(part);
		}
		return result;
	}

	if(target.size() != 1 || !target.front().hexValue.isValid())
		return result;
	const auto area = mechanics.rangeInHexes(target.front().hexValue);
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		const auto wallHex = battle.wallPartToBattleHex(part);
		if(battle.isWallPartAttackable(part) && battle.getWallStructuralHP(part) > 0
			&& wallHex.isValid() && area.contains(wallHex))
			result.insert(part);
	}
	return result;
}

float expectedStructuralFraction(const spells::Mechanics & mechanics,
	const spells::Target & target,
	const CBattleInfoCallback & battle)
{
	const auto damage = mechanics.getNewHorizonsHavocStructuralDamage();
	float result = 0.0f;
	for(const auto part : wallPartsInArea(mechanics, target, battle))
	{
		const auto currentHP = battle.getWallStructuralHP(part);
		if(currentHP > 0)
			result += static_cast<float>(std::min(currentHP, damage)) / static_cast<float>(currentHP);
	}
	return mechanics.getCasterSide() == BattleSide::ATTACKER ? result : -result;
}

bool obstacleIntersectsArea(const spells::Mechanics & mechanics,
	const spells::Target & target,
	const CObstacleInstance & obstacle)
{
	if(target.size() != 1 || !target.front().hexValue.isValid())
		return false;
	const auto area = mechanics.rangeInHexes(target.front().hexValue);
	for(const auto & hex : obstacle.getAffectedTiles())
		if(area.contains(hex))
			return true;
	return false;
}
}

class NewHorizonsHavocStructuresAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	bool neutralizeCommandUtility = false;

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
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, currentV3MagicRules());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(neutralizeCommandUtility)
		{
			const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
			JsonNode commandRules = combatRules["combat"]["heroCommands"];
			for(auto & commandEntry : commandRules["commands"].Struct())
				for(auto & effectEntry : commandEntry.second["effects"].Struct())
				{
					auto & formula = effectEntry.second;
					formula["base"].Float() = 0;
					formula["attack"].Float() = 0;
					formula["defense"].Float() = 0;
				}
			heroCommands::validateRules(commandRules);
			map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
		}
	}

	void prepare(const bool siege)
	{
		useCommands = true;
		startGame(siege);
		for(const auto hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->removeAllSpells();
			hero->addSpellToSpellbook(spellNamed(meteorShowerKey));
			hero->addSpellToSpellbook(spellNamed(armageddonKey));
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 1000);
		}

		if(siege)
		{
			const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
			ASSERT_EQ(towns.size(), 1u);
			ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
			startBattle(towns.front());
		}
		else
			startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void addArmies(const BattleHex attackerPosition, const BattleHex defenderPosition,
		const int32_t count = 100)
	{
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), attackerPosition, count);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), defenderPosition, count);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
	}

	void beginScenario(const bool waitForAttacker)
	{
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		if(!waitForAttacker)
			return;

		for(int index = 0; index < 100; ++index)
		{
			const auto * unit = battle()->battleActiveUnit();
			ASSERT_NE(unit, nullptr);
			const auto owner = battle()->battleGetOwner(unit);
			if(owner == PlayerColor(0))
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), owner,
				BattleAction::makeDefend(unit)));
		}
		FAIL() << "No attacker-side action became available";
	}

	BattleInfo * battle() const
	{
		return HeroCommandFixture::battle();
	}
};

TEST_F(NewHorizonsHavocStructuresAITest, SharedOutputAndSignedFortValueMatchTheDamageProfileWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(addArmies(BattleHex(4, 5), BattleHex(12, 5)));
	ASSERT_NO_FATAL_FAILURE(beginScenario(true));
	ASSERT_TRUE(battle()->hasFortifications());

	const std::map<SpellID, int32_t> structuralPercents{
		{spellNamed(meteorShowerKey), 50},
		{spellNamed(armageddonKey), 100}};
	const auto before = [&]()
	{
		std::map<EWallPart, int32_t> result;
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			result.emplace(part, battle()->getWallStructuralHP(part));
		}
		return result;
	}();

	for(const auto & [spellId, percent] : structuralPercents)
	{
		const auto * spell = spellId.toSpell();
		ASSERT_NE(spell, nullptr);
		spells::BattleCast attackerCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto attackerMechanics = spell->battleMechanics(&attackerCast);
		ASSERT_TRUE(attackerMechanics->usesNewHorizonsHavocStructures());
		const auto expectedDamage = static_cast<int32_t>(attackerMechanics->getEffectValue() * percent / 100);
		EXPECT_EQ(attackerMechanics->getNewHorizonsHavocStructuralDamage(), expectedDamage);

		spells::Target target;
		if(attackerMechanics->isMassive())
			target.emplace_back(BattleHex::INVALID);
		else
		{
			for(const auto & candidate : SpellTargetEvaluator::canonicalHavocStructureTargets(attackerMechanics.get()))
				if(!wallPartsInArea(*attackerMechanics, candidate, *battle()).empty())
				{
					target = candidate;
					break;
				}
		}
		ASSERT_FALSE(target.empty());
		const auto expectedAttackerValue = expectedStructuralFraction(*attackerMechanics, target, *battle());
		const auto attackerValue = SpellTargetEvaluator::havocStructuralHPValue(attackerMechanics.get(), target);
		ASSERT_TRUE(attackerValue.has_value());
		EXPECT_GT(*attackerValue, 0.0f);
		EXPECT_NEAR(*attackerValue, expectedAttackerValue, 0.0001f);

		spells::BattleCast defenderCast(battle(), defenderSideHero, spells::Mode::HERO, spell);
		const auto defenderMechanics = spell->battleMechanics(&defenderCast);
		ASSERT_TRUE(defenderMechanics->usesNewHorizonsHavocStructures());
		const auto defenderValue = SpellTargetEvaluator::havocStructuralHPValue(defenderMechanics.get(), target);
		ASSERT_TRUE(defenderValue.has_value());
		EXPECT_LT(*defenderValue, 0.0f);
		EXPECT_NEAR(*attackerValue, -*defenderValue, 0.0001f);
	}

	std::map<EWallPart, int32_t> after;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		after.emplace(part, battle()->getWallStructuralHP(part));
	}
	EXPECT_EQ(after, before) << "Structural value forecasts must not mutate the live battle";
}

TEST_F(NewHorizonsHavocStructuresAITest, MeteorRetainsEmptyAndImmuneObstacleFootprints)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const int obstacleType = obstacleId("core:0");
	ASSERT_GE(obstacleType, 0);
	auto addObstacle = [&](const int32_t id, const BattleHex & position)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = id;
		obstacle->ID = obstacleType;
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::USUAL;
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	};
	auto emptyObstacle = addObstacle(230, BattleHex(4, 0));
	auto immuneObstacle = addObstacle(231, BattleHex(9, 5));
	ASSERT_NE(emptyObstacle, nullptr);
	ASSERT_NE(immuneObstacle, nullptr);
	ASSERT_NO_FATAL_FAILURE(addArmies(BattleHex(2, 5), BattleHex(8, 4)));
	ASSERT_NO_FATAL_FAILURE(beginScenario(true));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(spellNamed(meteorShowerKey))));
	ASSERT_TRUE(enemy->hasImmunity(spellNamed(meteorShowerKey)));

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spellNamed(meteorShowerKey).toSpell());
	const auto mechanics = spellNamed(meteorShowerKey).toSpell()->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->usesNewHorizonsHavocStructures());
	const auto targets = SpellTargetEvaluator::canonicalHavocStructureTargets(mechanics.get());
	ASSERT_FALSE(targets.empty());

	bool retainedEmptyFootprint = false;
	bool retainedImmuneFootprint = false;
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 1u);
		ASSERT_TRUE(mechanics->canBeCastAt(target));
		const auto affected = mechanics->getAffectedStacks(target);
		if(affected.empty() && obstacleIntersectsArea(*mechanics, target, *emptyObstacle))
			retainedEmptyFootprint = true;
		const auto impactHexes = mechanics->rangeInHexes(target.front().hexValue);
		const auto enemyHexes = enemy->getHexes();
		const bool immuneEnemyGeometryIntersects = std::any_of(enemyHexes.begin(), enemyHexes.end(),
			[&](const BattleHex & hex)
			{
				return hex.isValid() && impactHexes.contains(hex);
			});
		const bool immuneEnemyExcluded = std::none_of(affected.begin(), affected.end(),
			[&](const CStack * unit)
			{
				return unit && unit->unitId() == enemy->unitId();
			});
		if(obstacleIntersectsArea(*mechanics, target, *immuneObstacle)
			&& immuneEnemyGeometryIntersects && immuneEnemyExcluded)
			retainedImmuneFootprint = true;
	}
	EXPECT_TRUE(retainedEmptyFootprint)
		<< "Ordinary obstacle geometry should preserve a legal center with no unit victims";
	EXPECT_TRUE(retainedImmuneFootprint)
		<< "A center with an immune victim still needs to preserve its distinct obstacle footprint";
}

TEST_F(NewHorizonsHavocStructuresAITest, BattleAISelectsAndServerAcceptsPaidMeteorFortCast)
{
	neutralizeCommandUtility = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(addArmies(BattleHex(4, 5), BattleHex(12, 5)));
	ASSERT_NO_FATAL_FAILURE(beginScenario(true));
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));

	auto environment = std::make_shared<HavocAIEnvironment>(gameState());
	auto callback = std::make_shared<HavocAICallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, spellNamed(meteorShowerKey));
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	ASSERT_TRUE(target.front().hexValue.isValid());

	const auto spell = spellNamed(meteorShowerKey);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero);
	ASSERT_GT(manaBefore, cost);
	std::map<EWallPart, int32_t> structuralHPBefore;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		structuralHPBefore.emplace(part, battle()->getWallStructuralHP(part));
	}

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - cost);
	bool damagedFortPart = false;
	for(const auto & [part, hpBefore] : structuralHPBefore)
		damagedFortPart |= battle()->getWallStructuralHP(part) < hpBefore;
	EXPECT_TRUE(damagedFortPart) << "A paid AI Meteor target should damage a fortification in its impact area";
}
