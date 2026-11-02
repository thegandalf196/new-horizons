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
#include "HeroActionTimingCapture.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/CRandomGenerator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include <limits>
#include <stdexcept>
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "OriginalMeleeSequenceFixture.h"
#include "LegacyOrderOracleChecks.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include <tuple>
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/StartInfo.h"
#include <array>
#include <bit>
#include <sstream>

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
	std::vector<BattleAction> unitActions;
	bool throwOnSubmit = false;

	RecordingCommandCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		unitActions.push_back(action);
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
		if(throwOnSubmit)
			throw std::runtime_error("Hero Action submit diagnostic fixture");
	}
};
}

class HeroCommandAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<RecordingCommandCallback> callback;
	std::shared_ptr<CommandEnvironment> environment;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		// Commands-off captures empty legacy hero development and its matching perk-offer policy.
		if(!useCommands)
			captureLegacyPerkOfferPolicy(*loaded);
	}

	virtual void configureHeroBeforeBattle()
	{
	}

	void prepareEvaluation(bool book)
	{
		startGame();
		if(book)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			setTestSpellPointTotal(attackerSideHero, 100);
		}
		configureHeroBeforeBattle();
		startBattle();
		beginCombat();
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

	auto rngState() const
	{
		CMemorySerializer memory;
		memory.oser & *gameHandler->randomizer;
		return memory.extractBuffer();
	}
	void chooseActive(bool allowSpells = true)
	{
		CBattleAI ai;
		AutocombatPreferences preferences;
		preferences.enableSpellsUsage = allowSpells;
		ai.initBattleInterface(environment, callback, preferences);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), active);
	}

	void characterizeConstrainedFallback(bool stopped, bool spentHeroAction = false)
	{
		useCommands = spentHeroAction;
		ASSERT_NO_FATAL_FAILURE(prepareEvaluation(false));
		if(spentHeroAction)
		{
			ASSERT_TRUE(choose());
			ASSERT_NO_FATAL_FAILURE(executeChosen());
			callback->submitted.clear();
		}
		active->pursuitMovementRemaining = 2;
		if(stopped)
			active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
		ASSERT_EQ(active->isTimeStopped(), stopped);
		for(const auto * unit : battle()->battleGetAllStacks(false))
			(void)unit->counterAttacks.available();
		const auto world = gameState()->saveToMemory();
		const auto serverRng = rngState();
		CMemorySerializer defaultRandom;
		defaultRandom.oser & CRandomGenerator::getDefault();
		const auto defaultRng = defaultRandom.extractBuffer();
		ASSERT_NO_THROW(chooseActive(false));
		EXPECT_TRUE(callback->submitted.empty());
		ASSERT_EQ(callback->unitActions.size(), 1u);
		const auto & action = callback->unitActions.front();
		EXPECT_EQ(action.stackNumber, active->unitId());
		EXPECT_EQ(action.side, active->unitSide());
		if(stopped)
			EXPECT_EQ(action.actionType, EActionType::NO_ACTION);
		else
		{
			EXPECT_EQ(action.actionType, EActionType::WALK);
			ASSERT_EQ(action.getTarget(battle()).size(), 1u);
			const auto path = battle()->getPath(active->getPosition(),
				action.getTarget(battle()).front().hexValue, active);
			EXPECT_GT(path.second, 0);
			EXPECT_LE(path.second, active->pursuitMovementRemaining);
		}
		EXPECT_EQ(gameState()->saveToMemory(), world);
		EXPECT_EQ(rngState(), serverRng);
		CMemorySerializer afterRandom;
		afterRandom.oser & CRandomGenerator::getDefault();
		EXPECT_EQ(afterRandom.extractBuffer(), defaultRng);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}

};

/// Isolate canonical Orders while still running the real evaluator and
/// authoritative action path. The ordinary rules remain canonical (all eight
/// commands are present); tests either zero non-selected effects or explicitly
/// configure the paired Charge/Brace competition.
class CanonicalOrderAITest : public HeroCommandAITest
{
protected:
	HeroCommand selectedCommand = HeroCommand::NONE;
	bool selectEncirclement = false;
	bool configureVengeancePerkData = false;
	bool selectCountercharge = false;
	bool configureCounterchargePerkData = false;
	bool selectIronDiscipline = false;
	bool configureIronDisciplinePerkData = false;
	bool competeBraceAndCharge = false;
	bool zeroRiposteEffects = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(configureVengeancePerkData || configureCounterchargePerkData || configureIronDisciplinePerkData)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(selectedCommand == HeroCommand::NONE)
			return;

		// Retain the historical eight-Order policy selected by HeroCommandFixture.
		const JsonNode file(JsonPath::builtin("config/newHorizonsCombatV3"));
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

		if(competeBraceAndCharge)
		{
			// Preserve the historical base coefficients: Charge 60%, Brace 50%,
			// or 75% with Countercharge. Final choices use common exchange utility,
			// not the ordering of these percentages.
			rules["commands"][heroCommands::key(HeroCommand::CHARGE)]["effects"]
				["meleeDamagePercent"]["base"].Integer() = 60;
			rules["commands"][heroCommands::key(HeroCommand::BRACE)]["effects"]
				["preemptiveDamagePercent"]["base"].Integer() = 50;
		}
		else
		{
			// Keep a modest positive coefficient for the selected Order.  Second
			// Wind has a fixed direct-damage heuristic, but retaining a positive
			// authored formula keeps this fixture valid for every canonical command.
			auto & selectedEffects = rules["commands"][heroCommands::key(selectedCommand)]["effects"];
			for(auto & [name, formula] : selectedEffects.Struct())
			{
				(void)name;
				formula["base"].Integer() = 50;
			}
			if(selectedCommand == HeroCommand::RIPOSTE && zeroRiposteEffects)
			{
				for(auto & [name, formula] : selectedEffects.Struct())
				{
					(void)name;
					formula["base"].Integer() = 0;
					formula["attack"].Float() = 0;
					formula["defense"].Float() = 0;
				}
			}
			if(selectedCommand == HeroCommand::FLANK)
			{
				// Keep the canonical non-perk coefficient so the AI test exercises
				// Encirclement's shared resolver for the 4% -> 7% change.
				selectedEffects["additionalSidePercent"]["base"].Integer() = 4;
				if(selectEncirclement)
					loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
						JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
			}
		}
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	void configureHeroBeforeBattle() override
	{
		if(selectEncirclement)
		{
			const auto offense = SecondarySkill::decode("new-horizons:offense");
			ASSERT_GE(offense, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(offense), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:offense", "new-horizons:offense.encirclement"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:offense", "new-horizons:offense.encirclement"));
		}
		if(selectCountercharge)
		{
			const auto armorer = SecondarySkill::decode("new-horizons:armorer");
			ASSERT_GE(armorer, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(armorer), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.countercharge"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer",
				"new-horizons:armorer.countercharge"));
		}
		if(selectIronDiscipline)
		{
			const auto armorer = SecondarySkill::decode(newHorizonsIronDiscipline::SKILL);
			ASSERT_GE(armorer, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(armorer), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsIronDiscipline::SKILL,
				newHorizonsIronDiscipline::PERK});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
				newHorizonsIronDiscipline::PERK));
		}
	}

	void addVisibleEnemySpellcaster()
	{
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELLCASTER, BonusSource::OTHER, 1, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
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

	void keepOnlyCompetingStacks()
	{
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * stack : battle()->battleGetAllStacks(false))
			if(stack != active && stack != enemy)
				removed.changedStacks.emplace_back(stack->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);
		ASSERT_EQ(battle()->battleGetAllStacks(false).size(), 2u);
	}
};

TEST_F(HeroCommandAITest, BooklessHeroChoosesBeneficialCommandAndServerAccepts)
{
	heroTimingTest::Capture capture;
	prepareEvaluation(false);
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, callback->submitted.front().command));
	capture.expectSubmission(battle()->getRound(), active->unitId(), true);
	executeChosen();
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(choose());
	EXPECT_EQ(callback->submitted.size(), 1u);
}

