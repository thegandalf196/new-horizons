/*
 * NewHorizonsSecondWindCreatureSpellTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/effects/Effects.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#endif

namespace
{
class NewHorizonsSecondWindCreatureSpellTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * window = nullptr;
	CStack * target = nullptr;
	const SpellID arrow{SpellID::MAGIC_ARROW};

	void prepare()
	{
		startGame();
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:fairieDragon"), BattleHex(4, 5), 10);
		window = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(11, 5), 100000);
		for(auto [unit, initiative] : {std::pair{actor, 1000}, std::pair{window, 900}})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, initiative,
				BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		// Morale immunity makes both good and bad Morale inapplicable without changing casts.
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_MORALE, BonusSource::OTHER, 0, BonusSourceID()));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_FALSE(battle()->battleIsFinished().has_value());
		ASSERT_GT(actor->casts.available(), 1);
	}

	std::unique_ptr<spells::Mechanics> mechanics(spells::Mode mode = spells::Mode::CREATURE_ACTIVE,
		const spells::Caster * caster = nullptr)
	{
		spells::BattleCast cast(battle(), caster ? caster : actor, mode, arrow.toSpell());
		cast.setSpellLevel(MasteryLevel::ADVANCED);
		return arrow.toSpell()->battleMechanics(&cast);
	}

	bool castActive()
	{
		battle::Target destination;
		destination.emplace_back(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeCreatureSpellcast(actor, destination, arrow));
	}

	void grantSecondWind(bool priorCast = true)
	{
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		if(priorCast)
			ASSERT_TRUE(castActive());
		else
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
				BattleAction::makeDefend(actor)));
		ASSERT_EQ(battle()->battleActiveUnit(), window);
		ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
				actor->unitId())));
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_FALSE(actor->moraleExtraActivation);
		ASSERT_FALSE(actor->castSpellThisTurn);
	}

	int output() const
	{
		const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
		EXPECT_TRUE(order);
		return order ? newHorizonsDivineMandate::crownSecondWindPercent(*attackerSideHero, *order,
			actor->unitId()) : 100;
	}

	int64_t preview(spells::Mechanics * context, bool indirect = false)
	{
		JsonNode effect;
		effect["damage"]["type"].String() = "core:damage";
		effect["damage"]["indirect"].Bool() = indirect;
		const auto effects = spells::effects::Effects::loadJson(effect, "core", "magicArrow");
		battle::Target destination;
		destination.emplace_back(target);
		return -effects.at("damage")->getHealthChange(context, destination).hpDelta;
	}
};
#if ENABLE_BATTLE_AI
class SecondWindSpellEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SecondWindSpellEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, ActualNormalCastThenAcceptedSecondWindRenewsCastAndPaysOnlyRemainingCharge)
{
	prepare();
	const auto full = mechanics()->adjustEffectValue(target);
	const auto charges = actor->casts.available();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto before = target->getAvailableHealth();
	grantSecondWind();
	EXPECT_EQ(before - target->getAvailableHealth(), full);
	EXPECT_EQ(actor->casts.available(), charges - 1);
	const int percent = output();
	ASSERT_LT(percent, 100);
	auto extra = mechanics();
	EXPECT_EQ(extra->getDirectCreatureActivationDamagePercent(), percent);
	EXPECT_EQ(extra->adjustEffectValue(target), full);
	EXPECT_EQ(preview(extra.get()), full * percent / 100);
	const auto beforeExtra = target->getAvailableHealth();
	const auto activationCount = server.stackActivations.size();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(beforeExtra - target->getAvailableHealth(), full * percent / 100);
	EXPECT_EQ(actor->casts.available(), charges - 2);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	const auto spent = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(spent);
	EXPECT_FALSE(spent->secondWindActive);
	EXPECT_NE(battle()->battleActiveUnit(), actor);
	for(size_t i = activationCount; i < server.stackActivations.size(); ++i)
		EXPECT_FALSE(server.stackActivations[i].stack == actor->unitId()
			&& (server.stackActivations[i].reason == BattleUnitTurnReason::MORALE
				|| server.stackActivations[i].reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION));
	const auto health = target->getAvailableHealth();
	EXPECT_FALSE(castActive()); // No stale request gains a third activation.
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(actor->casts.available(), charges - 2);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, DefendCompletionThenAcceptedSecondWindAllowsFirstCastAtSharedCoefficient)
{
	prepare();
	const auto full = mechanics()->adjustEffectValue(target);
	const auto charges = actor->casts.available();
	grantSecondWind(false);
	const auto before = target->getAvailableHealth();
	const int percent = output();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full * percent / 100);
	EXPECT_EQ(actor->casts.available(), charges - 1);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, OrdinaryNoGrantCastRemainsFullStrength)
{
	prepare();
	auto ordinary = mechanics();
	EXPECT_EQ(ordinary->getDirectCreatureActivationDamagePercent(), 100);
	const auto before = target->getAvailableHealth();
	const auto full = ordinary->adjustEffectValue(target);
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, AbsentLegacyCommandProfileDoesNotInventSecondWindReduction)
{
	useCommands = false; prepare();
	EXPECT_FALSE(battle()->battleUsesHeroCommands());
	EXPECT_EQ(mechanics()->getDirectCreatureActivationDamagePercent(), 100);
	const auto full = mechanics()->adjustEffectValue(target);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(castActive());
	EXPECT_EQ(before - target->getAvailableHealth(), full);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, OrdinaryHeroCommandContinuationDoesNotResetAlreadySpentCreatureCast)
{
	prepare();
	ASSERT_TRUE(castActive());
	ASSERT_TRUE(actor->castSpellThisTurn);
	const auto charges = actor->casts.available();
	BattleSetActiveStack continuation;
	continuation.battleID = BattleID(0);
	continuation.stack = actor->unitId();
	continuation.reason = BattleUnitTurnReason::HERO_COMMAND;
	gameHandler->sendAndApply(continuation);
	EXPECT_TRUE(actor->castSpellThisTurn);
	EXPECT_FALSE(actor->canCast());
	EXPECT_EQ(actor->casts.available(), charges);
	EXPECT_EQ(mechanics()->getDirectCreatureActivationDamagePercent(), 100);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, ExactActiveRecipientAndUnspentSnapshotAreRequired)
{
	prepare(); grantSecondWind(false);
	EXPECT_EQ(mechanics(spells::Mode::CREATURE_ACTIVE, window)->getDirectCreatureActivationDamagePercent(), 100);
	BattleSetActiveStack continuation;
	continuation.battleID = BattleID(0);
	continuation.stack = window->unitId();
	continuation.reason = BattleUnitTurnReason::ACTION_REJECTED;
	gameHandler->sendAndApply(continuation);
	EXPECT_EQ(mechanics()->getDirectCreatureActivationDamagePercent(), 100);
	continuation.stack = actor->unitId();
	gameHandler->sendAndApply(continuation);
	EXPECT_EQ(mechanics()->getDirectCreatureActivationDamagePercent(), output());
	ASSERT_TRUE(castActive());
	gameHandler->sendAndApply(continuation);
	EXPECT_EQ(mechanics()->getDirectCreatureActivationDamagePercent(), 100);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, HeroPassiveIndirectAndHealingDoNotInheritDirectCreatureOutput)
{
	prepare(); grantSecondWind(false);
	const auto full = mechanics()->adjustEffectValue(target);
	EXPECT_EQ(preview(mechanics().get(), true), full);
	for(const auto mode : {spells::Mode::PASSIVE, spells::Mode::ENCHANTER,
		spells::Mode::MAGIC_MIRROR, spells::Mode::SPELL_LIKE_ATTACK})
	{
		auto context = mechanics(mode);
		EXPECT_EQ(context->getDirectCreatureActivationDamagePercent(), 100);
		EXPECT_EQ(preview(context.get()), context->adjustEffectValue(target));
	}
	auto hero = mechanics(spells::Mode::HERO, attackerSideHero);
	EXPECT_EQ(hero->getDirectCreatureActivationDamagePercent(), 100);
	EXPECT_EQ(preview(hero.get()), hero->adjustEffectValue(target));

	auto injured = actor->acquireState();
	int64_t injury = 10;
	injured->damage(injury);
	UnitChanges change(actor->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = injured->save(); change.healthDelta = -10;
	BattleUnitsChanged changed;
	changed.battleID = BattleID(0); changed.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(changed);
	JsonNode heal;
	heal["heal"]["type"].String() = "core:heal";
	heal["heal"]["healLevel"].String() = "heal";
	heal["heal"]["healPower"].String() = "permanent";
	const auto effects = spells::effects::Effects::loadJson(heal, "core", "cure");
	battle::Target destination; destination.emplace_back(actor);
	auto context = mechanics();
	ASSERT_LT(context->getDirectCreatureActivationDamagePercent(), 100);
	EXPECT_EQ(effects.at("heal")->getHealthChange(context.get(), destination).hpDelta, 10);
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsSecondWindCreatureSpellTest, CopiedActiveGrantSurvivesLaterParentEndWithoutSiblingOrLiveMutation)
{
	prepare(); grantSecondWind(false);
	SecondWindSpellEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto spent = parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(spent);
	spent->secondWindActive = false;
	parent->setHeroOrderState(BattleSide::ATTACKER, spent);
	parent->nextTurn(window->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto * localActor = child->battleGetUnitByID(actor->unitId());
	const auto * localTarget = child->battleGetUnitByID(target->unitId());
	spells::BattleCast cast(child.get(), localActor, spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
	cast.setSpellLevel(MasteryLevel::ADVANCED);
	auto context = arrow.toSpell()->battleMechanics(&cast);
	ASSERT_EQ(context->getDirectCreatureActivationDamagePercent(), output());
	const auto before = target->getAvailableHealth();
	const auto full = context->adjustEffectValue(localTarget);
	battle::Target destination; destination.emplace_back(localTarget);
	context->castEval(child->getServerCallback(), destination);
	EXPECT_EQ(before - child->battleGetUnitByID(target->unitId())->getAvailableHealth(), full * output() / 100);
	EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(sibling->battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_FALSE(parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND)->secondWindActive);
	EXPECT_TRUE(sibling->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND)->secondWindActive);
	EXPECT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND)->secondWindActive);
}

TEST_F(NewHorizonsSecondWindCreatureSpellTest, DetachedSecondWindRenewsPriorCastAndUsesCopiedGrantWithoutParentLeaks)
{
	prepare();
	ASSERT_TRUE(castActive());
	ASSERT_TRUE(actor->castSpellThisTurn);
	const auto proposal = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::SECOND_WIND, {actor->unitId()});
	ASSERT_TRUE(proposal);
	SecondWindSpellEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto localOrder = *proposal; localOrder.secondWindActive = true;
	child->setHeroOrderState(BattleSide::ATTACKER, localOrder);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::HERO_COMMAND);
	const auto * localActor = child->battleGetUnitByID(actor->unitId());
	const auto * localTarget = child->battleGetUnitByID(target->unitId());
	EXPECT_TRUE(localActor->canCast());
	EXPECT_TRUE(actor->castSpellThisTurn);
	EXPECT_FALSE(parent->getForUpdate(actor->unitId())->canCast());
	EXPECT_FALSE(sibling->getForUpdate(actor->unitId())->canCast());
	const auto before = target->getAvailableHealth();
	const auto charges = actor->casts.available();
	const int percent = newHorizonsDivineMandate::crownSecondWindPercent(*attackerSideHero,
		localOrder, actor->unitId());
	spells::BattleCast cast(child.get(), localActor, spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
	cast.setSpellLevel(MasteryLevel::ADVANCED);
	auto context = arrow.toSpell()->battleMechanics(&cast);
	ASSERT_EQ(context->getDirectCreatureActivationDamagePercent(), percent);
	const auto full = context->adjustEffectValue(localTarget);
	battle::Target destination; destination.emplace_back(localTarget);
	context->castEval(child->getServerCallback(), destination);
	EXPECT_EQ(before - child->battleGetUnitByID(target->unitId())->getAvailableHealth(), full * percent / 100);
	EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(sibling->battleGetUnitByID(target->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(actor->casts.available(), charges);
}
#endif
