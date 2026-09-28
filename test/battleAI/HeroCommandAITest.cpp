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
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/modding/CModHandler.h"
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
	CStack * enemy = nullptr;
	std::shared_ptr<RecordingCommandCallback> callback;
	std::shared_ptr<CommandEnvironment> environment;

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

		if(competeBraceAndCharge)
		{
			// A real utility contest: without Countercharge, Brace is worth 50%
			// against the expected advancing stack while Charge is worth 60%.
			// Countercharge should raise only Brace to 75%, changing the winner.
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

TEST_F(CanonicalOrderAITest, EvaluatorChoosesBraceAndAuthoritativeTriggerIsLegal)
{
	prepareOrder(HeroCommand::BRACE);
	assertChosenOrder(HeroCommand::BRACE);
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(enemy, active, 3, false, false));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::BRACE);
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
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
	const auto normal = battle()->battleEstimateDamage(BattleAttackInfo(active, enemy, 0, false));

	assertChosenOrder(HeroCommand::BRACE);
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(enemy, active, 3, false, false));
	BattleAttackInfo preemptive(active, enemy, 0, false);
	preemptive.bracePreemptive = true;
	const auto resolved = battle()->battleEstimateDamage(preemptive);
	EXPECT_EQ(resolved.damage.min, normal.damage.min * 75 / 100);
	EXPECT_EQ(resolved.damage.max, normal.damage.max * 75 / 100)
		<< "AI valuation and saved Brace order damage resolution both use the shared 75% result";
}

TEST_F(CanonicalOrderAITest, ChargeBeatsUnmodifiedBraceInRealOrderCompetition)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::BRACE;
	configureCounterchargePerkData = true;
	competeBraceAndCharge = true;
	prepareEvaluation(false);
	keepOnlyCompetingStacks();
	ASSERT_FALSE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));

	assertChosenOrder(HeroCommand::CHARGE);
}

TEST_F(CanonicalOrderAITest, CounterchargeMakesBraceBeatHigherValuedCharge)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	selectedCommand = HeroCommand::BRACE;
	configureCounterchargePerkData = true;
	selectCountercharge = true;
	competeBraceAndCharge = true;
	prepareEvaluation(false);
	keepOnlyCompetingStacks();
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));

	assertChosenOrder(HeroCommand::BRACE);
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