TEST_F(HeroCommandAITest, NoHeroEarlyReturnAcceptsMissingActiveStack)
{
	heroTimingTest::Capture capture;
	prepareEvaluation(false);
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	const auto originalHero = side.heroID;
	side.heroID = ObjectInstanceID();
	ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetMyHero(), nullptr);
	const auto rng = rngState();
	const auto allowance = side.heroCommandUsed;
	const auto activeHealth = active->getAvailableHealth();
	const auto enemyHealth = enemy->getAvailableHealth();
	EXPECT_FALSE(evaluator.attemptCastingSpell(nullptr));
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_TRUE(capture.rows().empty());
	EXPECT_EQ(rngState(), rng);
	EXPECT_EQ(side.heroCommandUsed, allowance);
	EXPECT_EQ(active->getAvailableHealth(), activeHealth);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(side.heroID, ObjectInstanceID());
	side.heroID = originalHero;
	ASSERT_TRUE(choose());
	capture.expectSubmission(battle()->getRound(), active->unitId(), true);
	const auto restoredRows = capture.records();
	if(!restoredRows.empty())
	{
		EXPECT_EQ(restoredRows.front().at("hero_attempt"), 1);
	}
}

TEST_F(HeroCommandAITest, ThrowingHeroSubmissionRetainsCallRowAndLiveState)
{
	heroTimingTest::Capture capture;
	prepareEvaluation(false);
	callback->throwOnSubmit = true;
	const auto rng = rngState();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto activeHealth = active->getAvailableHealth();
	EXPECT_THROW(choose(), std::runtime_error);
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(active->getAvailableHealth(), activeHealth);
	EXPECT_EQ(rngState(), rng);
	const auto rows = capture.records();
	ASSERT_EQ(rows.size(), heroTimingTest::optedIn() ? 1u : 0u);
	if(!rows.empty())
	{
		EXPECT_EQ(rows.front().at("loop_joined"), 1);
		EXPECT_EQ(rows.front().at("submit_calls"), 1);
		EXPECT_EQ(rows.front().at("submit_returned"), 0);
		EXPECT_EQ(rows.front().at("submit_threw"), 1);
	}
}

TEST_F(HeroCommandAITest, StrongOffensiveSpellCompetesWithOrdersAndServerAccepts)
{
	heroTimingTest::Capture capture;
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
	capture.expectSubmission(battle()->getRound(), active->unitId());
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

TEST_F(CanonicalOrderAITest, VengeanceValuesSpentBeforeIssueRetaliationWithoutMutatingLiveBattle)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::RIPOSTE;
	configureVengeancePerkData = true;
	zeroRiposteEffects = true;
	prepareEvaluation(false);

	active->counterAttacks.use();
	ASSERT_FALSE(active->counterAttacks.canUse());
	const auto baselineTotal = active->counterAttacks.total();
	const auto baselineAvailable = active->counterAttacks.available();
	choose();
	EXPECT_FALSE(std::ranges::any_of(callback->submitted, [](const BattleAction & action)
	{
		return action.actionType == EActionType::HERO_COMMAND && action.command == HeroCommand::RIPOSTE;
	}));
	callback->submitted.clear();

	const int offense = SecondarySkill::decode(newHorizonsOffense::SKILL);
	ASSERT_GE(offense, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(offense), MasteryLevel::ADVANCED,
		ChangeValueMode::ABSOLUTE);
	// A legitimate Basic prefix is required even when the Skill rank was granted directly.
	const auto offenseRank = attackerSideHero->getPerkSkillRank(newHorizonsOffense::SKILL);
	const auto & perks = attackerSideHero->getPerkState();
	const std::string pursuit = "new-horizons:offense.pursuit";
	ASSERT_FALSE(perks.hasSelection(newHorizonsOffense::SKILL, pursuit));
	ASSERT_TRUE(perks.canAcquirePerk(newHorizonsOffense::SKILL, pursuit, offenseRank));
	ASSERT_FALSE(perks.canAcquirePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE, offenseRank));
	attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, pursuit});
	ASSERT_TRUE(perks.hasSelection(newHorizonsOffense::SKILL, pursuit));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, pursuit));
	ASSERT_TRUE(perks.canAcquirePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE, offenseRank));
	// Pursuit grants kill-triggered movement, not retaliation or an immediate activation budget.
	ASSERT_EQ(active->counterAttacks.total(), baselineTotal);
	ASSERT_EQ(active->counterAttacks.available(), baselineAvailable);
	ASSERT_EQ(active->pursuitMovementRemaining, 0);
	attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	choose();
	EXPECT_FALSE(std::ranges::any_of(callback->submitted, [](const BattleAction & action)
	{
		return action.actionType == EActionType::HERO_COMMAND && action.command == HeroCommand::RIPOSTE;
	})) << "Vengeance must not value a counterattack against a retaliation-blocking attacker";
	callback->submitted.clear();
	enemy->removeBonuses(Selector::type()(BonusType::BLOCKS_RETALIATION));

	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(callback->submitted.front().command, HeroCommand::RIPOSTE);
	EXPECT_EQ(active->counterAttacks.total(), baselineTotal);
	EXPECT_EQ(active->counterAttacks.available(), baselineAvailable);
	EXPECT_FALSE(newHorizonsOffense::hasVengeanceRetaliationBonus(active));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);

	executeChosen();
	battle::UnitInfo lateArrival;
	lateArrival.id = battle()->battleNextUnitId();
	lateArrival.count = 3;
	lateArrival.type = creatureByName("core:angel");
	lateArrival.side = BattleSide::ATTACKER;
	lateArrival.position = BattleHex(80);
	lateArrival.summoned = true;
	JsonNode arrivalData;
	lateArrival.save(arrivalData);
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	projected.addUnit(lateArrival.id, arrivalData);
	const auto * projectedArrival = projected.battleGetUnitByID(lateArrival.id);
	ASSERT_NE(projectedArrival, nullptr);
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(projectedArrival));
	EXPECT_EQ(projectedArrival->acquireState()->counterAttacks.total(), 2);
	EXPECT_EQ(battle()->getStack(lateArrival.id, false), nullptr)
		<< "Projecting a summoned unit must not add it to the authoritative battle";
}

TEST_F(CanonicalOrderAITest, ThreeHexBraceHasCommonExchangeValueAndNativeEffect)
{
	prepareOrder(HeroCommand::BRACE);
	keepOnlyCompetingStacks();
	ASSERT_NO_FATAL_FAILURE(legacyOrderTest::brace({*this, environment, callback, active, enemy}));
}

TEST_F(CanonicalOrderAITest, OrdinaryHoldStillValuesPhysicalThreatWithoutIronDiscipline)
{
	prepareOrder(HeroCommand::HOLD_THE_LINE);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
		newHorizonsIronDiscipline::PERK));
	assertChosenOrder(HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::HOLD_THE_LINE);
}

TEST_F(CanonicalOrderAITest, IronDisciplineValuesVisibleCreatureSpellThreatWithoutMutatingBattle)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::HOLD_THE_LINE;
	configureIronDisciplinePerkData = true;
	selectIronDiscipline = true;
	prepareEvaluation(false);
	// Isolate the visible creature-caster branch from the separate public
	// enemy-hero-presence prior exercised below.
	battle()->getSide(BattleSide::DEFENDER).heroID = ObjectInstanceID();
	ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetHeroInfo(BattleSide::DEFENDER).owner,
		PlayerColor::NEUTRAL);
	addVisibleEnemySpellcaster();
	keepOnlyCompetingStacks();
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
		newHorizonsIronDiscipline::PERK));
	EXPECT_FALSE(enemy->isMeleeAttacker())
		<< "The Siege Weapon marker removes this test stack from ordinary Hold melee valuation";
	ASSERT_TRUE(enemy->canCast());
	ASSERT_TRUE(SpellID(SpellID::MAGIC_ARROW).toSpell()->canBeCast(
		battle(), spells::Mode::CREATURE_ACTIVE, enemy));
	ASSERT_GT(SpellID(SpellID::MAGIC_ARROW).toSpell()->calculateDamage(enemy), 0);
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::HOLD_THE_LINE, {});
	ASSERT_TRUE(prepared);
	ASSERT_GT(prepared->holdMagicalReductionBasisPoints, 0);

	const auto initialRound = battle()->battleGetRound();
	const auto initialActive = battle()->getActiveStackID();
	const auto initialMana = attackerSideHero->getManaAvailable();
	const auto initialSpellsCast = battle()->battleCastSpells(BattleSide::ATTACKER);

	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(callback->submitted.front().command, HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(battle()->battleGetRound(), initialRound);
	EXPECT_EQ(battle()->getActiveStackID(), initialActive);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), initialMana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), initialSpellsCast);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
}

