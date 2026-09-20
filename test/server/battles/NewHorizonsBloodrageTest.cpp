/*
 * NewHorizonsBloodrageTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/NewHorizonsBloodrage.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr std::string_view BLOODRAGE = "new-horizons:bloodrage";
constexpr std::string_view WAR_DRUMS = "new-horizons:bloodrage.warDrums";

class NewHorizonsBloodrageRuntimeTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		startGame();
	}

	SecondarySkill bloodrage() const
	{
		const int decoded = SecondarySkill::decode(std::string(BLOODRAGE));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void setRank(CGHeroInstance * hero, int rank)
	{
		hero->setSecSkillLevel(bloodrage(), rank, ChangeValueMode::ABSOLUTE);
	}

	CStack * addSpecialStack(BattleSide side, BattleHex position, bool summoned, bool cloned)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = 1;
		info.type = creatureByName("core:pikeman");
		info.side = side;
		info.position = position;
		info.summoned = summoned;
		BattleUnitsChanged pack;
		pack.battleID = BattleID(0);
		pack.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(pack.changedStacks.back().data);
		gameHandler->sendAndApply(pack);
		auto * result = battle()->getStack(info.id);
		if(cloned)
		{
			BattleUnitsChanged cloneUpdate;
			cloneUpdate.battleID = BattleID(0);
			cloneUpdate.changedStacks.emplace_back(info.id, UnitChanges::EOperation::UPDATE);
			cloneUpdate.changedStacks.back().data = result->acquireState()->save();
			cloneUpdate.changedStacks.back().data["state"]["cloned"].Bool() = true;
			gameHandler->sendAndApply(cloneUpdate);
		}
		return result;
	}

	void kill(const std::vector<CStack *> & targets)
	{
		StacksInjured injury;
		injury.battleID = BattleID(0);
		for(const auto * target : targets)
		{
			auto & attacked = injury.stacks.emplace_back();
			attacked.stackAttacked = target->unitId();
			attacked.damageAmount = target->getAvailableHealth();
			target->prepareAttacked(attacked, gameHandler->getRandomGenerator());
			EXPECT_TRUE(attacked.killed());
			EXPECT_FALSE(attacked.willRebirth());
			ASSERT_NE(battle()->getStack(attacked.stackAttacked, false), nullptr);
		}
		gameHandler->sendAndApply(injury);
	}
};

class BloodrageEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BloodrageEnvironment(std::shared_ptr<CGameState> state_) : state(std::move(state_)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class BloodrageCompatibilityTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
	}
};
}

TEST(NewHorizonsBloodrageRulesTest, RankIncrementsAndCapsAreCanonical)
{
	EXPECT_EQ(newHorizonsBloodrage::incrementForRank(0), 0);
	EXPECT_EQ(newHorizonsBloodrage::incrementForRank(1), 5);
	EXPECT_EQ(newHorizonsBloodrage::incrementForRank(2), 8);
	EXPECT_EQ(newHorizonsBloodrage::incrementForRank(3), 12);
	EXPECT_EQ(newHorizonsBloodrage::capForRank(1), 20);
	EXPECT_EQ(newHorizonsBloodrage::capForRank(2), 40);
	EXPECT_EQ(newHorizonsBloodrage::capForRank(3), 60);
	EXPECT_EQ(newHorizonsBloodrage::capForRank(4), 0);
}

TEST_F(BloodrageCompatibilityTest, RanklessDeathDoesNotDirtyLegacySerialization)
{
	startBattle();
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
	StacksInjured injury;
	injury.battleID = BattleID(0);
	auto & attacked = injury.stacks.emplace_back();
	attacked.stackAttacked = victim->unitId();
	attacked.damageAmount = victim->getAvailableHealth();
	victim->prepareAttacked(attacked, gameHandler->getRandomGenerator());
	ASSERT_TRUE(attacked.killed());
	gameHandler->sendAndApply(injury);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS;
	EXPECT_NO_THROW(legacy.oser & *battle());
	EXPECT_FALSE(legacy.extractBuffer().empty());
}

TEST_F(NewHorizonsBloodrageRuntimeTest, FriendlyEnemyAndMultiStackDeathsAdvanceBothEligibleSides)
{
	setRank(attackerSideHero, 1);
	setRank(defenderSideHero, 2);
	startBattle();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
	kill({friendly, enemy});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::DEFENDER), 16);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, SummonedAndCloneDeathsDoNotAdvance)
{
	setRank(attackerSideHero, 3);
	startBattle();
	auto * summoned = addSpecialStack(BattleSide::DEFENDER, BattleHex(leftHex), true, false);
	auto * clone = addSpecialStack(BattleSide::DEFENDER, BattleHex(rightHex), false, true);
	kill({summoned, clone});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, RemovalCountsLikeSacrificeAndDuplicateDeathDoesNot)
{
	setRank(attackerSideHero, 1);
	startBattle();
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1);
	BattleUnitsChanged removal;
	removal.battleID = BattleID(0);
	removal.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(removal);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 5);
	gameHandler->sendAndApply(removal);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 5);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, SurvivingDamageRebirthAndGenuineResurrectionAreAccountedOnce)
{
	setRank(attackerSideHero, 1);
	startBattle();
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
	const JsonNode livingState = victim->acquireState()->save();

	StacksInjured surviving;
	surviving.battleID = BattleID(0);
	auto & hit = surviving.stacks.emplace_back();
	hit.stackAttacked = victim->unitId();
	hit.damageAmount = 1;
	victim->prepareAttacked(hit, gameHandler->getRandomGenerator());
	ASSERT_FALSE(hit.killed());
	gameHandler->sendAndApply(surviving);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);

	kill({victim});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 5);
	StacksInjured duplicate = surviving;
	duplicate.stacks.clear();
	auto & deadAgain = duplicate.stacks.emplace_back();
	deadAgain.stackAttacked = victim->unitId();
	deadAgain.flags = BattleStackAttacked::KILLED;
	deadAgain.newState.id = victim->unitId();
	deadAgain.newState.data = victim->acquireState()->save();
	gameHandler->sendAndApply(duplicate);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 5);

	BattleUnitsChanged resurrection;
	resurrection.battleID = BattleID(0);
	resurrection.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
	resurrection.changedStacks.back().data = livingState;
	gameHandler->sendAndApply(resurrection);
	ASSERT_TRUE(victim->alive());
	kill({victim});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);

	auto * rebirthing = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1);
	StacksInjured rebirth;
	rebirth.battleID = BattleID(0);
	auto & rebirthHit = rebirth.stacks.emplace_back();
	rebirthHit.stackAttacked = rebirthing->unitId();
	rebirthHit.flags = BattleStackAttacked::KILLED | BattleStackAttacked::REBIRTH;
	rebirthHit.newState.id = rebirthing->unitId();
	rebirthHit.newState.data = rebirthing->acquireState()->save();
	gameHandler->sendAndApply(rebirth);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, HypotheticalKillAdvancesOnlyIsolatedDamageState)
{
	setRank(attackerSideHero, 1);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
	auto * nextTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 1), 10);
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto environment = std::make_shared<BloodrageEnvironment>(gameState());
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto projectedVictim = model->getForUpdate(victim->unitId());
	const bool wasAlive = projectedVictim->alive();
	int64_t lethal = projectedVictim->getAvailableHealth();
	projectedVictim->damage(lethal);
	model->recordBloodrageTransition(projectedVictim, wasAlive);

	EXPECT_EQ(model->getBloodrageDamagePercent(BattleSide::ATTACKER), 5);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	const auto live = battle()->calculateDmgRange(BattleAttackInfo(attacker, nextTarget, 0, false)).damage;
	const auto projectedAttacker = model->getForUpdate(attacker->unitId());
	const auto projectedTarget = model->getForUpdate(nextTarget->unitId());
	const auto simulated = model->calculateDmgRange(BattleAttackInfo(projectedAttacker.get(), projectedTarget.get(), 0, false)).damage;
	EXPECT_EQ(simulated.min, live.min * 105 / 100);
	EXPECT_EQ(simulated.max, live.max * 105 / 100);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, CounterCapsAndAffectsCreatureDamageAndSharedPreviewOnly)
{
	setRank(attackerSideHero, 1);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	BattleAttackInfo info(attacker, defender, 0, false);
	const auto base = battle()->calculateDmgRange(info).damage;
	for(int i = 0; i < 8; ++i)
		battle()->recordBloodrageStackDeath(1000 + i);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 20);
	const auto boosted = battle()->calculateDmgRange(info).damage;
	EXPECT_EQ(boosted.min, base.min * 120 / 100);
	EXPECT_EQ(boosted.max, base.max * 120 / 100);
	const auto preview = battle()->battleEstimateDamage(info).damage;
	EXPECT_EQ(preview.min, boosted.min);
	EXPECT_EQ(preview.max, boosted.max);
	info.physicalDamage = false;
	const auto spellLike = battle()->calculateDmgRange(info).damage;
	EXPECT_EQ(spellLike.min, base.min);
	EXPECT_EQ(spellLike.max, base.max);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, RetaliationReadsDefendingSidesCounter)
{
	setRank(defenderSideHero, 2);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	BattleAttackInfo retaliation(defender, attacker, 0, false);
	retaliation.retaliation = true;
	const auto base = battle()->calculateDmgRange(retaliation).damage;
	battle()->recordBloodrageStackDeath(1000);
	const auto boosted = battle()->calculateDmgRange(retaliation).damage;
	EXPECT_EQ(boosted.min, base.min * 108 / 100);
	EXPECT_EQ(boosted.max, base.max * 108 / 100);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, WarDrumsStartsWithOneRankSizedIncrement)
{
	setRank(attackerSideHero, 2);
	attackerSideHero->applyPerkSelection({std::string(BLOODRAGE), std::string(WAR_DRUMS)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(BLOODRAGE), std::string(WAR_DRUMS)));
	startBattle();
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 8);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, CurrentBattleRoundTripPreservesCounter)
{
	setRank(attackerSideHero, 3);
	startBattle();
	battle()->recordBloodrageStackDeath(1000);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);
	restored->recordBloodrageStackDeath(1000);
	EXPECT_EQ(restored->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);
	restored->recordBloodrageStackDeath(1001);
	EXPECT_EQ(restored->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, LegacyBattleReadDefaultsCounter)
{
	startBattle();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS;
	old.oser & *battle();
	BattleInfo oldRestored(gameState().get());
	oldRestored.getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 12;
	old.iser & oldRestored;
	EXPECT_EQ(oldRestored.getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	oldRestored.recordBloodrageStackDeath(1000);
	EXPECT_EQ(oldRestored.getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsBloodrageRuntimeTest, LegacyBattleWriteRejectsCounterLoss)
{
	startBattle();
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 24;
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS;
	EXPECT_THROW(rejected.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
}
