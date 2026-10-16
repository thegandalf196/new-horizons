/*
 * NewHorizonsPlagueMovementCompletionAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

#include <map>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
class PlagueMoveEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PlagueMoveEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PlagueMoveCallback final : public CBattleCallback
{
public:
	PlagueMoveCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction &) override {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};

class NewHorizonsPlagueMovementCompletionAITest : public HeroCommandFixture
{
protected:
	CStack * mover = nullptr;
	CStack * spreadTarget = nullptr;
	std::shared_ptr<PlagueMoveEnvironment> environment;
	SpellID plague() const { return SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID))); }
	const BattleHex destination{5, 5};

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		for(auto & chance : rules["morale"]["goodChance"].Vector())
			chance.Integer() = 0;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(bool infected = true)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(plague());
		setTestSpellPointTotal(defenderSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		mover = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(4, 5), 1000);
		spreadTarget = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(6, 5), 1000);
		ASSERT_NE(mover, nullptr);
		ASSERT_NE(spreadTarget, nullptr);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(2, 2), 1000), nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(13, 2), 1000), nullptr);
		mover->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1000,
			BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), mover);
		ASSERT_TRUE(battle()->battleGetAvailableHexes(mover, false).contains(destination));
		if(infected)
		{
			ASSERT_TRUE(castOn(defenderSideHero, plague(), mover));
		}
		environment = std::make_shared<PlagueMoveEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> model()
	{
		auto result = std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		// Materialize every unit before any live action; never borrow the oracle.
		for(const auto * unit : battle()->battleGetAllUnits(true))
			EXPECT_NE(result->getForUpdate(unit->unitId()).get(), unit);
		return result;
	}

	BattleEvaluator::MovementCompletionProjection project(const std::shared_ptr<HypotheticBattle> & parent,
		PlayerColor perspective = PlayerColor(0))
	{
		return BattleEvaluator::projectOrdinaryMoveCompletion(environment.get(), parent,
			BattleAction::makeMove(mover, destination), perspective);
	}

	void setHealth(int64_t desired)
	{
		auto state = mover->acquireState();
		int64_t damage = mover->getAvailableHealth() - desired;
		state->damage(damage);
		UnitChanges change(mover->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
		ASSERT_EQ(mover->getAvailableHealth(), desired);
	}

	bool actualMove()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMove(mover, destination));
	}

	BattleAction chosenMove(std::pair<float, float> & value)
	{
		auto callback = std::make_shared<PlagueMoveCallback>();
		callback->onBattleStarted(battle());
		BattleEvaluator evaluator(environment, callback, mover, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		PotentialTargets targets(mover, damageCache, model());
		EXPECT_TRUE(targets.possibleAttacks.empty());
		return evaluator.moveOrAttack(mover, destination, targets, &value);
	}
	DamageCache damageCache;
};
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, OrdinaryMoveTicksOnceAtDestinationAndMatchesRealSpread)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = model();
	const auto before = mover->getAvailableHealth();
	const auto projection = project(parent);
	ASSERT_TRUE(projection.arrivedAlive);
	ASSERT_NE(projection.battle, nullptr);
	ASSERT_EQ(projection.battle->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(projection.battle->battleGetUnitByID(mover->unitId())->getPosition(), destination);
	EXPECT_TRUE(newHorizonsPlague::hasPlague(projection.battle->battleGetUnitByID(spreadTarget->unitId())));
	EXPECT_LT(projection.value.first, 0);
	EXPECT_LT(projection.value.second, 0);
	std::pair<float, float> chosenValue;
	const auto chosen = chosenMove(chosenValue);
	ASSERT_EQ(chosen.actionType, EActionType::WALK);
	EXPECT_FLOAT_EQ(chosenValue.first, projection.value.first);
	EXPECT_FLOAT_EQ(chosenValue.second, projection.value.second);
	ASSERT_TRUE(actualMove());
	EXPECT_EQ(mover->getPosition(), destination);
	EXPECT_EQ(projection.battle->battleGetUnitByID(mover->unitId())->getAvailableHealth(), mover->getAvailableHealth());
	EXPECT_FLOAT_EQ(projection.value.first, mover->getAvailableHealth() - before);
	EXPECT_TRUE(newHorizonsPlague::hasPlague(spreadTarget));
	newHorizonsPlague::completeActivation(*projection.battle, *projection.battle->getServerCallback(), mover->unitId());
	EXPECT_EQ(projection.battle->getProjectedPlagueHits().size(), 1u);
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, HostileLockMoveTicksAndSpreadsBeforeSealExpiryThenCountsDownOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); // Real paid Plague, not a fabricated marker.
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	// The infection captured its damage at SP100. SP0 gives the real seal one round.
	defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID seal(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	defenderSideHero->addSpellToSpellbook(seal);
	advanceRound(); // Existing fixture round packet refreshes the paid hero allowance.
	ASSERT_EQ(battle()->battleActiveUnit(), mover);
	const auto manaBeforeSeal = defenderSideHero->getManaAvailable();
	ASSERT_TRUE(castOn(defenderSideHero, seal, mover));
	EXPECT_LT(defenderSideHero->getManaAvailable(), manaBeforeSeal);
	const auto marker = [&](const battle::Unit * unit) -> std::shared_ptr<const Bonus>
	{
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(plague())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? nullptr : bonuses->front();
	};
	const auto infection = marker(mover);
	ASSERT_NE(infection, nullptr);
	ASSERT_NE(infection->parameters, nullptr);
	ASSERT_EQ(infection->turnsRemain, 3);
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*mover));
	ASSERT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*mover), std::optional<bool>(false));
	ASSERT_TRUE(newHorizonsSorcery::durationPausedBySpellLock(*mover, *infection));
	const auto seals = mover->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(seal)).And(Selector::type()(BonusType::NONE)));
	ASSERT_EQ(seals->size(), 1u);
	ASSERT_EQ(seals->front()->turnsRemain, 1);
	const auto round = battle()->battleGetRound();
	const auto initialParameters = infection->parameters->toCustom<JsonNode>();
	ASSERT_LT(initialParameters["lastProcessedRound"].Integer(), round);
	ASSERT_EQ(initialParameters["casterSide"].Integer(), static_cast<int>(BattleSide::DEFENDER));
	// Independent integral HP oracle: this ordinary Peasant has no mitigation,
	// and the caster has no incidental Plague damage bonus. Spell Lock resistance
	// blocks new spells, but does not reduce an already-captured Plague tick.
	const int64_t tick = infection->val;
	ASSERT_GT(tick, 0);
	ASSERT_EQ(defenderSideHero->getSpellBonus(plague().toSpell(), tick, mover), tick);
	ASSERT_EQ(mover->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), 0);
	ASSERT_EQ(mover->getGuardianSpiritHitPoints(), 0);
	ASSERT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(mover), 0);
	ASSERT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(mover), 0);
	const auto health = mover->getAvailableHealth();
	ASSERT_GT(health, 2 * tick);
	ASSERT_FALSE(newHorizonsPlague::hasPlague(spreadTarget));
	auto parent = model();
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	std::map<uint32_t, JsonNode> parentBefore;
	std::map<uint32_t, JsonNode> siblingBefore;
	std::map<uint32_t, JsonNode> liveBefore;
	for(const auto * unit : battle()->battleGetAllUnits(true))
	{
		parentBefore.emplace(unit->unitId(), parent->getForUpdate(unit->unitId())->save());
		siblingBefore.emplace(unit->unitId(), sibling->getForUpdate(unit->unitId())->save());
		liveBefore.emplace(unit->unitId(), unit->acquireState()->save());
	}
	const auto first = project(parent);
	ASSERT_TRUE(first.arrivedAlive);
	ASSERT_NE(first.battle, nullptr);
	ASSERT_EQ(first.battle->getMagicRules(), parent->getMagicRules());
	auto projectedMover = first.battle->getForUpdate(mover->unitId());
	ASSERT_EQ(first.battle->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(projectedMover->getAvailableHealth(), health - tick);
	EXPECT_FLOAT_EQ(first.value.first, -static_cast<float>(tick));
	ASSERT_NE(marker(projectedMover.get()), nullptr);
	EXPECT_EQ(marker(projectedMover.get())->turnsRemain, 3);
	EXPECT_EQ(marker(projectedMover.get())->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round);
	EXPECT_EQ(marker(projectedMover.get())->parameters->toCustom<JsonNode>()["spreadAttempts"].Integer(),
		initialParameters["spreadAttempts"].Integer() + 1);
	ASSERT_NE(marker(first.battle->battleGetUnitByID(spreadTarget->unitId())), nullptr);
	EXPECT_EQ(marker(first.battle->battleGetUnitByID(spreadTarget->unitId()))->val, tick);
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*projectedMover));
	newHorizonsPlague::completeActivation(*first.battle, *first.battle->getServerCallback(), mover->unitId());
	EXPECT_EQ(first.battle->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(projectedMover->getAvailableHealth(), health - tick);
	for(const auto & [id, snapshot] : liveBefore)
		EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), snapshot);
	ASSERT_TRUE(actualMove());
	EXPECT_EQ(mover->getPosition(), destination);
	EXPECT_EQ(mover->getAvailableHealth(), health - tick);
	ASSERT_NE(marker(mover), nullptr);
	EXPECT_EQ(marker(mover)->turnsRemain, 3);
	EXPECT_EQ(marker(mover)->parameters->toCustom<JsonNode>(), marker(projectedMover.get())->parameters->toCustom<JsonNode>());
	ASSERT_NE(marker(spreadTarget), nullptr);
	EXPECT_EQ(marker(spreadTarget)->toJsonNode(), marker(first.battle->battleGetUnitByID(spreadTarget->unitId()))->toJsonNode());

	// Drive the real ordinary queue to its next normal round with accepted
	// Defends, matching the existing delayed-move fixture. Never assign activeStack.
	for(size_t answered = 0; answered < battle()->stacks.size() + 1
		&& (battle()->battleGetRound() == round || battle()->battleActiveUnit() != mover); ++answered)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active, mover);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleGetRound(), round + 1);
	ASSERT_EQ(battle()->battleActiveUnit(), mover);
	ASSERT_FALSE(newHorizonsSorcery::isSpellLocked(*mover));
	ASSERT_NE(marker(mover), nullptr);
	EXPECT_EQ(marker(mover)->turnsRemain, 3);
	EXPECT_EQ(mover->getAvailableHealth(), health - tick);
	std::map<uint32_t, JsonNode> liveBeforeSecondProjection;
	for(const auto * unit : battle()->battleGetAllUnits(true))
		liveBeforeSecondProjection.emplace(unit->unitId(), unit->acquireState()->save());
	first.battle->nextRound();
	first.battle->nextTurn(mover->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_FALSE(newHorizonsSorcery::isSpellLocked(*projectedMover));
	ASSERT_NE(marker(projectedMover.get()), nullptr);
	EXPECT_EQ(marker(projectedMover.get())->turnsRemain, 3);
	EXPECT_EQ(projectedMover->getAvailableHealth(), health - tick);
	const auto back = BattleAction::makeMove(mover, BattleHex(4, 5));
	const auto second = BattleEvaluator::projectOrdinaryMoveCompletion(environment.get(), first.battle,
		back, PlayerColor(0));
	ASSERT_TRUE(second.arrivedAlive);
	ASSERT_EQ(second.battle->getProjectedPlagueHits().size(), 2u);
	const auto * secondMover = second.battle->battleGetUnitByID(mover->unitId());
	EXPECT_EQ(secondMover->getAvailableHealth(), health - 2 * tick);
	EXPECT_FLOAT_EQ(second.value.first, -static_cast<float>(tick));
	ASSERT_NE(marker(secondMover), nullptr);
	EXPECT_EQ(marker(secondMover)->turnsRemain, 2);
	EXPECT_EQ(marker(secondMover)->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round + 1);
	EXPECT_EQ(marker(secondMover)->parameters->toCustom<JsonNode>()["spreadAttempts"].Integer(),
		initialParameters["spreadAttempts"].Integer() + 2);
	newHorizonsPlague::completeActivation(*second.battle, *second.battle->getServerCallback(), mover->unitId());
	EXPECT_EQ(second.battle->getProjectedPlagueHits().size(), 2u);
	EXPECT_EQ(secondMover->getAvailableHealth(), health - 2 * tick);
	EXPECT_EQ(marker(secondMover)->turnsRemain, 2);
	for(const auto & [id, snapshot] : liveBeforeSecondProjection)
		EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), snapshot);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), back));
	EXPECT_EQ(mover->getAvailableHealth(), secondMover->getAvailableHealth());
	ASSERT_NE(marker(mover), nullptr);
	EXPECT_EQ(marker(mover)->toJsonNode(), marker(secondMover)->toJsonNode());
	for(const auto & [id, snapshot] : parentBefore)
		EXPECT_EQ(parent->getForUpdate(id)->save(), snapshot);
	for(const auto & [id, snapshot] : siblingBefore)
		EXPECT_EQ(sibling->getForUpdate(id)->save(), snapshot);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
	EXPECT_TRUE(sibling->getProjectedPlagueHits().empty());
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, LethalCompletionRemainsLegalAndPropagatesAfterArrival)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHealth(1));
	auto parent = model();
	const auto projection = project(parent);
	ASSERT_TRUE(projection.arrivedAlive);
	ASSERT_NE(projection.battle, nullptr);
	EXPECT_FALSE(projection.battle->battleGetUnitByID(mover->unitId())->alive());
	EXPECT_FLOAT_EQ(projection.value.first, -1);
	EXPECT_TRUE(newHorizonsPlague::hasPlague(projection.battle->battleGetUnitByID(spreadTarget->unitId())));
	std::pair<float, float> value;
	EXPECT_EQ(chosenMove(value).actionType, EActionType::WALK);
	EXPECT_FLOAT_EQ(value.first, -1);
	ASSERT_TRUE(actualMove());
	EXPECT_FALSE(mover->alive());
	EXPECT_TRUE(newHorizonsPlague::hasPlague(spreadTarget));
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, ReceiptAllegianceDeterminesSignedRiskWithoutMutatingParentOrSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = model();
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto live = mover->save();
	const auto original = parent->getForUpdate(mover->unitId())->save();
	const auto siblingState = sibling->getForUpdate(mover->unitId())->save();
	const auto ours = project(parent, PlayerColor(0));
	const auto theirs = project(parent, PlayerColor(1));
	ASSERT_EQ(ours.battle->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(ours.battle->getProjectedPlagueHits().front().targetController, PlayerColor(0));
	EXPECT_FLOAT_EQ(ours.value.first, -theirs.value.first);
	EXPECT_FLOAT_EQ(ours.value.second, -theirs.value.second);
	EXPECT_EQ(mover->save(), live);
	EXPECT_EQ(parent->getForUpdate(mover->unitId())->save(), original);
	EXPECT_EQ(sibling->getForUpdate(mover->unitId())->save(), siblingState);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
	EXPECT_TRUE(sibling->getProjectedPlagueHits().empty());
	EXPECT_FALSE(newHorizonsPlague::hasPlague(parent->battleGetUnitByID(spreadTarget->unitId())));
	EXPECT_FALSE(newHorizonsPlague::hasPlague(sibling->battleGetUnitByID(spreadTarget->unitId())));
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, WaitSpellAndPursuitContinuationCannotCompleteOrdinaryMove)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = model();
	const auto before = parent->getForUpdate(mover->unitId())->save();
	const auto wait = BattleEvaluator::projectOrdinaryMoveCompletion(environment.get(), parent,
		BattleAction::makeWait(mover), PlayerColor(0));
	EXPECT_EQ(wait.battle, nullptr);
	BattleAction spell;
	spell.actionType = EActionType::MONSTER_SPELL;
	spell.stackNumber = mover->unitId();
	EXPECT_EQ(BattleEvaluator::projectOrdinaryMoveCompletion(environment.get(), parent, spell, PlayerColor(0)).battle, nullptr);
	parent->getForUpdate(mover->unitId())->pursuitMovementRemaining = 1;
	EXPECT_EQ(project(parent).battle, nullptr);
	parent->getForUpdate(mover->unitId())->pursuitMovementRemaining = 0;
	EXPECT_EQ(parent->getForUpdate(mover->unitId())->save(), before);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, UninfectedMoveHasZeroCompletionDebitAndKeepsOrdinaryOutcome)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto parent = model();
	const auto before = mover->getAvailableHealth();
	const auto projection = project(parent);
	ASSERT_TRUE(projection.arrivedAlive);
	EXPECT_FLOAT_EQ(projection.value.first, 0);
	EXPECT_FLOAT_EQ(projection.value.second, 0);
	EXPECT_TRUE(projection.battle->getProjectedPlagueHits().empty());
	ASSERT_TRUE(actualMove());
	EXPECT_EQ(mover->getAvailableHealth(), before);
	EXPECT_EQ(mover->getPosition(), destination);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(spreadTarget));
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, ActualTimeStopPreventsMovementProjectionAndCompletion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto stop = SpellID(SpellID::decode("new-horizons:timeStop"));
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	defenderSideHero->addSpellToSpellbook(stop);
	advanceRound();
	ASSERT_TRUE(castOn(defenderSideHero, stop, mover));
	ASSERT_TRUE(mover->isTimeStopped());
	auto parent = model();
	const auto before = mover->save();
	EXPECT_EQ(project(parent).battle, nullptr);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
	EXPECT_EQ(mover->save(), before);
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, InfectedDelayedMoveSelectionClosesTimingAndProjectsCachedRouteOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto healthBeforeWait = mover->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeWait(mover)));
	// Reach the existing delayed slot through accepted ordinary actions, not a
	// raw active-stack assignment or a fabricated waited-state flag.
	for(size_t answered = 0; answered < battle()->stacks.size() + 1
		&& battle()->battleActiveUnit() != mover; ++answered)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), mover);
	ASSERT_TRUE(mover->acquireState()->waitedThisTurn);
	ASSERT_EQ(mover->getAvailableHealth(), healthBeforeWait);
	const auto beforeSelection = mover->save();
	const auto allyBefore = spreadTarget->save();
	auto callback = std::make_shared<PlagueMoveCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, mover, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	BattleEvaluator::SelectionTiming timing;
	const auto chosen = evaluator.selectStackAction(mover, &timing);
	ASSERT_EQ(chosen.actionType, EActionType::WALK);
	EXPECT_EQ(timing.movementDetails.findMove.calls, 1u);
	EXPECT_EQ(timing.movementDetails.goTowards.calls, 1u)
		<< "The cached selected action must not execute a second route preview";
	EXPECT_GE(timing.movementSearch.totalMicroseconds,
		timing.movementDetails.findMove.totalMicroseconds + timing.movementDetails.goTowards.totalMicroseconds);
	EXPECT_EQ(mover->save(), beforeSelection);
	EXPECT_EQ(spreadTarget->save(), allyBefore);
	auto parent = model();
	const auto projection = BattleEvaluator::projectOrdinaryMoveCompletion(environment.get(), parent,
		chosen, PlayerColor(0));
	ASSERT_TRUE(projection.arrivedAlive);
	ASSERT_EQ(projection.battle->getProjectedPlagueHits().size(), 1u);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), chosen));
	EXPECT_FLOAT_EQ(projection.value.first, mover->getAvailableHealth() - healthBeforeWait);
	EXPECT_LT(mover->getAvailableHealth(), healthBeforeWait);
	EXPECT_EQ(projection.battle->battleGetUnitByID(mover->unitId())->getAvailableHealth(), mover->getAvailableHealth());
}

TEST_F(NewHorizonsPlagueMovementCompletionAITest, FrozenApplicationRejectsOrdinaryMoveWithoutConsumingForfeitOrTick)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(mover->unitId(), std::vector<Bonus>{
		newHorizonsFrozen::makeFrozenMarker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
	gameHandler->sendAndApply(effect);
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*mover));
	ASSERT_FALSE(mover->canMove());
	auto parent = model();
	const auto before = parent->getForUpdate(mover->unitId())->save();
	EXPECT_EQ(project(parent).battle, nullptr);
	EXPECT_EQ(parent->getForUpdate(mover->unitId())->save(), before);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*parent->battleGetUnitByID(mover->unitId())));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(parent->battleGetUnitByID(mover->unitId())));
}