TEST_F(CanonicalOrderAITest, IronDisciplineUsesOnlyPublicEnemyHeroPresenceAsSpellThreat)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::HOLD_THE_LINE;
	configureIronDisciplinePerkData = true;
	selectIronDiscipline = true;
	prepareEvaluation(false);
	// Make the only opposing stack unable to create physical melee pressure;
	// no creature SPELLCASTER bonus is added in this case.
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	keepOnlyCompetingStacks();

	const auto aiBattle = callback->getBattle(BattleID(0));
	ASSERT_NE(aiBattle, nullptr);
	const auto enemyHeroInfo = aiBattle->battleGetHeroInfo(BattleSide::DEFENDER);
	ASSERT_NE(enemyHeroInfo.owner, PlayerColor::NEUTRAL);
	EXPECT_FALSE(enemyHeroInfo.details.has_value())
		<< "The enemy hero is concealed at the basic-info level during AI evaluation";
	EXPECT_EQ(aiBattle->battleGetFightingHero(BattleSide::DEFENDER), nullptr)
		<< "The heuristic must not need the concealed hero object";

	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().command, HeroCommand::HOLD_THE_LINE);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
}

TEST_F(CanonicalOrderAITest, IronDisciplineDoesNotAddMagicalThreatValueForNonHolder)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::HOLD_THE_LINE;
	configureIronDisciplinePerkData = true;
	prepareEvaluation(false);
	addVisibleEnemySpellcaster();
	keepOnlyCompetingStacks();
	ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
		newHorizonsIronDiscipline::PERK));
	EXPECT_FALSE(enemy->isMeleeAttacker());
	ASSERT_TRUE(enemy->canCast());
	ASSERT_TRUE(SpellID(SpellID::MAGIC_ARROW).toSpell()->canBeCast(
		battle(), spells::Mode::CREATURE_ACTIVE, enemy));
	ASSERT_GT(SpellID(SpellID::MAGIC_ARROW).toSpell()->calculateDamage(enemy), 0);
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::HOLD_THE_LINE, {});
	ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->holdMagicalReductionBasisPoints, 0);

	EXPECT_FALSE(choose()) << "No physical melee threat exists: a non-holder must not value the magical extension";
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
}

TEST_F(CanonicalOrderAITest, CounterchargeBraceHeuristicAndDamageForecastShareTheSameMultiplier)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::BRACE;
	configureCounterchargePerkData = true;
	selectCountercharge = true;
	prepareEvaluation(false);
	keepOnlyCompetingStacks();
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
	// Preserve this selector for existing focused rosters. Its obsolete adjacent
	// winner premise is replaced by the real qualifying approach, common exchange
	// utility and native two-packet effect; this does not assert an all-action winner.
	ASSERT_NO_FATAL_FAILURE(legacyOrderTest::brace({*this, environment, callback, active, enemy}, 75));
}

TEST_F(CanonicalOrderAITest, AdjacentFlyingChargeCannotInventRunupOrExchangeUtility)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::BRACE;
	configureCounterchargePerkData = true;
	competeBraceAndCharge = true; // Preserve the original 60/50 authored coefficients.
	prepareEvaluation(false);
	keepOnlyCompetingStacks();
	ASSERT_NO_FATAL_FAILURE(legacyOrderTest::adjacentCharge({*this, environment, callback, active, enemy}));
}

TEST_F(CanonicalOrderAITest, CounterchargeImprovesBraceWhileChargeHasHigherExchangeUtility)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::BRACE;
	configureCounterchargePerkData = true;
	competeBraceAndCharge = true;
	prepareEvaluation(false);
	keepOnlyCompetingStacks();
	const auto armorer = SecondarySkill::decode("new-horizons:armorer");
	ASSERT_GE(armorer, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(armorer), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_FALSE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
	// Prove every legal melee landing qualifies, including both footprints.
	legacyOrderTest::Context context{*this, environment, callback, active, enemy};
	ASSERT_NO_FATAL_FAILURE(legacyOrderTest::position(context, active, BattleHex(95)));
	ASSERT_NO_FATAL_FAILURE(legacyOrderTest::position(context, enemy, BattleHex(89)));
	const auto view = callback->getBattle(BattleID(0));
	RecordProperty("ownDoubleWide", active->doubleWide());
	RecordProperty("enemyDoubleWide", enemy->doubleWide());
	for(const auto [actor, victim] : {std::pair{active, enemy}, std::pair{enemy, active}})
	{
		const auto reachability = view->getReachability(actor);
		int minimum = std::numeric_limits<int>::max();
		for(const auto hex : view->battleGetAvailableHexes(reachability, actor, false))
			if(view->isMeleeAttackPossibleWithLongReach(actor, victim, hex))
			{
				const auto distance = SpellTargetEvaluator::physicalTravelDistance(reachability, hex);
				ASSERT_GE(distance, 3) << "Actor " << actor->unitId() << " from " << hex;
				minimum = std::min(minimum, distance);
			}
		ASSERT_NE(minimum, std::numeric_limits<int>::max());
		RecordProperty(actor == active ? "ownMinimumApproach" : "enemyMinimumApproach", minimum);
	}
	struct Values final : heroActionSelection::Observer
	{
		float charge = std::numeric_limits<float>::lowest();
		float brace = std::numeric_limits<float>::lowest();
		float baseline = std::numeric_limits<float>::lowest();
		void candidate(heroActionSelection::CandidateValue value) noexcept override
		{
			if(value.command == HeroCommand::CHARGE) charge = value.value;
			if(value.command == HeroCommand::BRACE) brace = value.value;
		}
		void selected(heroActionSelection::SelectionValue value) noexcept override { baseline = value.baseline; }
	} withoutCountercharge, values;
	const auto evaluate = [&](Values & observed)
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		return evaluator.canCastSpell() && evaluator.attemptCastingSpell(active, true, &observed);
	};
	// RecordingCommandCallback only records the proposed action: neither trial
	// submits to simulation authority until executeChosen below.
	const auto before = legacyOrderTest::snapshot(context, *view);
	const auto capturedRules = attackerSideHero->getPerkState().rules;
	const auto selectedBefore = attackerSideHero->getPerkState().selected.size();
	ASSERT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(50, attackerSideHero), 50);
	ASSERT_TRUE(evaluate(withoutCountercharge));
	ASSERT_NE(withoutCountercharge.charge, std::numeric_limits<float>::lowest());
	ASSERT_NE(withoutCountercharge.brace, std::numeric_limits<float>::lowest());
	ASSERT_NE(withoutCountercharge.baseline, std::numeric_limits<float>::lowest());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(legacyOrderTest::snapshot(context, *view), before);
	RecordProperty("withoutCounterchargeSelectedCommand", static_cast<int>(callback->submitted.front().command));
	callback->submitted.clear();
	attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.countercharge"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
	EXPECT_EQ(attackerSideHero->getPerkState().rules, capturedRules);
	EXPECT_EQ(attackerSideHero->getPerkState().selected.size(), selectedBefore + 1);
	ASSERT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(50, attackerSideHero), 75);
	const bool chosen = evaluate(values);
	ASSERT_NE(values.charge, std::numeric_limits<float>::lowest());
	ASSERT_NE(values.brace, std::numeric_limits<float>::lowest());
	ASSERT_NE(values.baseline, std::numeric_limits<float>::lowest());
	EXPECT_EQ(legacyOrderTest::snapshot(context, *view), before);
	RecordProperty("withoutCounterchargeChargeValue", std::to_string(withoutCountercharge.charge));
	RecordProperty("withoutCounterchargeBraceValue", std::to_string(withoutCountercharge.brace));
	RecordProperty("withoutCounterchargeBaseline", std::to_string(withoutCountercharge.baseline));
	EXPECT_GT(values.brace, withoutCountercharge.brace);
	EXPECT_FLOAT_EQ(values.charge, withoutCountercharge.charge);
	EXPECT_FLOAT_EQ(values.baseline, withoutCountercharge.baseline);
	EXPECT_GT(values.charge, values.brace);
	EXPECT_GT(values.charge, values.baseline);
	RecordProperty("chargeFinalValue", std::to_string(values.charge));
	RecordProperty("braceFinalValue", std::to_string(values.brace));
	RecordProperty("noHeroActionBaseline", std::to_string(values.baseline));
	ASSERT_TRUE(chosen);
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(callback->submitted.front().command, HeroCommand::CHARGE);
	executeChosen();
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->command, HeroCommand::CHARGE);
}

