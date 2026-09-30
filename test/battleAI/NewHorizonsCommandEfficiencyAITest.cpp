/*
 * NewHorizonsCommandEfficiencyAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../SpellPointTestUtils.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"

#include <cmath>

namespace
{
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char AGGRESSIVE_COMMANDER[] = "new-horizons:command.aggressiveCommander";
constexpr char DEFENSIVE_COMMANDER[] = "new-horizons:command.defensiveCommander";
constexpr char VETERAN_COMMANDER[] = "new-horizons:command.veteranCommander";

class CommandAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CommandAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class CommandAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	CommandAICallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsCommandEfficiencyAITest : public HeroCommandFixture
{
protected:
	HeroCommand selectedOrder = HeroCommand::NONE;
	CStack * active = nullptr;
	CStack * windTarget = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<CommandAIEnvironment> environment;
	std::shared_ptr<CommandAICallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));

		if(selectedOrder == HeroCommand::NONE)
			return;
		// Isolate the shared coefficient consumer for one selected Order. Magic Arrow
		// remains a real competing hero action; this fixture does not claim to
		// certify all-Orders tactical ranking under the canonical rules.
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCombat"))["combat"]["heroCommands"];
		for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
			HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::BRACE,
			HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND})
		{
			if(command == selectedOrder)
				continue;
			for(auto & [name, formula] : rules["commands"][heroCommands::key(command)]["effects"].Struct())
			{
				(void)name;
				formula["base"].Integer() = 0;
				formula["attack"].Float() = 0;
				formula["defense"].Float() = 0;
			}
		}
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	void configureHero(int rank, const char * basicPerk, const char * advancedPerk = nullptr)
	{
		const int command = SecondarySkill::decode(COMMAND_SKILL);
		ASSERT_GE(command, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(command), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({COMMAND_SKILL, basicPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, basicPerk));
		if(advancedPerk)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(command), rank, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({COMMAND_SKILL, advancedPerk});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, advancedPerk));
		}
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(COMMAND_SKILL), rank);
	}

	void prepare(HeroCommand order, const char * basicPerk, int rank,
		const char * advancedPerk = nullptr)
	{
		selectedOrder = order;
		startGame();
		configureHero(rank, basicPerk, advancedPerk);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 50, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);

		if(order == HeroCommand::FOCUS_FIRE)
		{
			active = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 100);
			addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 3), 100);
			addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 7), 100);
			enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 1000);
		}
		else if(order == HeroCommand::HOLD_THE_LINE)
		{
			active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 1000);
			enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(4, 5), 1000);
		}
		else
		{
			active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 7), 1);
			windTarget = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 100);
			enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 1000);
		}
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);

		beginCombat();
		if(windTarget)
			windTarget->movedThisRound = true;
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<CommandAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CommandAIEnvironment>(gameState());
	}

	bool chooseOrder(HeroCommand expected, BattleAction & selected)
	{
		const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		if(!spell)
		{
			ADD_FAILURE() << "Magic Arrow must be registered for the competition fixture";
			return false;
		}
		EXPECT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
			spells::Mode::HERO, attackerSideHero)) << "A real spell remains in the AI's candidate set";

		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		EXPECT_TRUE(evaluator.canCastSpell());
		if(!evaluator.attemptCastingSpell(active))
		{
			ADD_FAILURE() << "The AI did not submit a hero action";
			return false;
		}
		if(callback->submitted.size() != 1u)
		{
			ADD_FAILURE() << "Expected one submitted action, got " << callback->submitted.size();
			return false;
		}
		selected = callback->submitted.front();
		EXPECT_EQ(selected.actionType, EActionType::HERO_COMMAND);
		EXPECT_EQ(selected.command, expected);
		return true;
	}

	void execute(const BattleAction & action)
	{
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}
};

TEST_F(NewHorizonsCommandEfficiencyAITest, AggressiveCommanderAIOrderMatchesFocusFireSnapshotWithSpellAvailable)
{
	prepare(HeroCommand::FOCUS_FIRE, AGGRESSIVE_COMMANDER, MasteryLevel::BASIC);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));
	ASSERT_TRUE(battle()->battleCanShoot(active, enemy->getPosition()));
	const auto & formula = battle()->getHeroCommandRules()["commands"]["focusFire"]["effects"]["rangedDamagePercent"];
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero), 25);
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));

	BattleAction action;
	ASSERT_TRUE(chooseOrder(HeroCommand::FOCUS_FIRE, action));
	execute(action);
	const auto state = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->targetUnitId, enemy->unitId());
	EXPECT_EQ(state->rangedDamagePercent, heroCommands::coefficient(formula, *attackerSideHero));
}

TEST_F(NewHorizonsCommandEfficiencyAITest, DefensiveCommanderAIOrderMatchesHoldTheLineDamageProjectionWithSpellAvailable)
{
	prepare(HeroCommand::HOLD_THE_LINE, DEFENSIVE_COMMANDER, MasteryLevel::BASIC);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, DEFENSIVE_COMMANDER));
	const auto & formula = battle()->getHeroCommandRules()["commands"]["holdTheLine"]["effects"]["damageReductionPercent"];
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero), 23);
	const BattleAttackInfo incoming(enemy, active, 0, false);
	const auto before = battle()->battleEstimateDamage(incoming);
	ASSERT_GT(before.damage.min, 0);

	BattleAction action;
	ASSERT_TRUE(chooseOrder(HeroCommand::HOLD_THE_LINE, action));
	execute(action);
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->command, HeroCommand::HOLD_THE_LINE);
	EXPECT_NE(order->anchorFor(active->unitId()), nullptr);
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero, order->warcastingBonusPercent), 23);
	const auto after = battle()->battleEstimateDamage(incoming);
	EXPECT_EQ(after.defenderOrderCause, HeroCommand::HOLD_THE_LINE);
	EXPECT_LT(after.damage.min, before.damage.min);
	const auto expectedMinimum = static_cast<int64_t>(std::floor(
		static_cast<double>(before.damage.min) * (100 - heroCommands::coefficient(formula, *attackerSideHero)) / 100.0));
	EXPECT_LE(std::abs(after.damage.min - expectedMinimum), 1);
}

TEST_F(NewHorizonsCommandEfficiencyAITest, VeteranCommanderAISecondWindProjectionMatchesReducedActivationWithSpellAvailable)
{
	prepare(HeroCommand::SECOND_WIND, AGGRESSIVE_COMMANDER, MasteryLevel::ADVANCED, VETERAN_COMMANDER);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, VETERAN_COMMANDER));
	const auto capacity = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->capacity, 0);
	ASSERT_NE(windTarget, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(windTarget, enemy->getPosition()));
	const BattleAttackInfo attack(windTarget, enemy, 0, true);
	const auto before = battle()->battleEstimateDamage(attack);
	ASSERT_GT(before.damage.min, 0);

	BattleAction action;
	ASSERT_TRUE(chooseOrder(HeroCommand::SECOND_WIND, action));
	ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(action.command, HeroCommand::SECOND_WIND);
	execute(action);
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->command, HeroCommand::SECOND_WIND);
	EXPECT_TRUE(order->secondWindActive);
	EXPECT_EQ(order->primaryTargetUnitId, windTarget->unitId());

	const int leadershipEfficiency = 120 + 25 + order->warcastingBonusPercent;
	const int expectedPercent = std::clamp(50 + static_cast<int>(std::lround(
		0.015 * static_cast<double>(capacity->capacity) * leadershipEfficiency / 100.0)), 0, 100);
	EXPECT_EQ(heroCommands::secondWindPercent(*attackerSideHero, order->warcastingBonusPercent), expectedPercent);
	const auto after = battle()->battleEstimateDamage(attack);
	EXPECT_EQ(after.attackerOrderCause, HeroCommand::SECOND_WIND);
	const auto expectedMinimum = static_cast<int64_t>(std::floor(
		static_cast<double>(before.damage.min) * expectedPercent / 100.0));
	EXPECT_LE(std::abs(after.damage.min - expectedMinimum), 1);
}
