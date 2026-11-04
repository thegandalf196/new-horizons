/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/battle/NewHorizonsDemonicGating.h"
#include "../../../lib/battle/DemonicGateArrival.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/StackLocation.h"

namespace
{
class NewHorizonsTemporaryDemonicGatingTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:calh")));
	}
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
		startGame();
		const auto imp = CreatureID(CreatureID::decode("core:imp"));
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), imp.toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id, SlotID(0)), 12, ChangeValueMode::ABSOLUTE));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		auto & perks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		perks.rules["demonicGatingPolicy"]["version"].Integer() = 2;
		perks.selected.clear();
		attackerSideHero->setDemonicReserve({});
		startBattle();
		beginCombat();
	}
	void selectPerk(const char * suffix)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		const std::string perk = std::string("new-horizons:demonicGating.") + suffix;
		const auto & state = attackerSideHero->getPerkState();
		if(!state.canAcquirePerk("new-horizons:demonicGating", perk, MasteryLevel::EXPERT))
		{
			const std::string basic = "new-horizons:demonicGating.wideGate";
			if(state.canAcquirePerk("new-horizons:demonicGating", basic, MasteryLevel::EXPERT))
				attackerSideHero->applyPerkSelection({"new-horizons:demonicGating", basic});
			const std::string advanced = "new-horizons:demonicGating.infernalDiscipline";
			if(!state.canAcquirePerk("new-horizons:demonicGating", perk, MasteryLevel::EXPERT)
				&& state.canAcquirePerk("new-horizons:demonicGating", advanced, MasteryLevel::EXPERT))
				attackerSideHero->applyPerkSelection({"new-horizons:demonicGating", advanced});
		}
		attackerSideHero->applyPerkSelection({"new-horizons:demonicGating", perk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:demonicGating", perk));
	}
	BattleAction legalGate() const
	{
		const auto * source = battle()->battleActiveUnit();
		BattleAction action;
		action.actionType = EActionType::DEMONIC_GATING;
		action.side = BattleSide::ATTACKER;
		action.stackNumber = source->unitId();
		action.gatingCreature = source->creatureId();
		for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
		{
			action.target.clear();
			action.aimToHex(BattleHex(i));
			if(newHorizonsDemonicGating::plan(*battle(), action))
				return action;
		}
		action.target.clear();
		return action;
	}
	void retainHistoricalMobileDefinition(bool temporary)
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		const JsonNode historical(JsonPath::builtin("test/newHorizonsHistoricalFactionPerks"));
		state.rules["skills"]["new-horizons:demonicGating"] = historical["skills"]["new-horizons:demonicGating"];
		if(!temporary)
			state.rules.Struct().erase("demonicGatingPolicy");
		ASSERT_NO_THROW(newHorizonsHeroes::validatePerkRules(state.rules));
		state.selected.clear();
		ASSERT_NO_FATAL_FAILURE(selectPerk("mobileGate"));
		if(!temporary)
			battle()->getSide(BattleSide::ATTACKER).demonicReserve[battle()->battleActiveUnit()->creatureId()] = 3;
	}
	BattleAction movementGate() const
	{
		const auto ordinary = legalGate();
		if(ordinary.target.empty())
			return {};
		const auto * source = battle()->battleActiveUnit();
		const auto gate = ordinary.target.front().hexValue;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex move(index);
			if(!move.isAvailable() || move == source->getPosition() || move == gate
				|| BattleHex::getDistance(move, gate) > newHorizonsDemonicGating::placementRange(*attackerSideHero))
				continue;
			const auto [path, distance] = battle()->getPath(source->getPosition(), move, source);
			if(path.empty() || distance <= 0 || distance > static_cast<int>(source->getMovementRange(0) / 2))
				continue;
			auto action = ordinary;
			action.target.clear();
			action.aimToHex(move);
			action.aimToHex(gate);
			return action;
		}
		return {};
	}
	void issue(const BattleAction & action)
	{
		ASSERT_FALSE(action.target.empty());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}
};
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, CapturedPolicyIsStrictAndLegacyRemainsAbsent)
{
	JsonNode historical;
	EXPECT_FALSE(newHorizonsDemonicGating::usesTemporaryGating(historical));
	historical["demonicGatingPolicy"]["version"].Integer() = 2;
	EXPECT_TRUE(newHorizonsDemonicGating::usesTemporaryGating(historical));
	EXPECT_THROW(newHorizonsDemonicGating::validatePolicySerialization(historical, false), std::runtime_error);
	historical["demonicGatingPolicy"]["unexpected"].Bool() = true;
	EXPECT_THROW(newHorizonsDemonicGating::validatePolicy(historical), std::runtime_error);
}
TEST_F(NewHorizonsTemporaryDemonicGatingTest, CountUsesCurrentCreaturesWithFloorAndMinimum)
{
	EXPECT_EQ(newHorizonsDemonicGating::summonedCount(*attackerSideHero, 12), 3);
	EXPECT_EQ(newHorizonsDemonicGating::summonedCount(*attackerSideHero, 9), 2);
	EXPECT_EQ(newHorizonsDemonicGating::summonedCount(*attackerSideHero, 1), 1);
	EXPECT_EQ(newHorizonsDemonicGating::summonedCount(*attackerSideHero, 0), 0);
}
TEST_F(NewHorizonsTemporaryDemonicGatingTest, EmptyReserveStillAllowsSameTypeAndRejectsAlternateType)
{
	auto action = legalGate();
	ASSERT_FALSE(action.target.empty());
	const auto planned = newHorizonsDemonicGating::plan(*battle(), action);
	ASSERT_TRUE(planned);
	EXPECT_TRUE(planned->temporary);
	EXPECT_EQ(planned->count, 3);
	EXPECT_FALSE(planned->freeActivation);
	action.gatingCreature = CreatureID(CreatureID::decode("core:familiar"));
	EXPECT_FALSE(newHorizonsDemonicGating::plan(*battle(), action));
}
TEST_F(NewHorizonsTemporaryDemonicGatingTest, CommitMarksOriginalOnceAndArrivalIsTemporary)
{
	const auto action = legalGate();
	const auto * source = battle()->battleActiveUnit();
	const auto sourceId = source->unitId();
	ASSERT_NO_FATAL_FAILURE(issue(action));
	EXPECT_TRUE(battle()->getStack(sourceId)->hasOpenedDemonicGate());
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.pendingDemonicGates.size(), 1u);
	EXPECT_TRUE(side.pendingDemonicGates.front().temporary);
	EXPECT_EQ(side.pendingDemonicGates.front().count, 3);
	EXPECT_TRUE(side.demonicReserve.empty());
	endRound();
	ASSERT_TRUE(side.pendingDemonicGates.empty());
	ASSERT_EQ(side.gatedDemonicStacks.size(), 1u);
	const auto & record = side.gatedDemonicStacks.front();
	EXPECT_TRUE(record.temporary);
	EXPECT_EQ(record.endlessLegionRestoration(0), 0);
	const auto * reinforcement = battle()->getStack(record.unitId);
	ASSERT_NE(reinforcement, nullptr);
	EXPECT_TRUE(reinforcement->isSummoned());
	EXPECT_EQ(reinforcement->getCount(), 3);
	EXPECT_FALSE(newHorizonsDemonicGating::eligibleOriginalStack(*battle(), *reinforcement, BattleSide::ATTACKER));
	EXPECT_FALSE(newHorizonsDemonicGating::eligibleOriginalStack(*battle(), *battle()->getStack(sourceId), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, SwiftArrivalIsPublishedBeforeTheNextRoundBegins)
{
	ASSERT_NO_FATAL_FAILURE(selectPerk("swiftGate"));
	const int openingRound = battle()->getRound();
	const auto additionsBefore = server.unitAdditionRounds.size();
	ASSERT_NO_FATAL_FAILURE(issue(legalGate()));
	EXPECT_EQ(server.unitAdditionRounds.size(), additionsBefore);
	endRound();
	ASSERT_EQ(server.unitAdditionRounds.size(), additionsBefore + 1);
	EXPECT_EQ(server.unitAdditionRounds.back(), openingRound);
	EXPECT_EQ(battle()->getRound(), openingRound + 1);
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, MasterGateKeepsTheOriginalActivationWithoutAllowingRecursiveGating)
{
	ASSERT_NO_FATAL_FAILURE(selectPerk("masterGate"));
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto id = source->unitId();
	const auto serial = battle()->getActivationSerial();
	const auto activations = server.stackActivations.size();
	ASSERT_NO_FATAL_FAILURE(issue(legalGate()));
	EXPECT_TRUE(battle()->getMasterGateUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getActiveStackID(), static_cast<int32_t>(id));
	EXPECT_EQ(battle()->getActivationSerial(), serial);
	EXPECT_FALSE(source->moved());
	ASSERT_EQ(server.stackActivations.size(), activations + 1);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::MASTER_GATE_CONTINUATION);
	EXPECT_FALSE(newHorizonsDemonicGating::eligibleOriginalStack(*battle(), *source, BattleSide::ATTACKER));
	const auto actionRound = battle()->getRound();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(source)));
	if(battle()->getRound() == actionRound)
		EXPECT_TRUE(source->defended()); // Defend has its own spent-activation flag.
	else
		EXPECT_EQ(battle()->getRound(), actionRound + 1); // Round start resets moved().
	EXPECT_NE(battle()->getActivationSerial(), serial);
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, EndlessLegionCreatesHalfTheCurrentBodyWithoutWithdrawingArmyOrReserve)
{
	ASSERT_NO_FATAL_FAILURE(selectPerk("endlessLegion"));
	const auto * source = battle()->battleActiveUnit();
	const auto action = legalGate();
	const auto planned = newHorizonsDemonicGating::plan(*battle(), action);
	ASSERT_TRUE(planned);
	EXPECT_EQ(planned->count, 6);
	ASSERT_NO_FATAL_FAILURE(issue(action));
	endRound();
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.gatedDemonicStacks.size(), 1u);
	const auto * gated = battle()->getStack(side.gatedDemonicStacks.front().unitId);
	ASSERT_NE(gated, nullptr);
	EXPECT_EQ(gated->getCount(), 6);
	EXPECT_TRUE(gated->isSummoned());
	EXPECT_EQ(source->getCount(), 12);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 12);
	EXPECT_TRUE(side.demonicReserve.empty());
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, ReinforcedArrivalUsesSeparateShieldAndInfernalDisciplineLastsForCombat)
{
	ASSERT_NO_FATAL_FAILURE(selectPerk("reinforcedGate"));
	ASSERT_NO_FATAL_FAILURE(selectPerk("infernalDiscipline"));
	ASSERT_NO_FATAL_FAILURE(issue(legalGate()));
	endRound();
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.gatedDemonicStacks.size(), 1u);
	auto * gated = battle()->getStack(side.gatedDemonicStacks.front().unitId);
	ASSERT_NE(gated, nullptr);
	const auto creatureHealth = gated->getTotalHealth();
	const auto shield = creatureHealth * 20 / 100;
	EXPECT_EQ(gated->getCount(), 3);
	EXPECT_EQ(gated->health.getTemporaryHitPoints(), shield);
	EXPECT_EQ(gated->getAvailableHealth(), creatureHealth + shield);
	const auto floor = gated->getBonusesOfType(BonusType::MINIMUM_MORALE);
	ASSERT_EQ(floor->size(), 1u);
	EXPECT_EQ(floor->front()->duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(floor->front()->val, 0);
	auto copy = gated->acquireState();
	int64_t damage = shield;
	copy->damage(damage);
	EXPECT_EQ(copy->health.getTemporaryHitPoints(), 0);
	EXPECT_EQ(copy->getCount(), gated->getCount());
	EXPECT_EQ(copy->getAvailableHealth(), creatureHealth);
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, InfernalOnslaughtGrantsOnlyReinforcementsTheOrdinaryAttackBonus)
{
	ASSERT_NO_FATAL_FAILURE(selectPerk("infernalOnslaught"));
	const auto sourceId = battle()->battleActiveUnit()->unitId();
	ASSERT_NO_FATAL_FAILURE(issue(legalGate()));
	endRound();
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.gatedDemonicStacks.size(), 1u);
	const auto * gated = battle()->getStack(side.gatedDemonicStacks.front().unitId);
	ASSERT_NE(gated, nullptr);
	const auto selector = Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(attackerSideHero->id))
		.And(Selector::type()(BonusType::GENERAL_DAMAGE_PREMY));
	const auto bonuses = gated->getBonuses(selector);
	ASSERT_EQ(bonuses->size(), 1u);
	EXPECT_EQ(bonuses->front()->val, 15);
	EXPECT_EQ(bonuses->front()->duration, BonusDuration::ONE_BATTLE);
	EXPECT_TRUE(battle()->getStack(sourceId)->getBonuses(selector)->empty());
	EXPECT_EQ(gated->getCount(), 3);
	EXPECT_FALSE(side.chainGateArmed);
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, HellstrideChangesMovementWithoutChangingInitiativeOrGrantingMobileGate)
{
	ASSERT_NO_FATAL_FAILURE(selectPerk("hellstride"));
	const auto * source = battle()->battleActiveUnit();
	const auto movement = source->getMovementRange(0);
	const auto initiative = source->getInitiative();
	const auto action = legalGate();
	auto combined = action;
	combined.target.clear();
	combined.aimToHex(source->getPosition());
	combined.aimToHex(action.target.front().hexValue);
	EXPECT_FALSE(newHorizonsDemonicGating::plan(*battle(), combined));
	ASSERT_NO_FATAL_FAILURE(issue(action));
	endRound();
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.gatedDemonicStacks.size(), 1u);
	const auto * gated = battle()->getStack(side.gatedDemonicStacks.front().unitId);
	ASSERT_NE(gated, nullptr);
	EXPECT_EQ(gated->getMovementRange(0), movement + 2);
	EXPECT_EQ(gated->getInitiative(), initiative);
	const auto speed = gated->getBonusesOfType(BonusType::STACKS_MOVEMENT_RANGE);
	ASSERT_EQ(speed->size(), 1u);
	EXPECT_EQ(speed->front()->val, 2);
	EXPECT_EQ(speed->front()->duration, BonusDuration::ONE_BATTLE);
	EXPECT_FALSE(side.chainGateArmed);
}