TEST_F(CanonicalOrderAITest, FocusFireSubmitRouteKeepsNativeTargetAndReceipt)
{
	heroTimingTest::Capture capture;
	prepareOrder(HeroCommand::FOCUS_FIRE);
	const auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(104), 100);
	ASSERT_TRUE(shooter->canShoot());
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
	ASSERT_EQ(action.command, HeroCommand::FOCUS_FIRE);
	ASSERT_EQ(action.target.size(), 1u);
	const auto targetId = action.target.front().unitValue;
	ASSERT_GE(targetId, 0);
	ASSERT_TRUE(callback->getBattle(BattleID(0))->battleCanConfirmHeroCommand(
		BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, static_cast<uint32_t>(targetId)));
	capture.expectSubmission(battle()->getRound(), active->unitId(), true);
	executeChosen();
	const auto mark = battle()->getFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	EXPECT_EQ(mark->targetUnitId, targetId);
	EXPECT_TRUE(vstd::contains(mark->recipientUnitIds, shooter->unitId()));
}

TEST_F(CanonicalOrderAITest, EvaluatorChoosesProtectWithAnAuthoritativePair)
{
	heroTimingTest::Capture capture;
	prepareOrder(HeroCommand::PROTECT);
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(69), 100);
	ASSERT_EQ(BattleHex::getDistance(active->getPosition(), ward->getPosition()), 1);

	assertChosenOrder(HeroCommand::PROTECT);
	capture.expectSubmission(battle()->getRound(), active->unitId(), true);
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

TEST_F(CanonicalOrderAITest, FlankHeuristicPrefersTargetWithMultipleCurrentContactSides)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectEncirclement = true;
	prepareOrder(HeroCommand::FLANK);
	ASSERT_EQ(battle()->battleHeroOrderFlankAdditionalSidePercent(BattleSide::ATTACKER), 7);
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
	auto * secondAttacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(54), 100);
	ASSERT_NE(secondAttacker, nullptr);

	std::optional<BattleHex> remoteTargetPosition;
	const auto allUnits = battle()->battleGetAllUnits(false);
	for(int index = 0; index < GameConstants::BFIELD_SIZE && !remoteTargetPosition; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || battle()->battleGetUnitByPos(candidate))
			continue;
		bool adjacentToReadyAlly = false;
		for(const auto * unit : allUnits)
		{
			if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
				|| battle()->battleGetOwner(unit) != PlayerColor(0)
				|| !unit->isMeleeAttacker() || !unit->willMove(0))
				continue;
			for(const auto & occupied : unit->getHexes())
				if(occupied.isValid() && BattleHex::getDistance(occupied, candidate) == 1)
					adjacentToReadyAlly = true;
		}
		if(!adjacentToReadyAlly)
			remoteTargetPosition = candidate;
	}
	ASSERT_TRUE(remoteTargetPosition);
	auto * remoteTarget = addStack(BattleSide::DEFENDER, creatureByName("angel"), *remoteTargetPosition, 100);
	ASSERT_NE(remoteTarget, nullptr);
	remoteTarget->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, -1,
		BonusSourceID(SpellID(SpellID::BLESS)), BonusSubtypeID(PrimarySkill::DEFENSE)));

	uint8_t contactedSides = battle()->battleHeroOrderFlankSide(active, enemy);
	contactedSides |= battle()->battleHeroOrderFlankSide(secondAttacker, enemy);
	const auto countSides = [](uint8_t mask)
	{
		int count = 0;
		for(auto bits = mask; bits; bits &= static_cast<uint8_t>(bits - 1))
			++count;
		return count;
	};
	EXPECT_GE(countSides(contactedSides), 2);
	uint8_t remoteSides = battle()->battleHeroOrderFlankSide(active, remoteTarget);
	remoteSides |= battle()->battleHeroOrderFlankSide(secondAttacker, remoteTarget);
	EXPECT_EQ(remoteSides, 0);
	const auto enemyBaseDamage = battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.min;
	const auto remoteBaseDamage = battle()->calculateDmgRange(BattleAttackInfo(active, remoteTarget, 0, false)).damage.min;
	ASSERT_GT(remoteBaseDamage, enemyBaseDamage)
		<< "Without the additional-side opportunity, the slightly lower-Defense remote target should win";

	assertChosenOrder(HeroCommand::FLANK);
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->primaryTargetUnitId, enemy->unitId())
		<< "The AI should value Flank's bonus for the extra distinct contact side";
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

