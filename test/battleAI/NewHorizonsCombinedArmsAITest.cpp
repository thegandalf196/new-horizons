/*
 * NewHorizonsCombinedArmsAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../SpellPointTestUtils.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
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

void zeroOrderFormula(JsonNode & rules, HeroCommand command)
{
	for(auto & [name, formula] : rules["commands"][heroCommands::key(command)]["effects"].Struct())
	{
		(void)name;
		formula["base"].Integer() = 0;
		formula["attack"].Float() = 0;
		formula["defense"].Float() = 0;
	}
}
}

class NewHorizonsCombinedArmsAITest : public HeroCommandFixture
{
protected:
	bool isolateToFocusFire = false;
	bool isolateToFlank = false;
	CStack * active = nullptr;
	CStack * shooter = nullptr;
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
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));

		if(!isolateToFocusFire && !isolateToFlank)
			return;
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCombat"))["combat"]["heroCommands"];
		const auto isolateOrderCoefficients = [&rules](HeroCommand preservedCommand)
		{
			for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
				HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::BRACE,
				HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND})
				if(command != preservedCommand)
					zeroOrderFormula(rules, command);
		};
		if(isolateToFocusFire)
		{
			// The melee-only fixture isolates the new Focus Fire candidate consumer;
			// every other Order's coefficients are isolated, while Magic Arrow stays
			// a legal competing hero action. This is not an all-Orders ranking test.
			isolateOrderCoefficients(HeroCommand::FOCUS_FIRE);
		}
		if(isolateToFlank)
		{
			// The ranged-only fixture isolates the new Flank damage consumer; every
			// other Order's coefficients are isolated, with Magic Arrow still legal.
			// This is not an all-Orders ranking test.
			isolateOrderCoefficients(HeroCommand::FLANK);
		}
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
		if(!evaluator.canCastSpell() || !evaluator.attemptCastingSpell(active))
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
		bool shooting, size_t firstAttack = 0) const
	{
		for(size_t i = firstAttack; i < server.attacks.size(); ++i)
		{
			const auto & attack = server.attacks[i];
			if(attack.stackAttacking != attackerId || attack.shot() != shooting || attack.counter())
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

	const BattleAttackInfo meleeAttack(active, enemy, 0, false);
	const auto beforeProjection = battle()->battleEstimateDamage(meleeAttack);
	const auto healthBeforeProjection = enemy->getAvailableHealth();
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	projected.setFocusFireState(BattleSide::ATTACKER, *preparedFocusFire);
	projected.setHeroOrderState(BattleSide::ATTACKER, preparedOrder);
	const auto projectedDamage = projected.calculateDmgRange(meleeAttack);
	// NH hero ratings feed Orders, not passive creature Attack. With fixed
	// Angel damage (50 x 100), equal creature Attack/Defense, and no other
	// damage modifier, half of the 15% snapshot must yield exactly 5,375.
	ASSERT_EQ(active->getAttack(false), 20);
	ASSERT_EQ(enemy->getDefense(false), 20);
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

TEST_F(NewHorizonsCombinedArmsAITest, FlankValuesRangedAttackOnlyAndShooterDoesNotRecordASide)
{
	isolateToFlank = true;
	startGame();
	configureHero(DEFENSIVE_COMMANDER, 100);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, DEFENSIVE_COMMANDER));
	ASSERT_TRUE(heroCommands::hasCombinedArms(attackerSideHero));
	startBattle();
	removeInitialBattleStacks();
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 100);
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 1000);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(enemy, nullptr);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 100000, BonusSourceID()));
	const Bonus noChargeMovement(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -10, BonusSourceID());
	for(auto * unit : {static_cast<CStack *>(shooter), enemy})
		unit->addNewBonus(std::make_shared<Bonus>(noChargeMovement));
	beginCombat();
	prepareAI(shooter);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, enemy->getPosition()));
	EXPECT_LT(shooter->getMovementRange(0), 3u)
		<< "This keeps Charge from competing while leaving the ranged attack legal";
	const auto & flankFormula = battle()->getHeroCommandRules()["commands"]["flank"]["effects"]["meleeDamagePercent"];
	const auto preparedOrder = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::FLANK, {enemy->unitId()});
	ASSERT_TRUE(preparedOrder);
	const auto combinedArmsPercent = heroCommands::combinedArmsFlankPercent(
		flankFormula, *attackerSideHero, preparedOrder->warcastingBonusPercent);
	EXPECT_NEAR(combinedArmsPercent, 7.2, 0.0001)
		<< "Only the 12-point Attack-derived component gets Advanced Command efficiency and then is halved";

	const BattleAttackInfo shot(shooter, enemy, 0, true);
	const auto beforeProjection = battle()->battleEstimateDamage(shot);
	const auto healthBeforeProjection = enemy->getAvailableHealth();
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	projected.setHeroOrderState(BattleSide::ATTACKER, preparedOrder);
	const auto projectedDamage = projected.calculateDmgRange(shot);
	EXPECT_GT(projectedDamage.damage.min, beforeProjection.damage.min);
	const auto preparedTarget = preparedOrder->flankFor(enemy->unitId());
	ASSERT_NE(preparedTarget, nullptr);
	EXPECT_EQ(preparedTarget->sideMask, 0);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(enemy->getAvailableHealth(), healthBeforeProjection);
	const auto manaBeforeAI = attackerSideHero->getManaAvailable();
	const auto healthBeforeAI = enemy->getAvailableHealth();
	const auto activeIdBeforeAI = battle()->getActiveStackID();

	BattleAction action;
	ASSERT_TRUE(chooseOrder(HeroCommand::FLANK, action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeAI);
	EXPECT_EQ(enemy->getAvailableHealth(), healthBeforeAI);
	EXPECT_EQ(battle()->getActiveStackID(), activeIdBeforeAI);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));

	execute(action);
	const auto acceptedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(acceptedOrder);
	EXPECT_EQ(acceptedOrder->command, HeroCommand::FLANK);
	EXPECT_EQ(acceptedOrder->primaryTargetUnitId, enemy->unitId());
	const auto liveDamage = battle()->battleEstimateDamage(shot);
	EXPECT_EQ(liveDamage.damage.min, projectedDamage.damage.min);
	EXPECT_EQ(liveDamage.damage.max, projectedDamage.damage.max);

	const auto firstAttack = server.attacks.size();
	execute(BattleAction::makeShotAttack(shooter, enemy));
	const auto actualDamage = recordedDirectDamage(shooter->unitId(), enemy->unitId(), true, firstAttack);
	ASSERT_TRUE(actualDamage);
	EXPECT_GE(*actualDamage, liveDamage.damage.min);
	EXPECT_LE(*actualDamage, liveDamage.damage.max);
	const auto resolvedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(resolvedOrder);
	const auto resolvedTarget = resolvedOrder->flankFor(enemy->unitId());
	ASSERT_NE(resolvedTarget, nullptr);
	EXPECT_EQ(resolvedTarget->sideMask, 0)
		<< "A shot receives the Combined Arms bonus but must not establish a Flank side";
}
