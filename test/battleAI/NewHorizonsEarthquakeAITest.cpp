/*
 * NewHorizonsEarthquakeAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/Problem.h"

#include <vcmi/Environment.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view earthquakeKey = "core:earthquake";
constexpr std::string_view natureMagicKey = "new-horizons:natureMagic";
constexpr std::string_view rootcallerPerkKey = "new-horizons:natureMagic.rootcaller";
constexpr std::string_view geomancerPerkKey = "new-horizons:natureMagic.geomancer";

enum class MagicRulesProfile
{
	CURRENT_V3,
	CURRENT_V3_WITHOUT_EARTHQUAKE_ROW,
	LEGACY_V2
};

SpellID earthquakeSpell()
{
	return SpellID(SpellID::decode(std::string(earthquakeKey)));
}

JsonNode currentV3MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::CURRENT_RULESET_VERSION;
	newHorizonsMagic::validateRules(rules);
	return rules;
}

JsonNode legacyV2MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("earthquake");
		spell.Struct().erase("selectedPlacement");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

JsonNode magicRulesForProfile(MagicRulesProfile profile)
{
	if(profile == MagicRulesProfile::LEGACY_V2)
		return legacyV2MagicRules();

	auto rules = currentV3MagicRules();
	if(profile == MagicRulesProfile::CURRENT_V3_WITHOUT_EARTHQUAKE_ROW)
	{
		rules["spells"][std::string(earthquakeKey)].Struct().erase("earthquake");
		newHorizonsMagic::validateRules(rules);
	}
	return rules;
}

bool activateGeomancerRow(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(natureMagicKey)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == geomancerPerkKey;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class EarthquakeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit EarthquakeEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class EarthquakeCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	EarthquakeCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::set<uint32_t> affectedUnitIds(const spells::Mechanics & mechanics, const spells::Target & target)
{
	std::set<uint32_t> result;
	for(const auto * unit : mechanics.getAffectedStacks(target))
		if(unit)
			result.insert(unit->unitId());
	return result;
}

using StructuralHP = std::map<EWallPart, int32_t>;

StructuralHP structuralHP(const CBattleInfoCallback & battle)
{
	StructuralHP result;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		result.emplace(part, battle.getWallStructuralHP(part));
	}
	return result;
}

float projectedStructuralFraction(const Environment * environment,
	std::shared_ptr<CBattleInfoCallback> battleState,
	const CGHeroInstance * hero,
	const CSpell * spell,
	const spells::Target & target)
{
	HypotheticBattle projected(environment, battleState);
	spells::BattleCast cast(&projected, hero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	mechanics->castEval(projected.getServerCallback(), target);

	float result = 0.0f;
	for(const auto & [part, before] : structuralHP(*battleState))
	{
		const auto after = projected.getWallStructuralHP(part);
		if(before > after && before > 0)
			result += static_cast<float>(before - after) / static_cast<float>(before);
	}
	return result;
}

bool canCastAtWithDiagnostics(const spells::Mechanics & mechanics,
	const spells::Target & target,
	std::string_view casterName)
{
	spells::detail::ProblemImpl problem;
	const bool legal = mechanics.canBeCastAt(target, problem);
	std::vector<std::string> messages;
	problem.getAll(messages);
	EXPECT_TRUE(legal) << casterName << " Earthquake target preflight failed: "
		<< testing::PrintToString(messages);
	return legal;
}
}

class NewHorizonsEarthquakeAITest : public HeroCommandFixture
{
protected:
	MagicRulesProfile magicRulesProfile = MagicRulesProfile::CURRENT_V3;
	bool allowGeomancer = false;
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<EarthquakeEnvironment> environment;
	std::shared_ptr<EarthquakeCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			magicRulesForProfile(magicRulesProfile));
		if(allowGeomancer)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
			JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
			if(!activateGeomancerRow(perks))
				throw std::runtime_error("Missing Geomancer from the New Horizons perk registry");
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
		}
	}

	void prepareHero(const int spellPower = 0)
	{
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		ASSERT_NE(earthquakeSpell(), SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(earthquakeSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
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

	void addMinimalRoster()
	{
		removeDeployedUnits();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}

	void prepareField()
	{
		useCommands = false;
		startGame();
		prepareHero();
		startBattle();
		addMinimalRoster();
	}

	void prepareSiege(const int spellPower = 0)
	{
		// Geomancer exercises the expanded hero rules, which require the
		// corresponding New Horizons combat-command rules to be present too.
		useCommands = allowGeomancer;
		startGame(true);
		prepareHero(spellPower);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
		startBattle(towns.front());
		addMinimalRoster();
	}

	void createAICallbacks()
	{
		environment = std::make_shared<EarthquakeEnvironment>(gameState());
		callback = std::make_shared<EarthquakeCallback>();
		callback->onBattleStarted(battle());
	}

	void advanceToAttackerAction()
	{
		for(int attempt = 0; attempt < 100; ++attempt)
		{
			const auto * activeUnit = battle()->battleActiveUnit();
			ASSERT_NE(activeUnit, nullptr);
			const auto owner = battle()->battleGetOwner(activeUnit);
			if(owner == PlayerColor(0))
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), owner,
				BattleAction::makeDefend(activeUnit)));
		}
		FAIL() << "No legal attacker-side siege activation became available";
	}

	std::shared_ptr<CBattleInfoCallback> battleCallback(const PlayerColor player = PlayerColor(0)) const
	{
		return std::make_shared<CPlayerBattleCallback>(battle(), player);
	}

	BattleInfo * battle() const
	{
		return HeroCommandFixture::battle();
	}
};

TEST_F(NewHorizonsEarthquakeAITest, FieldTargetsRetainEqualAndEmptyVictimFootprintsAndPhysicalDistance)
{
	ASSERT_NO_FATAL_FAILURE(prepareField());
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	ASSERT_TRUE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), earthquakeSpell()));
	const auto * spell = earthquakeSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_EQ(mechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::LOCATION}));

	std::set<int> expectedLegalCenters;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isValid())
			continue;
		const spells::Target target{spells::Destination(hex)};
		if(mechanics->canBeCastAt(target))
			expectedLegalCenters.insert(hex.toInt());
	}

	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_LE(targets.size(), static_cast<size_t>(GameConstants::BFIELD_SIZE));
	std::map<int, std::set<uint32_t>> footprintsByCenter;
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 1u);
		ASSERT_EQ(target.front().unitValue, nullptr);
		ASSERT_TRUE(target.front().hexValue.isValid());
		ASSERT_TRUE(mechanics->canBeCastAt(target));
		EXPECT_TRUE(footprintsByCenter.emplace(target.front().hexValue.toInt(),
			affectedUnitIds(*mechanics, target)).second) << "Each legal center should occur once";
	}
	std::set<int> actualCenters;
	for(const auto & [center, affected] : footprintsByCenter)
	{
		(void)affected;
		actualCenters.insert(center);
	}
	EXPECT_EQ(actualCenters, expectedLegalCenters)
		<< "Earthquake candidates should preserve every legal, geometrically distinct center";

	bool foundEqualNonEmptyFootprints = false;
	bool foundEqualEmptyFootprints = false;
	for(auto first = footprintsByCenter.begin(); first != footprintsByCenter.end(); ++first)
		for(auto second = std::next(first); second != footprintsByCenter.end(); ++second)
		{
			if(first->second != second->second)
				continue;
			if(first->second.empty())
				foundEqualEmptyFootprints = true;
			else
				foundEqualNonEmptyFootprints = true;
		}
	EXPECT_TRUE(foundEqualEmptyFootprints)
		<< "Empty unit footprints still differ in the terrain geometry they create";
	EXPECT_TRUE(foundEqualNonEmptyFootprints)
		<< "Equal victims do not imply equal terrain geometry";

	const BattleHex start(1, 1);
	const BattleHex firstStep(2, 1);
	const BattleHex secondStep(3, 1);
	const BattleHex destination(4, 1);
	ReachabilityInfo walking;
	walking.params.startPosition = start;
	walking.distances[start.toInt()] = 0;
	walking.distances[firstStep.toInt()] = 2;
	walking.distances[secondStep.toInt()] = 6;
	walking.distances[destination.toInt()] = 10;
	walking.predecessors[firstStep.toInt()] = start;
	walking.predecessors[secondStep.toInt()] = firstStep;
	walking.predecessors[destination.toInt()] = secondStep;
	EXPECT_EQ(SpellTargetEvaluator::physicalTravelDistance(walking, destination), 3);
	EXPECT_EQ(walking.distances[destination.toInt()], 10u)
		<< "The predecessor path measures travel; the reachability array retains weighted cost";

	ReachabilityInfo flying;
	flying.params.startPosition = start;
	flying.params.flying = true;
	flying.distances[destination.toInt()] = 99;
	flying.predecessors[destination.toInt()] = start;
	EXPECT_EQ(SpellTargetEvaluator::physicalTravelDistance(flying, destination),
		BattleHex::getDistance(start, destination));
}

TEST_F(NewHorizonsEarthquakeAITest, SiegeProjectionUsesActualHPAndSignsItWithoutMutatingLiveBattle)
{
	allowGeomancer = true;
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	ASSERT_TRUE(battle()->hasFortifications());
	ASSERT_TRUE(battle()->si.canonicalStructuralHP);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(earthquakeSpell()));
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->removeAllSpells();
	defenderSideHero->addSpellToSpellbook(earthquakeSpell());
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_TRUE(defenderSideHero->hasSpellbook());
	ASSERT_TRUE(defenderSideHero->isSpellInscribedForCasting(earthquakeSpell()));
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	const auto * spell = earthquakeSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	const auto defenderSpellCost = battle()->battleGetSpellCost(spell, defenderSideHero);
	ASSERT_GT(defenderSpellCost, 0);
	ASSERT_GE(defenderSideHero->getManaAvailable(), defenderSpellCost);
	const auto targetHex = battle()->wallPartToBattleHex(EWallPart::GATE);
	ASSERT_TRUE(targetHex.isValid());
	ASSERT_TRUE(battle()->isWallPartAttackable(EWallPart::GATE));
	spells::BattleCast enumerationCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto enumerationMechanics = spell->battleMechanics(&enumerationCast);
	std::set<int> expectedSections;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isValid())
			continue;
		spells::detail::ProblemImpl problem;
		if(enumerationMechanics->canBeCastAt(spells::Target{spells::Destination(hex)}, problem))
			expectedSections.insert(hex.toInt());
	}
	const auto enumeratedSections = SpellTargetEvaluator::canonicalEarthquakeTargets(enumerationMechanics.get());
	ASSERT_LE(enumeratedSections.size(), static_cast<size_t>(GameConstants::BFIELD_SIZE));
	std::set<int> actualSections;
	for(const auto & section : enumeratedSections)
	{
		ASSERT_EQ(section.size(), 1u);
		actualSections.insert(section.front().hexValue.toInt());
	}
	EXPECT_EQ(actualSections.size(), enumeratedSections.size())
		<< "A fortification section should not be emitted more than once";
	EXPECT_EQ(actualSections, expectedSections)
		<< "Every legal selected fortification section is a distinct canonical siege candidate";
	const spells::Target target{spells::Destination(targetHex)};
	const auto attackerState = battleCallback();
	const auto defenderState = battleCallback(PlayerColor(1));
	const auto beforeHP = structuralHP(*attackerState);
	const auto attackerMana = attackerSideHero->getManaAvailable();
	const auto defenderMana = defenderSideHero->getManaAvailable();
	createAICallbacks();

	spells::BattleCast attackerCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto attackerMechanics = spell->battleMechanics(&attackerCast);
	ASSERT_TRUE(canCastAtWithDiagnostics(*attackerMechanics, target, "Attacker"));
	const auto attackerValue = SpellTargetEvaluator::earthquakeStructuralHPValue(
		attackerMechanics.get(), target, environment.get(), attackerState);
	ASSERT_TRUE(attackerValue.has_value());
	EXPECT_GT(*attackerValue, 0.0f);
	EXPECT_NEAR(*attackerValue, projectedStructuralFraction(
		environment.get(), attackerState, attackerSideHero, spell, target), 0.0001f);

	spells::BattleCast defenderCast(battle(), defenderSideHero, spells::Mode::HERO, spell);
	const auto defenderMechanics = spell->battleMechanics(&defenderCast);
	ASSERT_TRUE(canCastAtWithDiagnostics(*defenderMechanics, target, "Defender"));
	HypotheticBattle detachedDefender(environment.get(), defenderState);
	EXPECT_EQ(detachedDefender.battleGetMySide(), BattleSide::DEFENDER);
	spells::BattleCast detachedDefenderCast(&detachedDefender, defenderSideHero,
		spells::Mode::HERO, spell);
	const auto detachedDefenderMechanics = spell->battleMechanics(&detachedDefenderCast);
	EXPECT_EQ(detachedDefenderMechanics->getCasterSide(), BattleSide::DEFENDER);
	ASSERT_TRUE(canCastAtWithDiagnostics(*detachedDefenderMechanics, target, "Detached defender"));
	const auto defenderValue = SpellTargetEvaluator::earthquakeStructuralHPValue(
		defenderMechanics.get(), target, environment.get(), defenderState);
	ASSERT_TRUE(defenderValue.has_value());
	EXPECT_LT(*defenderValue, 0.0f);
	EXPECT_NEAR(*attackerValue, -*defenderValue, 0.0001f);
	EXPECT_NEAR(-*defenderValue, projectedStructuralFraction(
		environment.get(), defenderState, defenderSideHero, spell, target), 0.0001f);

	const auto nature = SecondarySkill(SecondarySkill::decode(std::string(natureMagicKey)));
	ASSERT_TRUE(nature.hasValue());
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(natureMagicKey), std::string(rootcallerPerkKey)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(natureMagicKey), std::string(rootcallerPerkKey)));
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(natureMagicKey), std::string(geomancerPerkKey)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(natureMagicKey), std::string(geomancerPerkKey)));
	spells::BattleCast geomancerCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto geomancerMechanics = spell->battleMechanics(&geomancerCast);
	const auto geomancerValue = SpellTargetEvaluator::earthquakeStructuralHPValue(
		geomancerMechanics.get(), target, environment.get(), attackerState);
	ASSERT_TRUE(geomancerValue.has_value());
	EXPECT_GT(*geomancerValue, *attackerValue)
		<< "The scripted cast projection should include Geomancer's 25% structural damage bonus";

	EXPECT_EQ(structuralHP(*attackerState), beforeHP)
		<< "All target/value forecasts run against a detached battle";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), attackerMana);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), defenderMana);
}

TEST_F(NewHorizonsEarthquakeAITest, BattleAISelectsAndProcessorAcceptsPaidInscribedSiegeCast)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	ASSERT_TRUE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), earthquakeSpell()));
	ASSERT_TRUE(attackerSideHero->hasSpellbook());
	ASSERT_TRUE(attackerSideHero->spellbookContainsSpell(earthquakeSpell()));
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(earthquakeSpell()));
	const auto inscribed = attackerSideHero->getInscribedSpellsForCasting();
	ASSERT_EQ(inscribed.size(), 1u);
	EXPECT_EQ(*inscribed.begin(), earthquakeSpell());
	ASSERT_EQ(battle()->battleGetAllUnits(false).size(), 2u)
		<< "Only the active army stack and one target stack remain in the scenario";

	advanceToAttackerAction();
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	createAICallbacks();
	const auto beforeHP = structuralHP(*battleCallback());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(earthquakeSpell().toSpell(), attackerSideHero);
	ASSERT_GT(manaBefore, cost);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, earthquakeSpell());
	const auto selected = action.getTarget(battle());
	ASSERT_EQ(selected.size(), 1u);
	ASSERT_EQ(selected.front().unitValue, nullptr);
	ASSERT_TRUE(selected.front().hexValue.isValid());
	const auto selectedPart = battle()->battleHexToWallPart(selected.front().hexValue);
	ASSERT_TRUE(battle()->isWallPartAttackable(selectedPart));
	spells::BattleCast targetCheck(battle(), attackerSideHero, spells::Mode::HERO, earthquakeSpell().toSpell());
	const auto targetMechanics = earthquakeSpell().toSpell()->battleMechanics(&targetCheck);
	EXPECT_TRUE(targetMechanics->canBeCastAt(selected));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore)
		<< "The AI projection must not spend Mana before server validation";
	EXPECT_EQ(structuralHP(*battleCallback()), beforeHP)
		<< "The detached AI projection must not damage authoritative fortifications";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - cost);
	EXPECT_LT(battle()->getWallStructuralHP(selectedPart), beforeHP.at(selectedPart));
	EXPECT_GT(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsEarthquakeAITest, SavedV3WithoutEarthquakeRowRetainsLegacySiegeTarget)
{
	magicRulesProfile = MagicRulesProfile::CURRENT_V3_WITHOUT_EARTHQUAKE_ROW;
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	EXPECT_FALSE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), earthquakeSpell()));
	const auto * spell = earthquakeSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->usesNewHorizonsEarthquake());
	EXPECT_EQ(mechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::NOTHING}));
	EXPECT_TRUE(mechanics->canBeCastAt(spells::Target{}));
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	EXPECT_TRUE(targets.front().empty());
	EXPECT_TRUE(SpellTargetEvaluator::canonicalEarthquakeTargets(mechanics.get()).empty());
	createAICallbacks();
	EXPECT_FALSE(SpellTargetEvaluator::earthquakeStructuralHPValue(
		mechanics.get(), {}, environment.get(), battleCallback()).has_value());
}

TEST_F(NewHorizonsEarthquakeAITest, SavedV2EarthquakeRetainsLegacySiegeTarget)
{
	magicRulesProfile = MagicRulesProfile::LEGACY_V2;
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	EXPECT_FALSE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), earthquakeSpell()));
	const auto * spell = earthquakeSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->usesNewHorizonsEarthquake());
	EXPECT_EQ(mechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::NOTHING}));
	EXPECT_TRUE(mechanics->canBeCastAt(spells::Target{}));
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	EXPECT_TRUE(targets.front().empty());
	EXPECT_TRUE(SpellTargetEvaluator::canonicalEarthquakeTargets(mechanics.get()).empty());
	createAICallbacks();
	EXPECT_FALSE(SpellTargetEvaluator::earthquakeStructuralHPValue(
		mechanics.get(), {}, environment.get(), battleCallback()).has_value());
}