class CanonicalOrderFutureActorTest : public OriginalMeleeSequenceTest
{
protected:
	struct Forecast
	{
		int64_t incomingHealth = 0;
		int64_t bracedHealth = 0;
		float utilityForBracedSide = 0;
		int braceReceipts = 0;
	};
	void mapLoaded(CMap * loaded) override
	{
		OriginalMeleeSequenceTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCombat"))["combat"]["heroCommands"];
		for(const auto & [key, effect, percent] : {
			std::tuple{"charge", "meleeDamagePercent", 60},
			std::tuple{"brace", "preemptiveDamagePercent", 50}})
		{
			auto & formula = rules["commands"][key]["effects"][effect];
			formula["base"].Integer() = percent;
			formula["attack"].Float() = formula["defense"].Float() = 0;
		}
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}
	auto liveHeroState() const
	{
		return std::tuple{
			projectedHitTest::serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER)),
			projectedHitTest::serialized(battle()->getHeroOrderStates(BattleSide::DEFENDER)),
			projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)),
			projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::DEFENDER))};
	}
	void forecast(bool selectedPreview, bool brace, Forecast & result)
	{
		auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), player);
		// This is the same genuine future-actor activation used by calculateExchange,
		// not the already-active first chosen AP's activation-skipping path.
		branch->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		auto actor = branch->getForUpdate(incoming->unitId());
		auto victim = branch->getForUpdate(braced->unitId());
		ASSERT_EQ(branch->getActiveStackID(), static_cast<int32_t>(incoming->unitId()));
		const auto order = branch->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::BRACE);
		ASSERT_EQ(order.has_value(), brace);
		if(brace)
		{
			ASSERT_TRUE(order->scheduledFor(victim->unitId(), branch->battleGetRound()));
		}
		ASSERT_EQ(branch->battleCanTriggerHeroOrderBrace(actor.get(), victim.get(), 3, false, false), brace);
		ASSERT_EQ(actor->getPosition(), BattleHex(89));
		ASSERT_EQ(victim->getPosition(), BattleHex(93));
		const auto reachability = branch->getReachability(actor.get());
		ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(reachability, BattleHex(92)), 3);
		ASSERT_TRUE(branch->battleGetAvailableHexes(reachability, actor.get(), false).contains(BattleHex(92)));
		ASSERT_FALSE(std::ranges::any_of(branch->battleAliveUnits(), [&](const auto * unit)
		{
			return branch->battleOverwatchReady(unit);
		}));
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		BattleExchangeVariant exchange;
		if(selectedPreview)
		{
			const auto preview = AttackPossibility::evaluate(
				BattleAttackInfo(actor.get(), victim.get(), 3, false), BattleHex(92), cache, branch);
			result.braceReceipts = static_cast<int>(std::ranges::count(preview.fortuneStrikes, true,
				&FortuneStrikeProjection::bracePreemptive));
			if(brace)
			{
				ASSERT_FALSE(preview.fortuneStrikes.empty());
				const auto & first = preview.fortuneStrikes.front();
				EXPECT_TRUE(first.bracePreemptive);
				EXPECT_FALSE(first.retaliation);
				EXPECT_EQ(first.attackerId, braced->unitId());
				EXPECT_EQ(first.defenderId, incoming->unitId());
			}
			exchange.trackAttack(preview, branch, cache);
		}
		else
			exchange.trackAttack(actor, victim, false, false, cache, branch, false, true);
		result.incomingHealth = branch->battleGetUnitByID(incoming->unitId())->getAvailableHealth();
		result.bracedHealth = branch->battleGetUnitByID(braced->unitId())->getAvailableHealth();
		auto score = exchange.getScore();
		// The AP overload scores from its actor's perspective; the future overload
		// received isOurAttack=false. Report the SAME braced-side sign in both.
		result.utilityForBracedSide = selectedPreview ? -score.value() : score.value();
	}
	void assertNativeSequence(bool brace)
	{
		ASSERT_EQ(server.attacks.size(), brace ? 2u : 1u);
		const auto & first = server.attacks.front();
		EXPECT_EQ(first.stackAttacking, brace ? braced->unitId() : incoming->unitId());
		EXPECT_FALSE(first.counter());
		ASSERT_EQ(first.bsa.size(), 1u);
		EXPECT_EQ(first.bsa.front().stackAttacked, brace ? incoming->unitId() : braced->unitId());
		if(brace)
		{
			const auto & second = server.attacks.at(1);
			EXPECT_EQ(second.stackAttacking, incoming->unitId());
			EXPECT_FALSE(second.counter());
			ASSERT_EQ(second.bsa.size(), 1u);
			EXPECT_EQ(second.bsa.front().stackAttacked, braced->unitId());
		}
	}
	void compareFutureWithSelectedAndNative(bool brace)
	{
		ASSERT_NO_FATAL_FAILURE(prepareSequence(brace, false));
		releaseKnown();
		const auto incomingBefore = incoming->acquireState()->save();
		const auto bracedBefore = braced->acquireState()->save();
		const auto gameRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto heroesBefore = liveHeroState();
		Forecast selected, future;
		ASSERT_NO_FATAL_FAILURE(forecast(true, brace, selected));
		EXPECT_EQ(liveHeroState(), heroesBefore);
		ASSERT_NO_FATAL_FAILURE(forecast(false, brace, future));
		EXPECT_EQ(liveHeroState(), heroesBefore);
		EXPECT_EQ(selected.braceReceipts, brace ? 1 : 0);
		EXPECT_EQ(incoming->acquireState()->save(), incomingBefore);
		EXPECT_EQ(braced->acquireState()->save(), bracedBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), gameRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
		EXPECT_EQ(future.incomingHealth, selected.incomingHealth)
			<< "real three-hex future approach must not discard the implemented Brace stage; utility future="
			<< future.utilityForBracedSide << " selected=" << selected.utilityForBracedSide;
		EXPECT_EQ(future.bracedHealth, selected.bracedHealth);
		ASSERT_GT(selected.incomingHealth, 0);
		ASSERT_GT(selected.bracedHealth, 0);
		// forecast() retired every projected provider/actor before native execution.
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
		EXPECT_EQ(incoming->getAvailableHealth(), selected.incomingHealth);
		EXPECT_EQ(braced->getAvailableHealth(), selected.bracedHealth);
		ASSERT_NO_FATAL_FAILURE(assertNativeSequence(brace));
	}
};

TEST_F(CanonicalOrderFutureActorTest, ActualThreeHexFutureApproachPreservesBracePrimary)
{
	ASSERT_NO_FATAL_FAILURE(compareFutureWithSelectedAndNative(true));
}

TEST_F(CanonicalOrderFutureActorTest, SameFutureApproachWithoutBraceRetainsOrdinaryHealthParity)
{
	ASSERT_NO_FATAL_FAILURE(compareFutureWithSelectedAndNative(false));
}

TEST_F(HeroCommandAITest, ConstrainedPursuitRetainsMovementOnlyFallbackWithoutHeroOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(characterizeConstrainedFallback(false));
}

TEST_F(HeroCommandAITest, ConstrainedTimeStopPrecedesExistingPursuitAndRetainsPhysicalSidePass)
{
	ASSERT_NO_FATAL_FAILURE(characterizeConstrainedFallback(true));
}

TEST_F(HeroCommandAITest, ConstrainedPursuitChecksSpentHeroAllowanceBeforeMovementFallback)
{
	ASSERT_NO_FATAL_FAILURE(characterizeConstrainedFallback(false, true));
}

TEST_F(HeroCommandAITest, ConstrainedTimeStopChecksSpentHeroAllowanceBeforePassing)
{
	ASSERT_NO_FATAL_FAILURE(characterizeConstrainedFallback(true, true));
}

// Exercise the actual optional-Hero gate in CBattleAI, rather than calling the evaluator directly.
class ConstrainedHeroActionAITest : public HeroCommandAITest
{
protected:
	void SetUp() override
	{
		HeroCommandAITest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandAITest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void configureHeroBeforeBattle() override
	{
		const SecondarySkill offense(SecondarySkill::decode(newHorizonsOffense::SKILL));
		ASSERT_GE(offense.getNum(), 0);
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const std::string pursuit = "new-horizons:offense.pursuit";
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(newHorizonsOffense::SKILL,
			pursuit, attackerSideHero->getPerkSkillRank(newHorizonsOffense::SKILL)));
		attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, pursuit});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, pursuit));
	}

	void characterizePositiveHeroAction(bool stopped, bool throwing = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareEvaluation(true));
		attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 99, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
		ASSERT_EQ(attackerSideHero->getEffectPower(spell), 99);
		const auto divisor = attackerSideHero->getEffectPowerDivisor(spell);
		ASSERT_GT(divisor, 0);
		const auto rating = int64_t{99} * divisor;
		ASSERT_LE(rating, std::numeric_limits<int32_t>::max());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, static_cast<int32_t>(rating),
			ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getEffectPower(spell), rating);
		ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), divisor);
		ASSERT_EQ(attackerSideHero->getEffectPower(spell) / divisor, 99);

		if(stopped)
			active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
		else
		{
			// Earn the movement tail through the authoritative kill path; never assign its allowance.
			CStack * bait = nullptr;
			for(const auto hex : active->getSurroundingHexes())
				if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex))
				{
					bait = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), hex, 1);
					break;
				}
			ASSERT_NE(bait, nullptr);
			const auto baitId = bait->unitId();
			ASSERT_EQ(battle()->battleActiveUnit()->unitId(), active->unitId());
			const auto attack = BattleAction::makeMeleeAttack(active, bait->getPosition(), active->getPosition());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), attack));
			const auto * survivor = battle()->battleGetUnitByID(baitId);
			ASSERT_TRUE(!survivor || !survivor->alive());
			ASSERT_GT(active->pursuitMovementRemaining, 0);
		}
		ASSERT_EQ(active->isTimeStopped(), stopped);
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), active->unitId());
		ASSERT_EQ(active->getCount(), 100);
		ASSERT_EQ(enemy->getCount(), 100);
		ASSERT_EQ(active->getAvailableHealth(), active->getTotalHealth());
		ASSERT_EQ(enemy->getAvailableHealth(), enemy->getTotalHealth());
		// Keep the existing strong-offensive-spell oracle, including the original target army.
		const auto spellDamage = spell->calculateDamage(attackerSideHero);
		const auto melee = battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.max;
		ASSERT_GT(spellDamage, melee / 2);
		ASSERT_LT(spellDamage, enemy->getAvailableHealth());
		ASSERT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
		ASSERT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
		ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(), spells::Mode::HERO, attackerSideHero));

		for(const auto * unit : battle()->battleGetAllStacks(false))
			(void)unit->counterAttacks.available();
		const auto world = gameState()->saveToMemory();
		const auto serverRng = rngState();
		const auto randomBytes = [](const CRandomGenerator & value)
		{
			CMemorySerializer memory;
			memory.oser & value;
			return memory.extractBuffer();
		};
		const auto defaultRng = randomBytes(CRandomGenerator::getDefault());
		const auto gameRng = randomBytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
		callback->waitTillRealize = true;
		callback->throwOnSubmit = throwing;
		if(throwing)
			EXPECT_THROW(chooseActive(), std::runtime_error);
		else
			ASSERT_NO_THROW(chooseActive());
		EXPECT_TRUE(callback->waitTillRealize); // CBattleAI destruction restores the caller's strategy on either exit.
		EXPECT_TRUE(callback->unitActions.empty()); // Accepted/throwing Hero submission cannot fall through.
		ASSERT_EQ(callback->submitted.size(), 1u);
		const auto & action = callback->submitted.front();
		ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
		EXPECT_EQ(action.spell, SpellID::IMPLOSION);
		const auto target = action.getTarget(battle());
		ASSERT_EQ(target.size(), 1u);
		ASSERT_NE(target.front().unitValue, nullptr);
		EXPECT_EQ(target.front().unitValue->unitId(), enemy->unitId());
		EXPECT_EQ(gameState()->saveToMemory(), world);
		EXPECT_EQ(rngState(), serverRng);
		EXPECT_EQ(randomBytes(CRandomGenerator::getDefault()), defaultRng);
		EXPECT_EQ(randomBytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		if(!throwing)
		{
			const auto health = enemy->getAvailableHealth();
			const auto mana = attackerSideHero->getManaAvailable();
			ASSERT_NO_FATAL_FAILURE(executeChosen());
			EXPECT_LT(enemy->getAvailableHealth(), health);
			EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
			EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
		}
	}
};

