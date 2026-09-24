/*
 * HeroCommandAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/CSpell.h"
#include <limits>

namespace
{
class CommandEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CommandEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingCommandCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	RecordingCommandCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class HeroCommandAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	const CStack * enemy = nullptr;
	std::shared_ptr<RecordingCommandCallback> callback;
	std::shared_ptr<CommandEnvironment> environment;

	void prepareEvaluation(bool book)
	{
		prepareCommands(book);
		active = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<RecordingCommandCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CommandEnvironment>(gameState());
	}

	bool choose()
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		return evaluator.canCastSpell() && evaluator.attemptCastingSpell(active);
	}

	void executeChosen()
	{
		ASSERT_EQ(callback->submitted.size(), 1u);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			callback->submitted.front()));
	}
};

/// Isolate one canonical Order while still running the real evaluator and
/// authoritative action path.  The ordinary rules remain canonical (all eight
/// commands are present), but every non-selected command has a zero-valued
/// effect so it cannot win the evaluator's contextual heuristic.
class CanonicalOrderAITest : public HeroCommandAITest
{
protected:
	HeroCommand selectedCommand = HeroCommand::NONE;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(selectedCommand == HeroCommand::NONE)
			return;

		const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
		auto rules = file["combat"]["heroCommands"];
		const auto commands = {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
			HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::BRACE,
			HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND};
		for(const auto command : commands)
		{
			auto & effects = rules["commands"][heroCommands::key(command)]["effects"];
			for(auto & [name, formula] : effects.Struct())
			{
				(void)name;
				formula["base"].Integer() = 0;
				formula["attack"].Float() = 0;
				formula["defense"].Float() = 0;
			}
		}

		// Keep a modest positive coefficient for the selected Order.  Second Wind
		// has a fixed direct-damage heuristic, but retaining a positive authored
		// formula keeps this fixture valid for every canonical command uniformly.
		auto & selectedEffects = rules["commands"][heroCommands::key(selectedCommand)]["effects"];
		for(auto & [name, formula] : selectedEffects.Struct())
		{
			(void)name;
			formula["base"].Integer() = 50;
		}
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	void prepareOrder(HeroCommand command)
	{
		selectedCommand = command;
		prepareEvaluation(false);
	}

	void assertChosenOrder(HeroCommand command)
	{
		ASSERT_TRUE(choose());
		ASSERT_EQ(callback->submitted.size(), 1u);
		EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
		EXPECT_EQ(callback->submitted.front().command, command);
		executeChosen();
		const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
		ASSERT_TRUE(state);
		EXPECT_EQ(state->command, command);
	}
};

TEST_F(HeroCommandAITest, BooklessHeroChoosesBeneficialCommandAndServerAccepts)
{
	prepareEvaluation(false);
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, callback->submitted.front().command));
	executeChosen();
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(choose());
	EXPECT_EQ(callback->submitted.size(), 1u);
}

TEST_F(HeroCommandAITest, StrongOffensiveSpellCompetesWithOrdersAndServerAccepts)
{
	prepareEvaluation(true);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 99, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	ASSERT_EQ(attackerSideHero->getEffectPower(spell), 99);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(), spells::Mode::HERO, attackerSideHero));
	const auto divisor = attackerSideHero->getEffectPowerDivisor(spell);
	ASSERT_GT(divisor, 0);
	RecordProperty("raw99_power", attackerSideHero->getEffectPower(spell));
	RecordProperty("raw99_divisor", divisor);
	RecordProperty("raw99_effect_level", attackerSideHero->getEffectLevel(spell));
	RecordProperty("raw99_damage", std::to_string(spell->calculateDamage(attackerSideHero)));
	// Preserve both 100-Angel armies and the original strength oracle. The
	// fixture means 99 legacy power units, not raw rating99 in every saved scale.
	const auto rating = int64_t{99} * divisor;
	ASSERT_LE(rating, std::numeric_limits<int32_t>::max());
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, static_cast<int32_t>(rating), ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getEffectPower(spell), rating) << "Detect a primary-rating clamp, do not weaken the strength assertion";
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), divisor);
	ASSERT_EQ(attackerSideHero->getEffectPower(spell) / divisor, 99);
	RecordProperty("effective99_rating", static_cast<int>(rating));
	// Substantial nonlethal direct damage, exceeding half an ordinary melee attack.
	const auto spellDamage = spell->calculateDamage(attackerSideHero);
	const auto melee = battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.max;
	ASSERT_GT(spellDamage, melee / 2);
	ASSERT_LT(spellDamage, enemy->getAvailableHealth());
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_SPELL);
	const auto mana = attackerSideHero->getManaAvailable();
	executeChosen();
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}

TEST_F(CanonicalOrderAITest, EvaluatorChoosesRiposteAndRoundLifecycleIsAuthoritative)
{
	prepareOrder(HeroCommand::RIPOSTE);
	BattleAttackInfo retaliation(active, enemy, 0, false);
	retaliation.retaliation = true;
	const auto before = battle()->calculateDmgRange(retaliation).damage.min;

	assertChosenOrder(HeroCommand::RIPOSTE);
	EXPECT_GT(battle()->calculateDmgRange(retaliation).damage.min, before);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::RIPOSTE);
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
}

TEST_F(CanonicalOrderAITest, EvaluatorChoosesBraceAndAuthoritativeTriggerIsLegal)
{
	prepareOrder(HeroCommand::BRACE);
	assertChosenOrder(HeroCommand::BRACE);
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(enemy, active, 3, false, false));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::BRACE);
}

TEST_F(CanonicalOrderAITest, EvaluatorChoosesProtectWithAnAuthoritativePair)
{
	prepareOrder(HeroCommand::PROTECT);
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(69), 100);
	ASSERT_EQ(BattleHex::getDistance(active->getPosition(), ward->getPosition()), 1);

	assertChosenOrder(HeroCommand::PROTECT);
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	ASSERT_NE(state->primaryTargetUnitId, state->secondaryTargetUnitId);
	const auto * protector = battle()->battleGetUnitByID(state->primaryTargetUnitId);
	const auto * protectedUnit = battle()->battleGetUnitByID(state->secondaryTargetUnitId);
	ASSERT_NE(protector, nullptr);
	ASSERT_NE(protectedUnit, nullptr);
	EXPECT_EQ(BattleHex::getDistance(protector->getPosition(), protectedUnit->getPosition()), 1);
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, protectedUnit, false), protector);
}

TEST_F(CanonicalOrderAITest, EvaluatorChoosesFlankAndAuthoritativeSideBonusIsRecorded)
{
	prepareOrder(HeroCommand::FLANK);
	// Keep the target deterministic and reachable.  The battle fixture starts
	// with one token stack per hero; remove only the defender token so the
	// evaluator cannot tie-break onto a distant default footprint.
	std::vector<uint32_t> defenderTokens;
	for(const auto * stack : battle()->battleGetAllStacks(false))
		if(battle()->battleGetOwner(stack) == PlayerColor(1) && stack != enemy)
			defenderTokens.push_back(stack->unitId());
	for(const auto unitId : defenderTokens)
	{
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		removed.changedStacks.emplace_back(unitId, UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
	}

	assertChosenOrder(HeroCommand::FLANK);
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	const auto * target = battle()->battleGetUnitByID(state->primaryTargetUnitId);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(battle()->battleGetOwner(target), PlayerColor(1));
	EXPECT_EQ(target->unitId(), enemy->unitId());

	const auto healthBefore = enemy->getAvailableHealth();
	ASSERT_TRUE(attack(active, enemy->getPosition()));
	EXPECT_LT(enemy->getAvailableHealth(), healthBefore);
	const auto afterAttack = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(afterAttack);
	const auto * flank = afterAttack->flankFor(enemy->unitId());
	ASSERT_NE(flank, nullptr);
	EXPECT_NE(flank->sideMask, 0);
}

TEST_F(CanonicalOrderAITest, EvaluatorChoosesSecondWindForMovedStackAndActivatesIt)
{
	prepareOrder(HeroCommand::SECOND_WIND);
	const auto issuerId = active->unitId();
	CStack * completed = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(69), 100);
	completed->movedThisRound = true;
	assertChosenOrder(HeroCommand::SECOND_WIND);
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->primaryTargetUnitId, completed->unitId());
	EXPECT_NE(state->primaryTargetUnitId, issuerId);
	EXPECT_TRUE(state->secondWindActive);
	EXPECT_EQ(battle()->getActiveStackID(), completed->unitId());
}
