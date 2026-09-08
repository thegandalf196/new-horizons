/*
 * NewHorizonsManagedMissileAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/Destination.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <cstdlib>

namespace
{
class ManagedAiEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ManagedAiEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ManagedAiCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	ManagedAiCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};

class ManagedAiBaseline
{
	std::unique_ptr<GameSettings> prior;
public:
	explicit ManagedAiBaseline(const JsonNode & rules)
	{
		auto all = LIBRARY->settingsHandler->getFullConfig();
		all["magic"]["newHorizons"] = rules;
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(all);
		prior = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}
	~ManagedAiBaseline() { LIBRARY->settingsHandler = std::move(prior); }
};
}

// Future profile-only tests. Not registered in the current baseline build.
class NewHorizonsManagedMissileAITest : public HeroCommandFixture
{
protected:
	SpellID missile;
	JsonNode rules;
	std::unique_ptr<ManagedAiBaseline> baseline;
	std::shared_ptr<ManagedAiEnvironment> environment;
	std::shared_ptr<ManagedAiCallback> callback;
	const CStack * active = nullptr;
	const CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		const auto * sentinel = std::getenv("NH_REQUIRE_MANAGED_MISSILE_PROFILE");
		ASSERT_NE(sentinel, nullptr);
		ASSERT_EQ(std::string(sentinel), "1");
		missile = SpellID(SpellID::decode(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
		ASSERT_NE(missile, SpellID(SpellID::NONE));
		ASSERT_NE(missile, SpellID(SpellID::MAGIC_ARROW));
		ASSERT_EQ(missile.toSpell()->getJsonKey(), GameConstants::NEW_HORIZONS_MAGIC_MISSILE);
	}

	void TearDown() override
	{
		callback.reset();
		environment.reset();
		HeroCommandFixture::TearDown();
		baseline.reset();
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}

	void prepare(bool admit)
	{
		rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_EQ(rules["rulesetVersion"].Integer(), 1);
		ASSERT_EQ(rules["spells"].Struct().size(), 69u);
		ASSERT_FALSE(rules["spells"].Struct().contains(GameConstants::NEW_HORIZONS_MAGIC_MISSILE));
		if(admit)
		{
			rules["rulesetVersion"].Integer() = 2;
			auto & row = rules["spells"][GameConstants::NEW_HORIZONS_MAGIC_MISSILE];
			row["schools"].Vector().push_back(JsonNode("new-horizons:sorcery"));
			row["level"].Integer() = 1;
			for(int rank = 0; rank < 4; ++rank)
				row["costs"].Vector().push_back(JsonNode(5));
			row["directDamage"]["base"].Integer() = 20;
			row["directDamage"]["powerCoefficient"].Integer() = 20;
		}
		newHorizonsMagic::validateRules(rules);
		baseline = std::make_unique<ManagedAiBaseline>(rules); // Not a partial override over70.
		startGame();
		ASSERT_EQ(gameState()->getMagicRules(), rules);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(missile); // Also probes possession bypass in old69.
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 9900, ChangeValueMode::ABSOLUTE);
		attackerSideHero->mana = 1000;
		startBattle();
		beginCombat();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<ManagedAiCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ManagedAiEnvironment>(gameState());
	}

	bool choose()
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		return evaluator.canCastSpell() && evaluator.attemptCastingSpell(active);
	}
};

TEST_F(NewHorizonsManagedMissileAITest, ActualEvaluatorSelectsManagedIdentityAndServerAcceptsWithExactCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(missile.toSpell()), 10);
	ASSERT_EQ(attackerSideHero->getEffectPower(missile.toSpell()), 9900);
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, missile.toSpell());
	ASSERT_EQ(missile.toSpell()->battleMechanics(&probe)->getEffectValue(), 19820);
	ASSERT_LT(19820, enemy->getAvailableHealth());
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto health = enemy->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, missile);
	const auto target = action.getTarget(battle());
	ASSERT_FALSE(target.empty());
	// AI may legally submit a hex rather than an explicit unit ID. Resolve
	// occupancy for the oracle only; execute the original recorded action below.
	const auto * selectedUnit = target.front().unitValue != nullptr
		? target.front().unitValue
		: battle()->battleGetUnitByPos(target.front().hexValue);
	ASSERT_NE(selectedUnit, nullptr);
	ASSERT_EQ(selectedUnit->unitId(), enemy->unitId());
	EXPECT_EQ(enemy->getAvailableHealth(), health) << "AI selection must not apply its prediction to real units";
	EXPECT_EQ(attackerSideHero->mana, mana);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(enemy->getAvailableHealth(), health - 19820);
	EXPECT_EQ(attackerSideHero->mana, mana - 5);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}

TEST_F(NewHorizonsManagedMissileAITest, ActualEvaluatorCannotSelectExcludedManagedSpellDespiteStoredBookEntry)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_TRUE(attackerSideHero->getSpellsInSpellbook().count(missile));
	ASSERT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), missile));
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	ASSERT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	const auto mana = attackerSideHero->mana;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->submitted.front()));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}