TEST_F(ConstrainedHeroActionAITest, TimeStopSubmitsBeneficialSpellBeforePhysicalPass)
{
	ASSERT_NO_FATAL_FAILURE(characterizePositiveHeroAction(true));
}

TEST_F(ConstrainedHeroActionAITest, EarnedPursuitSubmitsBeneficialSpellBeforeMovement)
{
	ASSERT_NO_FATAL_FAILURE(characterizePositiveHeroAction(false));
}

TEST_F(ConstrainedHeroActionAITest, ThrowingHeroSubmissionRestoresCallbackWithoutUnitFallback)
{
	ASSERT_NO_FATAL_FAILURE(characterizePositiveHeroAction(true, true));
}

namespace
{
struct CallbackOrderTrace final : heroActionSelection::Observer
{
	std::array<heroActionSelection::CandidateValue, 128> values{};
	std::array<std::optional<heroActionSelection::CandidateIdentity>, 128> identities{};
	size_t count = 0;
	bool overflow = false;
	std::optional<heroActionSelection::SelectionValue> selection;
	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		if(count == values.size()) { overflow = true; return; }
		values[count++] = value;
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override { selection = value; }
	bool observeIdentity() const noexcept override { return true; }
	void candidateIdentity(size_t ordinal, heroActionSelection::CandidateIdentity value) noexcept override
	{
		if(ordinal >= identities.size()) { overflow = true; return; }
		identities[ordinal] = std::move(value);
	}
	std::string ledger() const
	{
		std::ostringstream out;
		for(size_t i = 0; i < count; ++i)
			out << values[i].ordinal << ':' << static_cast<int>(values[i].command) << ':'
				<< values[i].spell.getNum() << ':' << values[i].overcharge << ':'
				<< std::bit_cast<uint32_t>(values[i].value) << ';';
		return out.str();
	}
};
struct CallbackOrderOutcome
{
	CallbackOrderTrace trace;
	heroDecisionPressure::Experiment experiment;
	std::vector<BattleAction> heroActions;
	std::vector<BattleAction> unitActions;
};
void recordActualCallbackWork(const heroTimingTest::Capture & capture,
	const CallbackOrderOutcome & directNull, const CallbackOrderOutcome & directOn,
	const CallbackOrderOutcome & actualOn)
{
	const auto rows = capture.records();
	ASSERT_EQ(rows.size(), 3u) << "Exactly the existing NULL, ON and real-callback attempts";
	const std::array<size_t, 3> submissions{directNull.heroActions.size(), directOn.heroActions.size(), actualOn.heroActions.size()};
	for(size_t index = 0; index < rows.size(); ++index)
	{
		const auto & row = rows[index];
		EXPECT_EQ(row.at("loop_joined"), 1);
		EXPECT_EQ(row.at("loop_threw"), 0);
		EXPECT_EQ(row.at("loop_calls"), 1);
		EXPECT_EQ(row.at("submit_calls"), submissions[index]);
		EXPECT_EQ(row.at("submit_returned"), submissions[index]);
		EXPECT_EQ(row.at("submit_threw"), 0);
		for(const auto * field : {"battle", "player", "side", "stack", "round", "candidates"})
			EXPECT_EQ(row.at(field), rows.front().at(field));
		if(index > 0)
			EXPECT_EQ(row.at("hero_attempt"), rows[index - 1].at("hero_attempt") + 1);
	}
	EXPECT_EQ(rows[0].at("candidates"), directNull.trace.count);
	EXPECT_EQ(rows[1].at("candidates"), directOn.trace.count);
	EXPECT_EQ(rows[2].at("candidates"), actualOn.trace.count);
	const auto declined = std::count(directOn.experiment.declined.begin(), directOn.experiment.declined.end(), true);
	ASSERT_GT(declined, 0);
	EXPECT_EQ(rows[2].at("continuation_full_calls"), rows[1].at("continuation_full_calls"));
	EXPECT_LT(rows[2].at("continuation_full_calls"), rows[0].at("continuation_full_calls"));
	const auto omitted = rows[0].at("continuation_full_calls") - rows[2].at("continuation_full_calls");
	EXPECT_EQ(omitted, declined);
	::testing::Test::RecordProperty("DiagnosticNullFullCalls", std::to_string(rows[0].at("continuation_full_calls")));
	::testing::Test::RecordProperty("DiagnosticDirectOnFullCalls", std::to_string(rows[1].at("continuation_full_calls")));
	::testing::Test::RecordProperty("DiagnosticActualOnFullCalls", std::to_string(rows[2].at("continuation_full_calls")));
	::testing::Test::RecordProperty("DiagnosticOmittedFullCallsVsNull", std::to_string(omitted));
	::testing::Test::RecordProperty("DiagnosticReferenceDeclinedCandidates", std::to_string(declined));
}

}

