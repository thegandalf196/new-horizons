/*
 * NewHorizonsCommandEfficiencyAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "PurifyRuleFixtures.h"
#include "HeroActionSelectionObserver.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../lib/spells/BattleSpellMechanics.h"

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
		const auto current = JsonNode(JsonPath::builtin("config/newHorizonsCombat"))["combat"]["heroCommands"];
		auto rules = purifyTest::neutralSpellCompetitionCommands();
		rules["commands"][heroCommands::key(selectedOrder)] = current["commands"][heroCommands::key(selectedOrder)];
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
		if(order == HeroCommand::SECOND_WIND)
		{
			// Isolate the immediate extra shot from movement/exchange risk.
			// The spent ranged recipient retains its ordinary movement and shots.
			for(auto * unit : {active, enemy})
			{
				const auto initiative = unit->getInitiative();
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
					BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER,
					-static_cast<int32_t>(unit->getMovementRange()), BonusSourceID()));
				ASSERT_EQ(unit->getMovementRange(), 0u);
				EXPECT_EQ(unit->getInitiative(), initiative);
			}
		}

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
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero), 23);
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

	ASSERT_EQ(active->getMovementRange(), 0u);
	ASSERT_EQ(enemy->getMovementRange(), 0u);
	ASSERT_GT(windTarget->getMovementRange(), 0u);
	ASSERT_TRUE(windTarget->moved());
	ASSERT_TRUE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::SECOND_WIND, {windTarget->unitId()}));
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast arrowCast(battle(), attackerSideHero, spells::Mode::HERO, arrow);
	const auto arrowMechanics = arrow->battleMechanics(&arrowCast);
	ASSERT_TRUE(arrowMechanics->canBeCastAt({spells::Destination(enemy)}));
	// Current Arrow has base20; School scaling multiplies its zero SP term.
	// The ordinary zero-Overcharge cast therefore retains exactly the base.
	const auto & arrowFormula = battle()->getMagicRules()["spells"]["core:magicArrow"]["directDamage"];
	ASSERT_EQ(arrowFormula["base"].Integer(), 20);
	ASSERT_EQ(arrowFormula["powerCoefficient"].Integer(), 20);
	ASSERT_EQ(arrowMechanics->getEffectPower(), 0);
	ASSERT_EQ(arrowMechanics->getOvercharge(), 0);
	constexpr int64_t expectedArrowDamage = 20;
	ASSERT_EQ(arrowMechanics->adjustEffectValue(enemy), expectedArrowDamage);
	const int leadershipEfficiency = 120 + 25;
	const int expectedPercent = std::clamp(50 + static_cast<int>(std::lround(
		0.015 * static_cast<double>(capacity->capacity) * leadershipEfficiency / 100.0)), 0, 100);
	ASSERT_GT(static_cast<int64_t>(std::floor(static_cast<double>(before.damage.min)
		* expectedPercent / 100.0)), expectedArrowDamage);

	class Observer final : public heroActionSelection::Observer
	{
	public:
		heroSelectionTest::Observer maximum;
		std::optional<float> secondWind;
		void candidate(heroActionSelection::CandidateValue value) noexcept override
		{
			maximum.candidate(value);
			if(value.command == HeroCommand::SECOND_WIND && (!secondWind || *secondWind < value.value))
				secondWind = value.value;
		}
		void selected(heroActionSelection::SelectionValue value) noexcept override { maximum.selected(value); }
	} observer;
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active, true, &observer));
	SCOPED_TRACE("SecondWind=" + ::testing::PrintToString(observer.secondWind)
		+ " best=" + std::to_string(observer.maximum.best.value)
		+ " command=" + std::to_string(static_cast<int>(observer.maximum.best.command))
		+ " spell=" + std::to_string(observer.maximum.best.spell.getNum()));
	ASSERT_TRUE(observer.secondWind);
	ASSERT_GT(*observer.secondWind, 0.0f);
	ASSERT_TRUE(observer.maximum.hasMagicArrow);
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_NO_FATAL_FAILURE(observer.maximum.expectPerformedMatching(action));
	ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(action.command, HeroCommand::SECOND_WIND);
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	PossibleSpellcast candidate;
	candidate.command = HeroCommand::SECOND_WIND;
	candidate.commandTargets = {windTarget->unitId()};
	const auto admission = heroActionProjection::admitCandidate(projected, BattleSide::ATTACKER, candidate);
	ASSERT_TRUE(admission.admitted);
	ASSERT_TRUE(admission.orderEffects);
	ASSERT_TRUE(admission.orderAllowance);
	ASSERT_TRUE(projected.projectAcceptedHeroOrder(BattleSide::ATTACKER,
		*admission.orderEffects, *admission.orderAllowance));
	const auto continuation = projected.projectOrderContinuation(BattleSide::ATTACKER,
		*admission.orderAllowance, *admission.orderEffects);
	ASSERT_EQ(continuation.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::SECOND_WIND_ACTOR);
	ASSERT_EQ(continuation.actor, windTarget->unitId());
	const auto projectedDamage = projected.battleEstimateDamage(BattleAttackInfo(
		projected.battleGetUnitByID(windTarget->unitId()), projected.battleGetUnitByID(enemy->unitId()), 0, true));
	execute(action);
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->command, HeroCommand::SECOND_WIND);
	EXPECT_TRUE(order->secondWindActive);
	EXPECT_EQ(order->primaryTargetUnitId, windTarget->unitId());

	EXPECT_EQ(heroCommands::secondWindPercent(*attackerSideHero), expectedPercent);
	const auto after = battle()->battleEstimateDamage(attack);
	EXPECT_EQ(after.attackerOrderCause, HeroCommand::SECOND_WIND);
	EXPECT_EQ(after.damage.min, projectedDamage.damage.min);
	EXPECT_EQ(after.damage.max, projectedDamage.damage.max);
	EXPECT_EQ(battle()->getActiveStackID(), windTarget->unitId());
	const auto expectedMinimum = static_cast<int64_t>(std::floor(
		static_cast<double>(before.damage.min) * expectedPercent / 100.0));
	EXPECT_LE(std::abs(after.damage.min - expectedMinimum), 1);
}
