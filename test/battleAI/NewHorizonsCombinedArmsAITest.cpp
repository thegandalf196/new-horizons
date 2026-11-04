/*
 * NewHorizonsCombinedArmsAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "PurifyRuleFixtures.h"
#include "SelectedHeroUtilityCapture.h"
#include "HeroActionSelectionObserver.h"
#include "HeroActionSelectionState.h"

#include "../SpellPointTestUtils.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/HeroOrderCandidates.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char AGGRESSIVE_COMMANDER[] = "new-horizons:command.aggressiveCommander";
constexpr char DEFENSIVE_COMMANDER[] = "new-horizons:command.defensiveCommander";
constexpr char COMBINED_ARMS[] = "new-horizons:command.combinedArms";

class CombinedArmsAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CombinedArmsAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class CombinedArmsAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	CombinedArmsAICallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

}

class NewHorizonsCombinedArmsAITest : public HeroCommandFixture
{
protected:
	bool isolateToFocusFire = false;
	bool isolateToFeint = false;
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<CombinedArmsAIEnvironment> environment;
	std::shared_ptr<CombinedArmsAICallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));

		if(!isolateToFocusFire && !isolateToFeint)
			return;
		const auto current = JsonNode(JsonPath::builtin("config/newHorizonsCombat"))["combat"]["heroCommands"];
		auto rules = purifyTest::neutralSpellCompetitionCommands();
		const auto preserved = isolateToFocusFire ? HeroCommand::FOCUS_FIRE : HeroCommand::FEINT;
		rules["commands"][heroCommands::key(preserved)] = current["commands"][heroCommands::key(preserved)];
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	void configureHero(const char * basicPerk, int attack)
	{
		const int command = SecondarySkill::decode(COMMAND_SKILL);
		ASSERT_GE(command, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(command), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({COMMAND_SKILL, basicPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, basicPerk));
		attackerSideHero->setSecSkillLevel(SecondarySkill(command), MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({COMMAND_SKILL, COMBINED_ARMS});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, COMBINED_ARMS));
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(COMMAND_SKILL), MasteryLevel::ADVANCED);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, attack, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 50, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}

	void removeInitialBattleStacks()
	{
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);
	}

	void prepareAI(CStack * activeStack)
	{
		active = activeStack;
		ASSERT_NE(active, nullptr);
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<CombinedArmsAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CombinedArmsAIEnvironment>(gameState());
	}

	int64_t initiatingMinimum = 0;
	int64_t initiatingMaximum = 0;
	int64_t retaliationMinimum = 0;
	int64_t retaliationMaximum = 0;
	void prepareCurrentFeint()
	{
		isolateToFeint = true;
		startGame();
		configureHero(DEFENSIVE_COMMANDER, 100);
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, DEFENSIVE_COMMANDER));
		ASSERT_TRUE(heroCommands::hasCombinedArms(attackerSideHero));
		startBattle();
		removeInitialBattleStacks();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(4, 5), 1000);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		// Keep both stacks alive and counts stable through the native exchange.
		for(auto * unit : {active, enemy})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
				BonusSource::OTHER, 100000, BonusSourceID()));
		beginCombat();
		prepareAI(active);
		// Materialize the ordinary retaliation-cap getter latch during setup.
		// Later full-byte witnesses retain this latch; they do not claim cold-cache
		// bitwise immutability or normalize any state after AI evaluation.
		const auto nativeRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto ownAllowance = projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
		const auto enemyAllowance = projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::DEFENDER));
		const auto ownMana = attackerSideHero->getManaAvailable();
		const auto enemyMana = defenderSideHero->getManaAvailable();
		for(auto * unit : {active, enemy})
			ASSERT_EQ(unit->counterAttacks.total(), 1);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), nativeRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
		EXPECT_EQ(projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), ownAllowance);
		EXPECT_EQ(projectedHitTest::serialized(battle()->getHeroActionAllowances(BattleSide::DEFENDER)), enemyAllowance);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), ownMana);
		EXPECT_EQ(defenderSideHero->getManaAvailable(), enemyMana);
		EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
		EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::DEFENDER));
		EXPECT_TRUE(callback->submitted.empty());
		ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FEINT));
		const auto preparedOrder = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::FEINT, {});
		ASSERT_TRUE(preparedOrder);
		ASSERT_TRUE(preparedOrder->revision);
		const auto capacity = attackerSideHero->getLeadershipCapacity();
		ASSERT_TRUE(capacity);
		// Accepted current rules: base15 + 0.015*Leadership at Advanced120%,
		// then Combined Arms +5 percentage points, with a final 60% cap.
		const int expected = std::min(60, static_cast<int>(std::lround(
			15 + 0.015 * std::max<int64_t>(0, capacity->capacity) * 1.2)) + 5);
		EXPECT_EQ(preparedOrder->revision->feintReductionPercent, expected);
		EXPECT_EQ(preparedOrder->revision->feintCap, 60);
		EXPECT_EQ(preparedOrder->revision->expiresAfterRound, battle()->getRound());
		const BattleAttackInfo melee(active, enemy, 0, false);
		DamageEstimation beforeRetaliation;
		const auto before = battle()->battleEstimateDamage(melee, &beforeRetaliation);
		const auto healthBeforeProjection = enemy->getAvailableHealth();
		HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
		projected.setHeroOrderState(BattleSide::ATTACKER, preparedOrder);
		DamageEstimation projectedRetaliation;
		const auto projectedHit = projected.battleEstimateDamage(melee, &projectedRetaliation);
		EXPECT_EQ(projectedHit.damage.min, before.damage.min);
		EXPECT_EQ(projectedHit.damage.max, before.damage.max);
		EXPECT_LT(projectedRetaliation.damage.min, beforeRetaliation.damage.min);
		EXPECT_LT(projectedRetaliation.damage.max, beforeRetaliation.damage.max);
		EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
		EXPECT_EQ(enemy->getAvailableHealth(), healthBeforeProjection);
		initiatingMinimum = projectedHit.damage.min;
		initiatingMaximum = projectedHit.damage.max;
		retaliationMinimum = projectedRetaliation.damage.min;
		retaliationMaximum = projectedRetaliation.damage.max;
	}

	bool chooseOrder(HeroCommand expected, BattleAction & selected)
	{
		const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		if(!spell || !spell->canBeCast(callback->getBattle(BattleID(0)).get(),
			spells::Mode::HERO, attackerSideHero))
		{
			ADD_FAILURE() << "Magic Arrow must remain a legal competing hero action";
			return false;
		}

		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		selectedHeroTest::Capture capture;
		const bool submitted = evaluator.canCastSpell() && evaluator.attemptCastingSpell(active);
		SCOPED_TRACE(capture.describe(callback->submitted));
		if(!submitted)
		{
			ADD_FAILURE() << "The evaluator did not submit a hero action";
			return false;
		}
		if(callback->submitted.size() != 1u)
		{
			ADD_FAILURE() << "Expected one submitted action, got " << callback->submitted.size();
			return false;
		}
		selected = callback->submitted.front();
		if(!capture.hasSelectedUtility())
		{
			ADD_FAILURE() << "Missing/ambiguous existing selected signed-utility trace";
			return false;
		}
		if(selected.actionType != EActionType::HERO_COMMAND || selected.command != expected)
		{
			ADD_FAILURE() << "Expected Order " << heroCommands::key(expected)
				<< ", got action type " << static_cast<int>(selected.actionType)
				<< " and Order " << heroCommands::key(selected.command);
			return false;
		}
		return true;
	}

	void execute(const BattleAction & action)
	{
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}

	std::optional<int64_t> recordedDirectDamage(uint32_t attackerId, uint32_t targetId,
		bool shooting, size_t firstAttack = 0, bool counter = false) const
	{
		for(size_t i = firstAttack; i < server.attacks.size(); ++i)
		{
			const auto & attack = server.attacks[i];
			if(attack.stackAttacking != attackerId || attack.shot() != shooting || attack.counter() != counter)
				continue;
			for(const auto & victim : attack.bsa)
				if(victim.stackAttacked == targetId && !victim.isSecondary())
					return victim.damageAmount;
		}
		return std::nullopt;
	}
};

TEST_F(NewHorizonsCombinedArmsAITest, CombinedArmsAdmitsMeleeOnlyFocusFireAndMatchesProjectedDamage)
{
	isolateToFocusFire = true;
	startGame();
	configureHero(AGGRESSIVE_COMMANDER, 47);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));
	ASSERT_TRUE(heroCommands::hasCombinedArms(attackerSideHero));
	startBattle();
	removeInitialBattleStacks();
	active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 100);
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(4, 5), 1000);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	prepareAI(active);
	EXPECT_FALSE(active->isShooter());
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	const auto preparedOrder = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::FOCUS_FIRE, {enemy->unitId()});
	const auto preparedFocusFire = battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, enemy->unitId());
	ASSERT_TRUE(preparedOrder);
	ASSERT_TRUE(preparedFocusFire);
	ASSERT_TRUE(std::binary_search(preparedFocusFire->recipientUnitIds.begin(),
		preparedFocusFire->recipientUnitIds.end(), active->unitId()));
	const auto expectedMeleeBonus = heroCommands::combinedArmsFocusFirePercent(
		preparedFocusFire->rangedDamagePercent, *attackerSideHero);
	EXPECT_EQ(expectedMeleeBonus, static_cast<double>(preparedFocusFire->rangedDamagePercent) / 2.0);
	EXPECT_EQ(std::fmod(expectedMeleeBonus, 1.0), 0.5)
		<< "An odd snapshot percentage must remain fractional through the shared helper";

	// The sand-shore backdrop does not replace the TinyMap's native grass ground.
	ASSERT_EQ(battle()->getTerrainType(), TerrainId(ETerrainId::GRASS));
	const auto nativeAttack = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK))
		.And(Selector::sourceTypeSel(BonusSource::TERRAIN_NATIVE));
	const auto nativeDefense = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE))
		.And(Selector::sourceTypeSel(BonusSource::TERRAIN_NATIVE));
	for(const auto * unit : {active, enemy})
	{
		ASSERT_NE(unit->unitType(), nullptr);
		ASSERT_EQ(unit->unitType()->getBaseAttack(), 20);
		ASSERT_EQ(unit->unitType()->getBaseDefense(), 20);
		ASSERT_EQ(unit->getCurrentTerrain(), TerrainId(ETerrainId::GRASS));
		ASSERT_TRUE(unit->isOnNativeTerrain());
		ASSERT_EQ(unit->valOfBonuses(nativeAttack), 1);
		ASSERT_EQ(unit->valOfBonuses(nativeDefense), 1);
	}

	const BattleAttackInfo meleeAttack(active, enemy, 0, false);
	const auto beforeProjection = battle()->battleEstimateDamage(meleeAttack);
	const auto healthBeforeProjection = enemy->getAvailableHealth();
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	projected.setFocusFireState(BattleSide::ATTACKER, *preparedFocusFire);
	projected.setHeroOrderState(BattleSide::ATTACKER, preparedOrder);
	const auto projectedDamage = projected.calculateDmgRange(meleeAttack);
	// NH hero ratings feed Orders, not passive creature Attack. With fixed
	// Angel damage (50 x 100), equal effective Attack/Defense (base20 + native1), and no other
	// damage modifier, half of the 15% snapshot must yield exactly 5,375.
	ASSERT_EQ(active->getAttack(false), 21);
	ASSERT_EQ(enemy->getDefense(false), 21);
	ASSERT_EQ(active->getAttack(false), enemy->getDefense(false));
	ASSERT_EQ(beforeProjection.damage.min, 5000);
	ASSERT_EQ(beforeProjection.damage.max, 5000);
	EXPECT_DOUBLE_EQ(expectedMeleeBonus, 7.5);
	EXPECT_EQ(projectedDamage.damage.min, 5375);
	EXPECT_EQ(projectedDamage.damage.max, 5375);
	EXPECT_GT(projectedDamage.damage.min, beforeProjection.damage.min);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_EQ(enemy->getAvailableHealth(), healthBeforeProjection);
	const auto manaBeforeAI = attackerSideHero->getManaAvailable();
	const auto healthBeforeAI = enemy->getAvailableHealth();
	const auto activeIdBeforeAI = battle()->getActiveStackID();

	BattleAction action;
	ASSERT_TRUE(chooseOrder(HeroCommand::FOCUS_FIRE, action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeAI);
	EXPECT_EQ(enemy->getAvailableHealth(), healthBeforeAI);
	EXPECT_EQ(battle()->getActiveStackID(), activeIdBeforeAI);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));

	execute(action);
	const auto acceptedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	const auto acceptedMark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(acceptedOrder);
	ASSERT_TRUE(acceptedMark);
	EXPECT_EQ(acceptedOrder->command, HeroCommand::FOCUS_FIRE);
	EXPECT_EQ(acceptedOrder->primaryTargetUnitId, enemy->unitId());
	EXPECT_EQ(acceptedMark->rangedDamagePercent, preparedFocusFire->rangedDamagePercent);
	const auto liveDamage = battle()->battleEstimateDamage(meleeAttack);
	EXPECT_EQ(liveDamage.damage.min, projectedDamage.damage.min);
	EXPECT_EQ(liveDamage.damage.max, projectedDamage.damage.max);

	const auto firstAttack = server.attacks.size();
	execute(BattleAction::makeMeleeAttack(active, enemy, active->getPosition(), false));
	const auto actualDamage = recordedDirectDamage(active->unitId(), enemy->unitId(), false, firstAttack);
	ASSERT_TRUE(actualDamage);
	EXPECT_GE(*actualDamage, liveDamage.damage.min);
	EXPECT_LE(*actualDamage, liveDamage.damage.max);
	EXPECT_GT(server.attacks.size(), firstAttack);
}

TEST_F(NewHorizonsCombinedArmsAITest, CurrentFeintCompetesOnFullFirstMaximumWithoutMutatingState)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrentFeint());
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	const auto live = callback->getBattle(BattleID(0));
	const auto state = [&] (const CBattleInfoCallback & view)
	{
		return heroSelectionTest::snapshot(view, *gameHandler->randomizer,
			dynamic_cast<CRandomGenerator &>(*getRNG()));
	};
	std::vector<heroSelectionTest::StatePart> beforeParts;
	const auto before = heroSelectionTest::snapshot(*live, *gameHandler->randomizer,
		dynamic_cast<CRandomGenerator &>(*getRNG()), &beforeParts);
	const auto checkLive = [&](const char * stage)
	{
		std::vector<heroSelectionTest::StatePart> parts;
		const auto actual = heroSelectionTest::snapshot(*live, *gameHandler->randomizer,
			dynamic_cast<CRandomGenerator &>(*getRNG()), &parts);
		if(actual != before)
			ADD_FAILURE() << stage << ' ' << heroSelectionTest::firstDifference(beforeParts, parts);
	};
	checkLive("before parent construction (repeat snapshot)");
	BattleAction selected;
	{
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		checkLive("after parent construction");
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		checkLive("after sibling construction");
		const auto parentBefore = state(*parent);
		checkLive("after parent snapshot");
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		checkLive("after evaluator construction");
		evaluator.selectStackAction(active);
		checkLive("after selectStackAction");
		ASSERT_TRUE(evaluator.canCastSpell());
		checkLive("after canCastSpell");
		heroSelectionTest::Observer observer;
		ASSERT_TRUE(evaluator.attemptCastingSpell(active, true, &observer));
		checkLive("after attemptCastingSpell");
		ASSERT_EQ(callback->submitted.size(), 1u);
		selected = callback->submitted.front();
		ASSERT_NO_FATAL_FAILURE(observer.expectPerformedMatching(selected));
		ASSERT_TRUE(observer.hasFeint);
		ASSERT_TRUE(observer.hasMagicArrow);
		ASSERT_TRUE(observer.selection);
		EXPECT_GT(observer.bestFeint, observer.selection->baseline);
		EXPECT_FALSE(vstd::isAlmostEqual(observer.bestFeint, observer.selection->baseline));
		EXPECT_LE(observer.bestFeint, observer.best.value);
		EXPECT_EQ(state(*parent), parentBefore);
		EXPECT_EQ(state(*sibling), parentBefore);
		EXPECT_EQ(state(*live), before);
	}
	checkLive("after projected providers destroyed");
	EXPECT_EQ(state(*live), before);
	// Execute the ACTUAL normal selector output, not a forced Feint action.
	ASSERT_NO_FATAL_FAILURE(execute(selected));
}

TEST_F(NewHorizonsCombinedArmsAITest, NativeCurrentFeintRetaliationMatchesProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrentFeint());
	const BattleAttackInfo melee(active, enemy, 0, false);
	const auto round = battle()->getRound();
	ASSERT_NO_FATAL_FAILURE(execute(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::FEINT)));
	const auto acceptedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(acceptedOrder);
	EXPECT_EQ(acceptedOrder->command, HeroCommand::FEINT);
	ASSERT_TRUE(acceptedOrder->revision);
	EXPECT_EQ(acceptedOrder->revision->expiresAfterRound, round);
	DamageEstimation retaliation;
	const auto liveDamage = battle()->battleEstimateDamage(melee, &retaliation);
	EXPECT_EQ(liveDamage.damage.min, initiatingMinimum);
	EXPECT_EQ(liveDamage.damage.max, initiatingMaximum);
	EXPECT_EQ(retaliation.damage.min, retaliationMinimum);
	EXPECT_EQ(retaliation.damage.max, retaliationMaximum);
	const auto firstAttack = server.attacks.size();
	ASSERT_NO_FATAL_FAILURE(execute(BattleAction::makeMeleeAttack(active, enemy, active->getPosition(), false)));
	const auto hit = recordedDirectDamage(active->unitId(), enemy->unitId(), false, firstAttack);
	const auto counter = recordedDirectDamage(enemy->unitId(), active->unitId(), false, firstAttack, true);
	ASSERT_TRUE(hit);
	ASSERT_TRUE(counter);
	EXPECT_GE(*hit, initiatingMinimum);
	EXPECT_LE(*hit, initiatingMaximum);
	EXPECT_GE(*counter, retaliationMinimum);
	EXPECT_LE(*counter, retaliationMaximum);
	EXPECT_EQ(battle()->getRound(), round);
	const auto resolvedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(resolvedOrder);
	EXPECT_EQ(resolvedOrder->command, HeroCommand::FEINT);
	EXPECT_EQ(resolvedOrder->revision->expiresAfterRound, round);
}

TEST_F(NewHorizonsCombinedArmsAITest, CandidateGenerationKeepsPreparedMeleeOnlyFocusFireSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(configureHero(AGGRESSIVE_COMMANDER, 47));
	ASSERT_TRUE(heroCommands::hasCombinedArms(attackerSideHero));
	ASSERT_NO_FATAL_FAILURE(startBattle());
	removeInitialBattleStacks();
	active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 100);
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(4, 5), 1000);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(prepareAI(active));
	ASSERT_FALSE(active->isShooter());
	const auto prepared = battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, enemy->unitId());
	ASSERT_TRUE(prepared);
	class PositiveValuation final : public heroOrderCandidates::Valuation
	{
	public:
		float score(const heroOrderCandidates::ValuationRequest &) const override { return 1.0f; }
	} valuation;
	std::vector<PossibleSpellcast> candidates;
	heroOrderCandidates::appendOrders(candidates, *callback, BattleID(0), BattleSide::ATTACKER, false, valuation);
	const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto & candidate)
	{
		return candidate.command == HeroCommand::FOCUS_FIRE
			&& candidate.commandTargets == std::vector<uint32_t>{enemy->unitId()};
	});
	ASSERT_NE(found, candidates.end());
	ASSERT_TRUE(found->focusFire);
	EXPECT_EQ(*found->focusFire, *prepared);
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_TRUE(callback->submitted.empty());
}