class CurrentOrderCallbackAITest : public HeroCommandAITest
{
protected:
	bool contactProfile = false;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandAITest::mapLoaded(loaded);
		if(!contactProfile)
			return;
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & profiles = heroes["startingSkills"]["startingDevelopmentProfiles"].Struct();
		const auto found = profiles.find("core:orrin");
		ASSERT_NE(found, profiles.end());
		ASSERT_EQ(found->second["startingPerks"].Vector().size(), 1u);
		ASSERT_EQ(found->second["startingPerks"].Vector().front()["perk"].String(), "new-horizons:archery.targetCaller");
		profiles.erase(found); // Supported generic creation fallback, before initHero; never a runtime respec.
		heroes.setOverrideFlag(true); // Keep this complete authored map copy; merging would restore the omitted entry.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
		ASSERT_FALSE(loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)
			["startingSkills"]["startingDevelopmentProfiles"].Struct().contains("core:orrin"));
	}
	void selectPerk(const char * skill, const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed, attackerSideHero->getPerkOfferWeightContext());
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.skillId == skill && offers[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No canonical offer for " << perk;
	}
	void acquireContactPointBlank(const battle::Units & contacts)
	{
		ASSERT_FALSE(attackerSideHero->getPrimaryGrowthRules()["startingSkills"]["startingDevelopmentProfiles"]
			.Struct().contains("core:orrin"));
		ASSERT_FALSE(attackerSideHero->getPerkState().hasSelection(
			"new-horizons:archery", "new-horizons:archery.targetCaller"));
		const SecondarySkill archery(SecondarySkill::decode("new-horizons:archery"));
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(archery), MasteryLevel::NONE);
		attackerSideHero->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto beforePointBlank = battle()->battleEstimateDamage(
			BattleAttackInfo(active, contacts.front(), 0, true));
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(
			"new-horizons:archery", "new-horizons:archery.pointBlankShot", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(selectPerk("new-horizons:archery", "new-horizons:archery.pointBlankShot", MasteryLevel::BASIC));
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:archery", "new-horizons:archery.pointBlankShot"));
		ASSERT_FALSE(attackerSideHero->getPerkState().hasSelection(
			"new-horizons:archery", "new-horizons:archery.targetCaller"));
		const auto plain = battle()->battleEstimateDamage(BattleAttackInfo(active, contacts.front(), 0, true));
		EXPECT_GE(plain.damage.min, 2 * beforePointBlank.damage.min);
		EXPECT_LE(plain.damage.min, 2 * beforePointBlank.damage.min + 1);
		EXPECT_GE(plain.damage.max, 2 * beforePointBlank.damage.max);
		EXPECT_LE(plain.damage.max, 2 * beforePointBlank.damage.max + 1);
		RecordProperty("PlainAdjacentBeforePointBlank", std::to_string(beforePointBlank.damage.min) + "," + std::to_string(beforePointBlank.damage.max));
		RecordProperty("PlainAdjacentWithPointBlank", std::to_string(plain.damage.min) + "," + std::to_string(plain.damage.max));
		for(const auto * own : battle()->battleAliveUnits(BattleSide::ATTACKER))
			for(const auto * enemy : contacts)
			{
				ASSERT_TRUE(battle()->battleCanShoot(own, enemy->getPosition()));
				ASSERT_TRUE(battle()->isMeleeAttackPossible(enemy, own));
			}
		auto focused = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		PossibleSpellcast candidate;
		candidate.command = HeroCommand::FOCUS_FIRE;
		candidate.commandTargets = {contacts.front()->unitId()};
		const auto admission = heroActionProjection::admitCandidate(*focused, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(admission.admitted);
		ASSERT_TRUE(admission.orderEffects);
		ASSERT_TRUE(admission.orderAllowance);
		ASSERT_TRUE(focused->projectAcceptedHeroOrder(BattleSide::ATTACKER,
			*admission.orderEffects, *admission.orderAllowance));
		const auto focus = focused->battleEstimateDamage(BattleAttackInfo(
			focused->battleGetUnitByID(active->unitId()), focused->battleGetUnitByID(contacts.front()->unitId()), 0, true));
		RecordProperty("FocusFireAdjacentWithPointBlank", std::to_string(focus.damage.min) + "," + std::to_string(focus.damage.max));
	}
	void configureWorldInitialization(StartInfo & info, GameRandomizer & randomizer) const override
	{
		info.startTime = 1'700'000'000;
		randomizer.setSeed(1337);
	}
	void prepareCurrentOrder(bool contact)
	{
		useRevisedOrders = true;
		contactProfile = contact;
		dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(1337);
		CRandomGenerator::getDefault().setSeed(1337);
		startGame();
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->removeAllSpells();
			if(hero->hasSpellbook()) hero->removeArtifact(ArtifactPosition::SPELLBOOK);
			for(const auto attribute : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE})
				hero->setPrimarySkill(attribute, 0, ChangeValueMode::ABSOLUTE);
		}
		if(!contact) attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 200, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged clear;
		clear.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			clear.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(clear);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 5), 200);
		if(contact)
		{
			active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::FREE_SHOOTING, BonusSource::OTHER, 1, BonusSourceID()));
			const auto & neighbours = active->getPosition().getNeighbouringTiles();
			auto * first = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), neighbours[0], 150);
			auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), neighbours[1], 250);
			BattleHex ally = BattleHex::INVALID;
			for(const auto hex : first->getPosition().getNeighbouringTiles())
				if(hex != active->getPosition() && hex.isAvailable()
					&& BattleHex::getDistance(hex, second->getPosition()) == 1
					&& !battle()->battleGetUnitByPos(hex, true)
					&& battle()->battleGetAllObstaclesOnPos(hex, false).empty())
				{ ally = hex; break; }
			ASSERT_TRUE(ally.isAvailable());
			addStack(BattleSide::ATTACKER, creatureByName("core:archer"), ally, 200);
		}
		else
			for(int i = 0; i < 3; ++i)
				addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(13, 1 + 4 * i), 150 + 50 * i);
		beginCombat();
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = active->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		for(const auto * unit : battle()->battleGetAllStacks(false)) (void)unit->counterAttacks.available();
		callback = std::make_shared<RecordingCommandCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CommandEnvironment>(gameState());
		if(contact)
		{
			const auto contacts = battle()->battleAliveUnits(BattleSide::DEFENDER);
			ASSERT_EQ(contacts.size(), 2u);
			for(const auto * own : battle()->battleAliveUnits(BattleSide::ATTACKER))
				for(const auto * target : contacts)
				{
					ASSERT_TRUE(battle()->isMeleeAttackPossible(target, own));
					ASSERT_FALSE(battle()->battleCanShoot(target, own->getPosition()));
				}
			ASSERT_NO_FATAL_FAILURE(acquireContactPointBlank(contacts));
			for(const auto * unit : battle()->battleGetAllStacks(false)) (void)unit->counterAttacks.available();
			RecordProperty("PointBlankFocusFireQualification", "Existing adjacent stacking behavior; rerun after its separate mechanics correction");
		}
	}
	auto unchangedState()
	{
		return std::array<std::vector<std::byte>, 4>{gameState()->saveToMemory(), rngState(),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}
	CallbackOrderOutcome evaluateCallback(bool realCallback, bool directOn = false)
	{
		CallbackOrderOutcome result;
		callback->submitted.clear();
		callback->unitActions.clear();
		if(realCallback)
		{
			CBattleAI ai(&result.trace);
			AutocombatPreferences preferences;
			preferences.enableSpellsUsage = false;
			ai.initBattleInterface(environment, callback, preferences);
			ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
				attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
			ai.activeStack(BattleID(0), active);
		}
		else
		{
			int own = 0, opposing = 0;
			for(const auto * unit : battle()->battleGetAllStacks())
				(unit->unitSide() == BattleSide::ATTACKER ? own : opposing)
					+= unit->getCount() * unit->creatureId().toCreature()->getAIValue();
			const float ratio = opposing ? static_cast<float>(own) / opposing : 1.0f;
			const int horizon = gameState()->getStartInfo()->difficulty < 4 ? 2 : 10;
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, ratio, horizon);
			const auto fallback = evaluator.selectStackAction(active);
			if(!evaluator.attemptCastingSpell(active, false, &result.trace, nullptr, nullptr, nullptr,
				directOn ? &result.experiment : nullptr))
				callback->unitActions.push_back(fallback);
		}
		result.heroActions = callback->submitted;
		result.unitActions = callback->unitActions;
		return result;
	}
	void characterizeIntendedOn(HeroCommand positive)
	{
		std::optional<heroTimingTest::Capture> workCounts;
		if(heroTimingTest::optedIn()) workCounts.emplace();
		const auto before = unchangedState();
		const auto directNull = evaluateCallback(false);
		EXPECT_EQ(unchangedState(), before);
		const auto directOn = evaluateCallback(false, true);
		EXPECT_EQ(unchangedState(), before);
		const auto actualOn = evaluateCallback(true);
		EXPECT_EQ(unchangedState(), before);
		if(workCounts)
			ASSERT_NO_FATAL_FAILURE(recordActualCallbackWork(*workCounts, directNull, directOn, actualOn));
		ASSERT_FALSE(directNull.trace.overflow);
		ASSERT_FALSE(directOn.trace.overflow);
		ASSERT_FALSE(actualOn.trace.overflow);
		ASSERT_TRUE(actualOn.trace.selection);
		ASSERT_TRUE(directNull.trace.selection);
		ASSERT_GT(actualOn.trace.count, 0u);
		ASSERT_EQ(actualOn.trace.count, directNull.trace.count);
		ASSERT_EQ(actualOn.trace.count, directOn.trace.count);
		for(size_t ordinal = 0; ordinal < actualOn.trace.count; ++ordinal)
		{
			ASSERT_EQ(actualOn.trace.values[ordinal].ordinal, ordinal);
			ASSERT_TRUE(actualOn.trace.identities[ordinal]);
			ASSERT_TRUE(directNull.trace.identities[ordinal]);
			ASSERT_TRUE(directOn.trace.identities[ordinal]);
		}
		EXPECT_EQ(actualOn.trace.ledger(), directOn.trace.ledger());
		EXPECT_EQ(actualOn.trace.identities, directOn.trace.identities);
		EXPECT_EQ(actualOn.trace.selection->ordinal, directOn.trace.selection->ordinal);
		EXPECT_EQ(actualOn.trace.selection->disposition, directOn.trace.selection->disposition);
		EXPECT_EQ(std::bit_cast<uint32_t>(actualOn.trace.selection->baseline),
			std::bit_cast<uint32_t>(directOn.trace.selection->baseline));
		EXPECT_EQ(projectedHitTest::serialized(actualOn.heroActions), projectedHitTest::serialized(directOn.heroActions));
		EXPECT_EQ(projectedHitTest::serialized(actualOn.unitActions), projectedHitTest::serialized(directOn.unitActions));
		if(positive == HeroCommand::RIPOSTE)
		{
			const auto count = [&](HeroCommand command)
			{
				return std::count_if(directNull.trace.values.begin(), directNull.trace.values.begin() + directNull.trace.count,
					[&](const auto & value) { return value.command == command; });
			};
			EXPECT_EQ(count(HeroCommand::FOCUS_FIRE), 2);
			EXPECT_EQ(count(HeroCommand::RIPOSTE), 1);
		}
		ASSERT_GE(directOn.experiment.cheapScores.size(), 3u);
		ASSERT_TRUE(directOn.experiment.baselineReady);
		EXPECT_TRUE(std::any_of(directOn.experiment.cheapScores.begin(), directOn.experiment.cheapScores.end(),
			[&](const auto & ranked) { return directOn.trace.values[ranked.ordinal].command == positive && ranked.marginal > 0; }));
		EXPECT_GT(std::count(directOn.experiment.declined.begin(), directOn.experiment.declined.end(), true), 0);
		EXPECT_GT(directOn.experiment.refinementContinuationCalls, 0u);
		EXPECT_LE(directOn.experiment.refinementContinuationCalls, 2u);
		RecordProperty("ActualOnLedger", actualOn.trace.ledger());
		RecordProperty("OldNullLedger", directNull.trace.ledger());
		RecordProperty("IntendedDirectOnLedger", directOn.trace.ledger());
		ASSERT_EQ(directOn.heroActions.size(), 1u);
		ASSERT_EQ(actualOn.heroActions.size(), 1u);
		EXPECT_TRUE(actualOn.unitActions.empty());
		// Real ON must match accepted direct ON; old NULL choice differences are recorded.
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), actualOn.heroActions.front()));
	}
};

