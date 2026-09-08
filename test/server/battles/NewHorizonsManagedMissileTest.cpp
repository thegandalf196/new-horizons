/*
 * NewHorizonsManagedMissileTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/Destination.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/json/JsonRandom.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Configuration.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/entities/building/TownFortifications.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include <vcmi/Environment.h>
#include <cstdlib>

namespace
{
JsonNode old69()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
}

JsonNode current70()
{
	auto rules = old69();
	rules["rulesetVersion"].Integer() = 2;
	auto & row = rules["spells"][GameConstants::NEW_HORIZONS_MAGIC_MISSILE];
	row["schools"].Vector().push_back(JsonNode("new-horizons:sorcery"));
	row["level"].Integer() = 1;
	for(int rank = 0; rank < 4; ++rank)
		row["costs"].Vector().push_back(JsonNode(5));
	row["directDamage"]["base"].Integer() = 20;
	row["directDamage"]["powerCoefficient"].Integer() = 20;
	return rules;
}

class InstalledMagicBaseline
{
	std::unique_ptr<GameSettings> previous;
public:
	explicit InstalledMagicBaseline(const JsonNode & rules)
	{
		auto config = LIBRARY->settingsHandler->getFullConfig();
		config["magic"]["newHorizons"] = rules; // Replace, do not deep-merge a 69-row override over 70.
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(config);
		previous = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}
	~InstalledMagicBaseline() { LIBRARY->settingsHandler = std::move(previous); }
};

class ManagedWorld final : public CGameInfoCallback
{
	CGameState & state;
	JsonNode rules;
public:
	ManagedWorld(CGameState & state, JsonNode rules) : state(state), rules(std::move(rules)) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const JsonNode & getMagicRules() const override { return rules; }
};

class ManagedArmyRestore
{
	CGHeroInstance & left;
	CGHeroInstance & right;
	BattleInfo * oldLeft;
	BattleInfo * oldRight;
public:
	ManagedArmyRestore(CGHeroInstance & left, CGHeroInstance & right)
		: left(left), right(right), oldLeft(left.battle), oldRight(right.battle) {}
	~ManagedArmyRestore() { left.battle = oldLeft; right.battle = oldRight; }
};

class MissileEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MissileEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

// UNREGISTERED future suite. Build must opt into its actual managed-spell profile;
// a missing sentinel/definition is a failure, never a skipped or substituted test.
class NewHorizonsManagedMissileTest : public HeroCommandFixture
{
protected:
	SpellID missile;
	JsonNode selected;
	bool mandatoryMissile = false;
	std::unique_ptr<InstalledMagicBaseline> baseline;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		const auto * sentinel = std::getenv("NH_REQUIRE_MANAGED_MISSILE_PROFILE");
		ASSERT_NE(sentinel, nullptr) << "Requires Build's isolated managed70 profile";
		ASSERT_EQ(std::string(sentinel), "1");
		missile = SpellID(SpellID::decode(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
		ASSERT_NE(missile, SpellID(SpellID::NONE)) << "No synthetic ID or Arrow substitution";
		ASSERT_NE(missile, SpellID(SpellID::MAGIC_ARROW));
		ASSERT_EQ(missile.toSpell()->getJsonKey(), GameConstants::NEW_HORIZONS_MAGIC_MISSILE);
		ASSERT_TRUE(missile.toSpell()->isCommonHeroSpell());
		const auto old = old69();
		ASSERT_EQ(old["rulesetVersion"].Integer(), 1);
		ASSERT_EQ(old["spells"].Struct().size(), 69u);
		ASSERT_FALSE(old["spells"].Struct().contains(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
		size_t common = 0;
		for(const auto & definition : LIBRARY->spellh->objects)
			if(definition && definition->isCommonHeroSpell())
				++common;
		ASSERT_EQ(common, 70u) << "Exact managed70 test profile required";
	}

	void TearDown() override
	{
		HeroCommandFixture::TearDown();
		baseline.reset();
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, selected);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		if(mandatoryMissile)
		{
			map->allowedSpells.insert(missile);
			map->allowedSpells.erase(SpellID(SpellID::MAGIC_ARROW));
			for(auto * town : map->getObjects<CGTownInstance>())
			{
				town->obligatorySpells = {missile, SpellID(SpellID::MAGIC_ARROW)};
				town->possibleSpells.clear();
			}
		}
	}

	void startWithRules(JsonNode rules)
	{
		selected = std::move(rules);
		newHorizonsMagic::validateRules(selected);
		baseline = std::make_unique<InstalledMagicBaseline>(selected);
		startGame(mandatoryMissile);
		ASSERT_EQ(gameState()->getMagicRules(), selected) << "No inherited seventieth row in an old69 snapshot";
	}
};

TEST_F(NewHorizonsManagedMissileTest, ValidOld69LoadsWithNewInstalledDefinitionButDoesNotAdmitIt)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), missile));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_TRUE(newHorizonsMagic::spellSchools(gameState()->getMagicRules(), missile).empty());
	startBattle();
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), missile));
}

TEST_F(NewHorizonsManagedMissileTest, ActualNullSnapshotKeepsLegacyArrowButExcludesNewManagedMissile)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(JsonNode()));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), missile));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), SpellID(SpellID::MAGIC_ARROW)));
	startBattle();
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), missile));
}

TEST_F(NewHorizonsManagedMissileTest, RegisteredIdentityValidationRequiresVersionFormulaAndNonManagedCoverage)
{
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(old69()));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(current70()));
	auto missingCore = current70();
	missingCore["spells"].Struct().erase("core:magicArrow");
	EXPECT_THROW(newHorizonsMagic::validateRules(missingCore), std::runtime_error);
	auto forbiddenV1 = current70();
	forbiddenV1["rulesetVersion"].Integer() = 1;
	forbiddenV1["spells"][GameConstants::NEW_HORIZONS_MAGIC_MISSILE].Struct().erase("directDamage");
	EXPECT_THROW(newHorizonsMagic::validateRules(forbiddenV1), std::runtime_error);
	auto missingFormula = current70();
	missingFormula["spells"][GameConstants::NEW_HORIZONS_MAGIC_MISSILE].Struct().erase("directDamage");
	EXPECT_THROW(newHorizonsMagic::validateRules(missingFormula), std::runtime_error);
	auto olderV2 = current70();
	olderV2["spells"].Struct().erase(GameConstants::NEW_HORIZONS_MAGIC_MISSILE);
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(olderV2));
}

TEST_F(NewHorizonsManagedMissileTest, ActualLoadFromMemoryRetainsOld69WhileInstalledSettingsBecome70)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	const auto saved = gameState()->saveToMemory();
	InstalledMagicBaseline newer(current70());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	EXPECT_EQ(restored.getMagicRules(), old69());
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(restored, missile));
	CGameState resaved;
	resaved.preInit(LIBRARY);
	resaved.loadFromMemory(restored.saveToMemory());
	EXPECT_EQ(resaved.getMagicRules(), old69());
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(resaved, missile));
}

TEST_F(NewHorizonsManagedMissileTest, ActualLoadFromMemoryRetainsV2FormulaWhileInstalledSettingsBecomeAbsent)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(current70()));
	const auto saved = gameState()->saveToMemory();
	InstalledMagicBaseline absent{JsonNode()};
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	EXPECT_EQ(restored.getMagicRules(), current70());
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(restored, missile));
	EXPECT_EQ(newHorizonsMagic::directDamageValue(restored.getMagicRules(), GameConstants::NEW_HORIZONS_MAGIC_MISSILE, 24, 10), 68);
}

TEST_F(NewHorizonsManagedMissileTest, ExplicitRewardCannotBypassOldRosterOrLeakNoneButLegacyMapBanOverrideRemains)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	gameState()->getMap().allowedSpells.insert(missile);
	GameRandomizer randomizer(*gameState());
	JsonRandom json(gameState().get(), randomizer);
	JsonNode named(GameConstants::NEW_HORIZONS_MAGIC_MISSILE);
	named.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_EQ(json.loadSpell(named, {}), SpellID(SpellID::NONE));
	JsonNode list;
	list.Vector().push_back(named);
	// The same array loader supplies required limiter predicates. Dropping an
	// unavailable member would silently weaken them; reject rather than emit NONE.
	EXPECT_THROW(json.loadSpells(list, {}), std::runtime_error);
	const SpellID arrow(SpellID::MAGIC_ARROW);
	gameState()->getMap().allowedSpells.erase(arrow);
	JsonNode legacyName("core:magicArrow");
	legacyName.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_EQ(json.loadSpell(legacyName, {}), arrow) << "Named scholar/map-ban override remains distinct from roster admission";
	// These are discriminating future assertions: current named-key extraction
	// bypasses the allowed set and needs a separately leased JsonRandom repair.
}

TEST_F(NewHorizonsManagedMissileTest, ExplicitRewardAdmitsActuallyRegisteredMissileInValidV2Roster)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(current70()));
	gameState()->getMap().allowedSpells.insert(missile);
	GameRandomizer randomizer(*gameState());
	JsonRandom json(gameState().get(), randomizer);
	JsonNode named(GameConstants::NEW_HORIZONS_MAGIC_MISSILE);
	named.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_EQ(json.loadSpell(named, {}), missile);
	JsonNode list;
	list.Vector().push_back(named);
	EXPECT_EQ(json.loadSpells(list, {}), std::vector<SpellID>{missile});
}

TEST_F(NewHorizonsManagedMissileTest, Old69MandatoryGuildSkipsMissileBeforeIndexButKeepsLegacyBanOverride)
{
	mandatoryMissile = true;
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	ASSERT_FALSE(gameState()->getMap().getAllTowns().empty());
	for(const auto townId : gameState()->getMap().getAllTowns())
	{
		const auto * town = gameState()->getTown(townId);
		bool foundArrow = false;
		for(const auto & level : town->spells)
		{
			EXPECT_FALSE(vstd::contains(level, missile));
			foundArrow |= vstd::contains(level, SpellID(SpellID::MAGIC_ARROW));
		}
		EXPECT_TRUE(foundArrow);
	}
	// Expected to require the future mandatory-guild pre-index guard; do not
	// execute before that separately authorized source repair/profile is ready.
}

TEST_F(NewHorizonsManagedMissileTest, V2MandatoryGuildAdmitsActuallyRegisteredMissile)
{
	mandatoryMissile = true;
	ASSERT_NO_FATAL_FAILURE(startWithRules(current70()));
	ASSERT_FALSE(gameState()->getMap().getAllTowns().empty());
	for(const auto townId : gameState()->getMap().getAllTowns())
	{
		const auto * town = gameState()->getTown(townId);
		bool found = false;
		for(const auto & level : town->spells)
			found |= vstd::contains(level, missile);
		EXPECT_TRUE(found);
	}
}

TEST_F(NewHorizonsManagedMissileTest, RewardAndLimiterArraysRejectUnavailableMembersWithoutPublishingVisit)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	GameRandomizer randomizer(*gameState());
	for(const bool limiter : {false, true})
		for(const std::string key : (limiter ? std::vector<std::string>{"spells", "scrolls", "canLearnSpells"}
			: std::vector<std::string>{"spells", "scrolls", "takenScrolls"}))
		{
			SCOPED_TRACE(key);
			JsonNode parameters;
			parameters["rewards"].Vector().resize(1);
			auto & entry = parameters["rewards"].Vector().front();
			auto & fields = limiter ? entry["limiter"] : entry;
			fields[key].Vector().push_back(JsonNode(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
			parameters.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
			Rewardable::Info info;
			info.init(parameters, "managedMissileArrayTest");
			Rewardable::Configuration configured;
			EXPECT_THROW(info.configureObject(configured, randomizer, gameState().get()), std::runtime_error);
			EXPECT_TRUE(configured.info.empty());
		}
}

TEST_F(NewHorizonsManagedMissileTest, ExplicitEmptyRewardAndLimiterArraysRemainValid)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	GameRandomizer randomizer(*gameState());
	JsonNode parameters;
	parameters["rewards"].Vector().resize(1);
	auto & entry = parameters["rewards"].Vector().front();
	entry["spells"].Vector();
	entry["limiter"]["canLearnSpells"].Vector();
	Rewardable::Info info;
	info.init(parameters, "managedMissileEmptyArrayTest");
	Rewardable::Configuration configured;
	EXPECT_NO_THROW(info.configureObject(configured, randomizer, gameState().get()));
	ASSERT_EQ(configured.info.size(), 1u);
	EXPECT_TRUE(configured.info.front().reward.spells.empty());
	EXPECT_TRUE(configured.info.front().limiter.canLearnSpells.empty());
}

TEST_F(NewHorizonsManagedMissileTest, UnavailableSpellVariableRejectsBeforePublicationOrText)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	GameRandomizer randomizer(*gameState());
	JsonNode parameters;
	parameters["variables"]["spell"]["chosen"].String() = GameConstants::NEW_HORIZONS_MAGIC_MISSILE;
	parameters["rewards"].Vector().resize(1);
	parameters["rewards"].Vector().front()["message"].Vector().push_back(JsonNode("%s"));
	parameters.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	Rewardable::Info info;
	info.init(parameters, "managedMissileVariableRejectionTest");
	Rewardable::Configuration configured;
	EXPECT_THROW(info.configureObject(configured, randomizer, gameState().get()), std::runtime_error);
	EXPECT_FALSE(configured.getVariable("spell", "chosen").has_value());
	EXPECT_TRUE(configured.info.empty());
}

TEST_F(NewHorizonsManagedMissileTest, ValidSpellVariablePublishesActualIdentityAndResolvesMessage)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(current70()));
	GameRandomizer randomizer(*gameState());
	JsonNode parameters;
	parameters["variables"]["spell"]["chosen"].String() = GameConstants::NEW_HORIZONS_MAGIC_MISSILE;
	parameters["rewards"].Vector().resize(1);
	parameters["rewards"].Vector().front()["message"].Vector().push_back(JsonNode("%s"));
	parameters.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	Rewardable::Info info;
	info.init(parameters, "managedMissileVariablePositiveTest");
	Rewardable::Configuration configured;
	ASSERT_NO_THROW(info.configureObject(configured, randomizer, gameState().get()));
	ASSERT_TRUE(configured.getVariable("spell", "chosen").has_value());
	EXPECT_EQ(*configured.getVariable("spell", "chosen"), missile.getNum());
	ASSERT_EQ(configured.info.size(), 1u);
	EXPECT_EQ(configured.info.front().message.toString(LIBRARY->staticTexts()), missile.toSpell()->getNameTranslated());
}

TEST_F(NewHorizonsManagedMissileTest, InvalidLegacySpellLikeVariableCannotReachNameDereference)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	GameRandomizer randomizer(*gameState());
	JsonNode parameters;
	// Adversarial legacy-shaped variable category retains its default -1. This
	// reaches the text defense, independently of the recognized-spell producer.
	parameters["variables"]["spellLegacy"]["chosen"].String() = "core:magicArrow";
	parameters["rewards"].Vector().resize(1);
	parameters["rewards"].Vector().front()["message"].Vector().push_back(JsonNode("%s"));
	Rewardable::Info info;
	info.init(parameters, "managedMissileLegacyVariableTest");
	Rewardable::Configuration configured;
	bool rejected = false;
	try
	{
		info.configureObject(configured, randomizer, gameState().get());
	}
	catch(const std::runtime_error & error)
	{
		rejected = true;
		EXPECT_EQ(std::string(error.what()), "Unavailable spell variable in reward text: spellLegacy@chosen");
	}
	EXPECT_TRUE(rejected);
	EXPECT_TRUE(configured.info.empty());
}

TEST_F(NewHorizonsManagedMissileTest, ScalarSpellCastAbsenceDoesNotBecomeScrollOrSpellGrant)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	GameRandomizer randomizer(*gameState());
	JsonNode parameters;
	parameters["rewards"].Vector().resize(1);
	parameters["rewards"].Vector().front()["spellCast"]["spell"].String() = GameConstants::NEW_HORIZONS_MAGIC_MISSILE;
	parameters.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	Rewardable::Info info;
	info.init(parameters, "managedMissileScalarAbsenceTest");
	Rewardable::Configuration configured;
	ASSERT_NO_THROW(info.configureObject(configured, randomizer, gameState().get()));
	ASSERT_EQ(configured.info.size(), 1u);
	EXPECT_EQ(configured.info.front().reward.spellCast.first, SpellID(SpellID::NONE));
	EXPECT_TRUE(configured.info.front().reward.spells.empty());
	EXPECT_TRUE(configured.info.front().reward.grantedScrolls.empty());
	// Interface separately guards spellCast.NONE before invoking the cast callback.
}

TEST_F(NewHorizonsManagedMissileTest, ValidOld69IncomingBattleDoesNotAdoptValid70WorldAdmission)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(old69()));
	startBattle();
	ManagedWorld newer(*gameState(), current70());
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(newer.getMagicRules()));
	CMemorySerializer wire;
	wire.oser & *battle();
	wire.iser.cb = &newer;
	{
		ManagedArmyRestore restore(*attackerSideHero, *defenderSideHero);
		BattleInfo incoming(&newer);
		ASSERT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(incoming, missile));
		wire.iser & incoming;
		EXPECT_EQ(incoming.getMagicRules(), old69());
		EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(newer, missile));
		EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(incoming, missile));
		// Admission query only; no uninitialized stack state is inspected here.
	}
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
}

TEST_F(NewHorizonsManagedMissileTest, RealManagedSpellAiPredictionAndAuthoritativeCastChargeExactlyFiveMana)
{
	ASSERT_NO_FATAL_FAILURE(startWithRules(current70()));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(missile);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 100;
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	beginCombat();
	const auto * definition = missile.toSpell();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), missile));
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(definition), 10);
	ASSERT_EQ(attackerSideHero->getEffectPower(definition), 24);
	ASSERT_EQ(attackerSideHero->getSpellCost(definition), 5);
	spells::Target destination;
	destination.emplace_back(target);
	spells::BattleCast legal(battle(), attackerSideHero, spells::Mode::HERO, definition);
	spells::detail::ProblemImpl problem;
	auto mechanics = definition->battleMechanics(&legal);
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	MissileEnvironment environment(gameState());
	HypotheticBattle prediction(&environment, callback);
	spells::BattleCast evaluation(&prediction, attackerSideHero, spells::Mode::HERO, definition);
	evaluation.castEval(prediction.getServerCallback(), destination);
	EXPECT_EQ(health - prediction.battleGetUnitByID(target->unitId())->getAvailableHealth(), 68);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->mana, mana);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = missile;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(health - target->getAvailableHealth(), 68);
	EXPECT_EQ(attackerSideHero->mana, mana - 5);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth(), health - 68);
	EXPECT_EQ(attackerSideHero->mana, mana - 5);
	// Actual AI effect evaluation, not yet BattleAI's candidate selection gate.
}
