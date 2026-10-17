/*
 * HeroOrderEffectsProjectionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/HeroOrderEffects.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

using namespace heroQueueTest;

class HeroOrderEffectsProjectionTest : public HeroQueueSelectorTest
{
protected:
	std::optional<heroCommands::PreparedOrderEffects> effects;
	std::optional<HypotheticBattle::ProjectedOrderAllowance> allowance;
	static constexpr auto side = BattleSide::ATTACKER;

	void perk(const char * skill, const char * id, MasteryLevel::Type rank)
	{
		const std::map<std::string, std::pair<const char *, const char *>> predecessors{
			{"new-horizons:offense", {"new-horizons:offense.shockAssault", nullptr}},
			{"new-horizons:discipline", {"new-horizons:discipline.inspirationalLeader", nullptr}},
			{"new-horizons:warcasting", {"new-horizons:warcasting.martialChanneling", nullptr}},
			{"new-horizons:command", {"new-horizons:command.aggressiveCommander", "new-horizons:command.veteranCommander"}},
			{"new-horizons:divineMandate", {"new-horizons:divineMandate.sharedPurpose", "new-horizons:divineMandate.divineDiscipline"}}};
		const auto & prior = predecessors.at(skill);
		if(rank > MasteryLevel::BASIC)
			ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, prior.first, MasteryLevel::BASIC));
		if(rank > MasteryLevel::ADVANCED)
			ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, prior.second, MasteryLevel::ADVANCED));
		ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, id, rank));
	}

	static bool issuedGrant(const Bonus * bonus)
	{
		const auto vengeance = newHorizonsOffense::vengeanceRetaliationBonus();
		return newHorizonsEnchantedCommand::isMoraleBonus(bonus)
			|| newHorizonsDiscipline::isHoldFastMoraleFloorBonus(bonus)
			|| newHorizonsDivineMandate::isSharedPurposeMoraleBonus(bonus)
			|| (bonus && bonus->source == vengeance.source && bonus->sid == vengeance.sid
				&& bonus->type == vengeance.type && bonus->duration == vengeance.duration);
	}

	std::vector<Bonus> grants(const battle::Unit * unit)
	{
		std::vector<Bonus> records;
		const auto selected = unit->getAllBonuses(CSelector(issuedGrant));
		for(const auto & bonus : *selected)
			records.push_back(*bonus);
		return records;
	}

	void stage()
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}

	void begin(HeroCommand command, const std::vector<uint32_t> & targets = std::vector<uint32_t>())
	{
		if(!child)
			ASSERT_NO_FATAL_FAILURE(stage());
		const auto outside = observation();
		allowance = child->prepareHeroOrderAllowance(side);
		ASSERT_TRUE(allowance);
		effects = child->beginProjectedHeroOrder(side, command, targets, *allowance);
		ASSERT_TRUE(effects);
		EXPECT_EQ(observation(), outside);
	}

	void project(HeroCommand command, const std::vector<uint32_t> & targets = std::vector<uint32_t>())
	{
		ASSERT_NO_FATAL_FAILURE(begin(command, targets));
		const auto outside = observation();
		ASSERT_TRUE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
		EXPECT_EQ(child->getHeroOrderState(side, command), std::optional<HeroOrderState>(effects->order));
		EXPECT_FALSE(parent->getHeroOrderState(side, command));
		EXPECT_FALSE(sibling->getHeroOrderState(side, command));
		EXPECT_EQ(observation(), outside); // Native state, packets, mana and RNG unchanged.
	}

	std::string prefix(const HypotheticBattle & value)
	{
		auto result = state(value); // Units/grants, ledger, Seize and saved Orders.
		for(const auto owner : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			result += wire(value.getWarcastingState(owner)) + wire(value.getFocusFireState(owner))
				+ wire(value.getDoubleCommandState(owner)) + wire(value.getPreCombatOrderState(owner));
		return result;
	}

	void reject(const heroCommands::PreparedOrderEffects & altered)
	{
		const auto before = prefix(*child);
		const auto outside = observation();
		bool accepted = true;
		EXPECT_NO_THROW(accepted = child->projectAcceptedHeroOrder(side, altered, *allowance));
		EXPECT_FALSE(accepted);
		EXPECT_EQ(prefix(*child), before);
		EXPECT_EQ(observation(), outside);
		// Rejection did not damage the legitimate bound transaction.
		ASSERT_TRUE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
	}

	void nativeParity(HeroCommand command, const std::vector<uint32_t> & targets = std::vector<uint32_t>())
	{
		ASSERT_TRUE(child);
		const auto projectedOrders = wire(child->getHeroOrderStates(side));
		const auto projectedMark = wire(child->getFocusFireState(side));
		const auto projectedAllowance = wire(child->getHeroActionAllowances(side));
		std::vector<std::pair<uint32_t, std::vector<Bonus>>> bonuses;
		for(const auto * unit : child->battleGetAllUnits(true))
			bonuses.emplace_back(unit->unitId(), grants(unit));
		auto action = BattleAction::makeHeroCommand(side, command);
		for(const auto id : targets)
			action.target.push_back({static_cast<int32_t>(id), BattleHex::INVALID});
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(wire(battle()->getHeroOrderStates(side)), projectedOrders);
		EXPECT_EQ(wire(battle()->getFocusFireState(side)), projectedMark);
		EXPECT_EQ(wire(battle()->getHeroActionAllowances(side)), projectedAllowance);
		for(const auto & [id, expected] : bonuses)
			EXPECT_EQ(wire(grants(battle()->battleGetUnitByID(id))), wire(expected));
	}
};

TEST_F(HeroOrderEffectsProjectionTest, ChargeInstallsSavedStateAndSpendsOnlyAdmittedAllowance)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::CHARGE));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::CHARGE));
}

TEST_F(HeroOrderEffectsProjectionTest, HoldFastUsesIssueAnchorsAndNativeGrant)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(perk("new-horizons:discipline", "new-horizons:discipline.holdFast", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::HOLD_THE_LINE));
	EXPECT_TRUE(child->battleGetUnitByID(actor->unitId())->hasBonus(newHorizonsDiscipline::holdFastMoraleFloorBonusSelector()));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::HOLD_THE_LINE));
}

TEST_F(HeroOrderEffectsProjectionTest, FocusFireInstallsDedicatedMarkAndCapturedRecipientSet)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto * shooter = addStack(side, creatureByName("core:archer"), BattleHex(5, 7), 10);
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::FOCUS_FIRE, {enemy->unitId()}));
	ASSERT_TRUE(child->getFocusFireState(side));
	EXPECT_TRUE(vstd::contains(child->getFocusFireState(side)->recipientUnitIds, shooter->unitId()));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::FOCUS_FIRE, {enemy->unitId()}));
}

TEST_F(HeroOrderEffectsProjectionTest, VengeanceInstallsExactlyOneNativeRetaliationGrant)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(perk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE, MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::RIPOSTE));
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(child->battleGetUnitByID(actor->unitId())));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::RIPOSTE));
}

TEST_F(HeroOrderEffectsProjectionTest, WarcastingSnapshotPrecedesCompletionAndGrantsEnchantedCommand)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(perk("new-horizons:warcasting", "new-horizons:warcasting.enchantedCommand", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto healthy = actor->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(injure(actor, healthy - 1));
	ASSERT_EQ(actor->getAvailableHealth(), healthy - 1);
	ASSERT_TRUE(cast(attackerSideHero, SpellID::CURE, actor));
	EXPECT_EQ(actor->getAvailableHealth(), healthy);
	ASSERT_NO_FATAL_FAILURE(advanceRound());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::HOLD_THE_LINE));
	EXPECT_GT(effects->order.warcastingBonusPercent, 0);
	EXPECT_TRUE(child->battleGetUnitByID(actor->unitId())->hasBonus(newHorizonsEnchantedCommand::moraleBonusSelector()));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::HOLD_THE_LINE));
}

TEST_F(HeroOrderEffectsProjectionTest, InvalidTargetCannotCommitOrInstallOrderEffects)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	environment = std::make_shared<QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	child = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto before = wire(child->getHeroActionAllowances(side));
	EXPECT_FALSE(heroCommands::prepareOrderEffects(*child, side, HeroCommand::FOCUS_FIRE, {active->unitId()}));
	EXPECT_EQ(wire(child->getHeroActionAllowances(side)), before);
	EXPECT_TRUE(child->getHeroOrderStates(side).empty());
}

TEST_F(HeroOrderEffectsProjectionTest, ReplayingAcceptedAllowanceCannotDuplicateEffects)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::CHARGE));
	const auto ledger = wire(child->getHeroActionAllowances(side));
	const auto orders = wire(child->getHeroOrderStates(side));
	EXPECT_FALSE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
	EXPECT_EQ(wire(child->getHeroActionAllowances(side)), ledger);
	EXPECT_EQ(wire(child->getHeroOrderStates(side)), orders);
}

TEST_F(HeroOrderEffectsProjectionTest, ImmediateSecondWindUsesExistingActivationOwnerAndActualActor)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	actor->movedThisRound = true;
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::SECOND_WIND, {actor->unitId()}));
	const auto result = child->projectOrderContinuation(side, *allowance, *effects);
	EXPECT_EQ(result.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::SECOND_WIND_ACTOR);
	EXPECT_EQ(result.actor, actor->unitId());
	ASSERT_TRUE(child->getHeroOrderState(side, HeroCommand::SECOND_WIND));
	EXPECT_TRUE(child->getHeroOrderState(side, HeroCommand::SECOND_WIND)->secondWindActive);
	EXPECT_EQ(child->getActiveStackID(), actor->unitId());
}

TEST_F(HeroOrderEffectsProjectionTest, DoubleCommandChoiceRemainsUnvaluedWithinSharedUnitHorizon)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(perk("new-horizons:command", "new-horizons:command.doubleCommand", MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::CHARGE));
	const auto before = wire(child->getDoubleCommandState(side));
	const auto result = child->projectOrderContinuation(side, *allowance, *effects);
	EXPECT_EQ(result.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::CURRENT_ACTOR);
	EXPECT_EQ(result.actor, active->unitId());
	EXPECT_TRUE(result.futureHeroChoicesUnvalued);
	EXPECT_TRUE(child->getDoubleCommandState(side).orderPending());
	EXPECT_EQ(wire(child->getDoubleCommandState(side)), before);
}

TEST_F(HeroOrderEffectsProjectionTest, CrownAndSharedPurposeKeepOriginalPairRecipientIntersection)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(perk("new-horizons:divineMandate", "new-horizons:divineMandate.crownAndAltar", MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::BLESS, "new-horizons:lightMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, actor));
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(allowance->action.receipt.source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	EXPECT_TRUE(vstd::contains(effects->order.crownAndAltarRecipientUnitIds, actor->unitId()));
	EXPECT_FALSE(vstd::contains(effects->order.crownAndAltarRecipientUnitIds, waiter->unitId()));
	EXPECT_TRUE(child->battleGetUnitByID(actor->unitId())->hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus)));
	EXPECT_FALSE(child->battleGetUnitByID(waiter->unitId())->hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus)));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::HOLD_THE_LINE));
}

TEST_F(HeroOrderEffectsProjectionTest, ProtectInstallsOrderedPairWithoutASecondPayment)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto * ward = addStack(side, creatureByName("core:pikeman"), BattleHex(2, 5), 10);
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::PROTECT, {active->unitId(), ward->unitId()}));
	EXPECT_EQ(effects->order.primaryTargetUnitId, active->unitId());
	EXPECT_EQ(effects->order.secondaryTargetUnitId, ward->unitId());
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::PROTECT, {active->unitId(), ward->unitId()}));
}

TEST_F(HeroOrderEffectsProjectionTest, FlankInstallsInitialSideLedger)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::FLANK, {enemy->unitId()}));
	ASSERT_EQ(effects->order.flankTargets.size(), 1);
	EXPECT_EQ(effects->order.flankTargets.front().sideMask, 0);
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::FLANK, {enemy->unitId()}));
}

TEST_F(HeroOrderEffectsProjectionTest, BraceInstallsNativeRoundSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::BRACE));
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::BRACE));
}

TEST_F(HeroOrderEffectsProjectionTest, DoubleCommandFollowupPreservesPrimaryOrderAndGrantIdentity)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(perk("new-horizons:command", "new-horizons:command.doubleCommand", MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(child->getHeroOrderStates(side).size(), 2);
	EXPECT_EQ(allowance->action.receipt.source, HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND);
	ASSERT_NO_FATAL_FAILURE(nativeParity(HeroCommand::HOLD_THE_LINE));
}

TEST_F(HeroOrderEffectsProjectionTest, SecondWindRunsKnownActivationAndLabelsUnvaluedObstacleOutcomes)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	actor->movedThisRound = true;
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->uniqueID = 230;
	obstacle->ID = SpellID::FORCE_FIELD;
	obstacle->trigger = SpellID::NONE;
	obstacle->pos = BattleHex(8, 0);
	obstacle->customSize.insert(obstacle->pos);
	obstacle->turnsRemaining = 2;
	obstacle->casterSide = side;
	obstacle->nativeVisible = false;
	battle()->obstacles.push_back(obstacle);
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::SECOND_WIND, {actor->unitId()}));
	const auto result = child->projectOrderContinuation(side, *allowance, *effects);
	EXPECT_EQ(result.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::SECOND_WIND_ACTOR);
	EXPECT_EQ(result.actor, actor->unitId());
	EXPECT_EQ(child->getActiveStackID(), actor->unitId());
	EXPECT_TRUE(result.activationEffectsUnvalued);
	ASSERT_TRUE(child->getHeroOrderState(side, HeroCommand::SECOND_WIND));
	EXPECT_TRUE(child->getHeroOrderState(side, HeroCommand::SECOND_WIND)->secondWindActive);
}

TEST_F(HeroOrderEffectsProjectionTest, RandomOrderOutcomesRemainExplicitlyUnvaluedWithoutRolling)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:luck", "new-horizons:luck.secondChance", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:luck", "new-horizons:luck.luckyRecovery", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(project(HeroCommand::CHARGE));
	const auto outside = observation();
	const auto result = child->projectOrderContinuation(side, *allowance, *effects);
	EXPECT_EQ(result.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::CURRENT_ACTOR);
	EXPECT_TRUE(result.orderRandomOutcomesUnvalued);
	EXPECT_FALSE(effects->order.heroLuck);
	EXPECT_EQ(observation(), outside);
}

TEST_F(HeroOrderEffectsProjectionTest, AlteredFocusFireOrderTargetRejectsBeforeAnyMutation)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	addStack(side, creatureByName("core:archer"), BattleHex(5, 7), 10);
	const auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 7), 10);
	ASSERT_NO_FATAL_FAILURE(begin(HeroCommand::FOCUS_FIRE, {enemy->unitId()}));
	auto altered = *effects;
	altered.order.primaryTargetUnitId = other->unitId();
	ASSERT_NO_THROW(altered.order.validateShape());
	ASSERT_NO_THROW(altered.focusFire->validateShape());
	ASSERT_NO_FATAL_FAILURE(reject(altered));
}

TEST_F(HeroOrderEffectsProjectionTest, MalformedOrderRejectsBeforeLedgerSeizeOrWarcastingMutation)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(begin(HeroCommand::HOLD_THE_LINE));
	auto altered = *effects;
	altered.order.warcastingBonusPercent = 101;
	ASSERT_NO_FATAL_FAILURE(reject(altered));
}

TEST_F(HeroOrderEffectsProjectionTest, MalformedMarkRejectsBeforeInstallingAnyOrder)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	addStack(side, creatureByName("core:archer"), BattleHex(5, 7), 10);
	ASSERT_NO_FATAL_FAILURE(begin(HeroCommand::FOCUS_FIRE, {enemy->unitId()}));
	auto altered = *effects;
	altered.focusFire->recipientUnitIds.clear();
	ASSERT_NO_FATAL_FAILURE(reject(altered));
}

TEST_F(HeroOrderEffectsProjectionTest, SameRoundStaleAnchorSnapshotCannotReplaceBoundPreCleanupCapture)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(stage());
	const auto stale = heroCommands::prepareOrderEffects(*child, side, HeroCommand::HOLD_THE_LINE, {});
	ASSERT_TRUE(stale);
	child->getForUpdate(actor->unitId())->setPosition(BattleHex(6, 5));
	ASSERT_NO_FATAL_FAILURE(begin(HeroCommand::HOLD_THE_LINE));
	EXPECT_NE(stale->order.anchors, effects->order.anchors);
	ASSERT_NO_FATAL_FAILURE(reject(*stale));
}

TEST_F(HeroOrderEffectsProjectionTest, UnboundSnapshotCannotClaimGenericAccountingAdmission)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(stage());
	effects = heroCommands::prepareOrderEffects(*child, side, HeroCommand::CHARGE, {});
	allowance = child->prepareHeroOrderAllowance(side);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(allowance);
	ASSERT_TRUE(child->beginProjectedHeroAction(side, *allowance));
	const auto before = prefix(*child);
	const auto outside = observation();
	EXPECT_FALSE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
	EXPECT_EQ(prefix(*child), before);
	EXPECT_EQ(observation(), outside);
}