TEST_F(CurrentOrderCallbackAITest, IntendedOnRangedFocusFireMatchesAcceptedDirectOn)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrentOrder(false));
	ASSERT_NO_FATAL_FAILURE(characterizeIntendedOn(HeroCommand::FOCUS_FIRE));
}

TEST_F(CurrentOrderCallbackAITest, IntendedOnContactRiposteMatchesAcceptedDirectOn)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrentOrder(true));
	ASSERT_NO_FATAL_FAILURE(characterizeIntendedOn(HeroCommand::RIPOSTE));
}

namespace
{
std::string callbackTimerHex(const std::vector<std::byte> & bytes)
{
	constexpr size_t maximumRawBytes = 64 * 1024 * 1024;
	if(bytes.size() > maximumRawBytes)
		throw std::runtime_error("Callback timer value export exceeded raw-byte bound: actual="
			+ std::to_string(bytes.size()) + " limit=" + std::to_string(maximumRawBytes));
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(bytes.size() * 2);
	for(const auto byte : bytes)
	{
		const auto value = std::to_integer<unsigned int>(byte);
		result.push_back(digits[value >> 4]);
		result.push_back(digits[value & 15]);
	}
	return result;
}
}

TEST_F(CurrentOrderCallbackAITest, DISABLED_CurrentRangedWholeCallbackWall)
{
	ASSERT_FALSE(heroTimingTest::optedIn()) << "Work-count diagnostics are a separate invocation";
	ASSERT_NO_FATAL_FAILURE(prepareCurrentOrder(false));
	ASSERT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	ASSERT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	ASSERT_FALSE(battle()->getCrisisCommandState().choice());
	ASSERT_FALSE(active->isTimeStopped());
	ASSERT_EQ(active->pursuitMovementRemaining, 0);
	const auto before = unchangedState();
	const auto startedActions = server.startedActions.size();
	const bool waitBefore = callback->waitTillRealize;
	CBattleAI::TimingSummary measured;
	{
		CBattleAI ai; // Production null observer; no direct reference decisions.
		AutocombatPreferences preferences;
		preferences.enableSpellsUsage = false;
		ai.initBattleInterface(environment, callback, preferences);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), active);
		const auto summary = ai.getTimingSummary();
		ASSERT_TRUE(summary);
		measured = *summary;
	}
	EXPECT_EQ(callback->waitTillRealize, waitBefore);
	EXPECT_EQ(unchangedState(), before);
	EXPECT_EQ(server.startedActions.size(), startedActions);
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_TRUE(callback->unitActions.empty());
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(measured.activeStackCalls, 1u);
	EXPECT_EQ(measured.activeStackInFlight, 0u);
	EXPECT_EQ(measured.evaluatorConstruction.calls, 1u);
	EXPECT_EQ(measured.stackActionSelection.calls, 1u);
	EXPECT_EQ(measured.heroAction.calls, 1u);
	EXPECT_EQ(measured.directUnitSubmission.calls, 0u);
	EXPECT_GT(measured.activeStackTotalMicroseconds, 0u);
	EXPECT_GT(measured.heroAction.totalMicroseconds, 0u);
	EXPECT_GE(measured.activeStackTotalMicroseconds, measured.heroAction.totalMicroseconds);
	RecordProperty("CallbackWallMicroseconds", std::to_string(measured.activeStackTotalMicroseconds));
	RecordProperty("HeroActionMicroseconds", std::to_string(measured.heroAction.totalMicroseconds));
	RecordProperty("CallbackCalls", std::to_string(measured.activeStackCalls));
	RecordProperty("HeroActionCalls", std::to_string(measured.heroAction.calls));
	RecordProperty("Performed", "true");
	const auto actionBytes = projectedHitTest::serialized(callback->submitted);
	RecordProperty("HeroActionsBytes", std::to_string(actionBytes.size()));
	for(size_t part = 0; part < before.size(); ++part)
		RecordProperty("StatePart" + std::to_string(part) + "Bytes", std::to_string(before[part].size()));
	RecordProperty("HeroActionsWire", callbackTimerHex(actionBytes));
	for(size_t part = 0; part < before.size(); ++part)
		RecordProperty("StatePart" + std::to_string(part) + "Wire", callbackTimerHex(before[part]));
	// Actual authority acceptance is outside both existing decision clocks.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(server.startedActions.size(), startedActions + 1);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}