TEST(NewHorizonsTemporaryGatingRoster, CurrentAdvancedPerksReplaceMobileAndChainWithoutChangingTierCounts)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	const auto & rows = rules["skills"]["new-horizons:demonicGating"]["perks"].Vector();
	std::map<std::string, int> tiers;
	std::set<std::string> ids;
	for(const auto & row : rows)
	{
		++tiers[row["requires"].String()];
		ids.insert(row["id"].String());
		if(row["id"].String() == "new-horizons:demonicGating.infernalOnslaught"
			|| row["id"].String() == "new-horizons:demonicGating.hellstride")
			EXPECT_EQ(row["requires"].String(), "advanced");
	}
	EXPECT_EQ(rows.size(), 10u);
	EXPECT_EQ(tiers["basic"], 4);
	EXPECT_EQ(tiers["advanced"], 4);
	EXPECT_EQ(tiers["expert"], 2);
	EXPECT_TRUE(ids.contains("new-horizons:demonicGating.infernalOnslaught"));
	EXPECT_TRUE(ids.contains("new-horizons:demonicGating.hellstride"));
	EXPECT_FALSE(ids.contains("new-horizons:demonicGating.mobileGate"));
	EXPECT_FALSE(ids.contains("new-horizons:demonicGating.chainGate"));
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, TemporaryPolicyRejectsRetainedHistoricalMobilePerkWithoutSpendingAnything)
{
	ASSERT_NO_FATAL_FAILURE(retainHistoricalMobileDefinition(true));
	ASSERT_TRUE(newHorizonsDemonicGating::usesTemporaryGating(attackerSideHero));
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate"));
	const auto action = movementGate();
	ASSERT_EQ(action.target.size(), 2u);
	const auto * source = battle()->battleActiveUnit();
	const auto before = source->acquireState()->save();
	const auto actions = server.startedActions.size();
	EXPECT_FALSE(newHorizonsDemonicGating::plan(*battle(), action));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(source->acquireState()->save(), before);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.empty());
	EXPECT_FALSE(source->hasOpenedDemonicGate());
}

TEST_F(NewHorizonsTemporaryDemonicGatingTest, AbsentTemporaryPolicyKeepsHistoricalMobilePerkAndReserveMovement)
{
	ASSERT_NO_FATAL_FAILURE(retainHistoricalMobileDefinition(false));
	ASSERT_FALSE(newHorizonsDemonicGating::usesTemporaryGating(attackerSideHero));
	const auto action = movementGate();
	ASSERT_EQ(action.target.size(), 2u);
	const auto planned = newHorizonsDemonicGating::plan(*battle(), action);
	ASSERT_TRUE(planned);
	EXPECT_FALSE(planned->temporary);
	ASSERT_NO_FATAL_FAILURE(issue(action));
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.pendingDemonicGates.size(), 1u);
	EXPECT_FALSE(side.pendingDemonicGates.front().temporary);
	EXPECT_EQ(battle()->getStack(action.stackNumber)->getPosition(), action.target.front().hexValue);
	EXPECT_TRUE(side.demonicReserve.empty());
}
