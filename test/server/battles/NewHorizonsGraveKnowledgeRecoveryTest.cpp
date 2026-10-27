/*
 * NewHorizonsGraveKnowledgeRecoveryTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "NewHorizonsReclaimerFixture.h"
#include "../../../client/NewHorizonsNecromancyText.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
class RecoveryRecordingServer : public RecordingGameServer
{
public:
	std::vector<BattleResult> captured;
	std::vector<BattleResultsApplied> applied;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * value = dynamic_cast<const BattleResult *>(&pack))
			captured.push_back(*value);
		if(const auto * value = dynamic_cast<const BattleResultsApplied *>(&pack))
			applied.push_back(*value);
		RecordingGameServer::applyPack(pack);
	}
};
}

class NewHorizonsGraveKnowledgeRecoveryTest : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
	RecoveryRecordingServer recording;
	static CreatureID id(const char * value) { return CreatureID(CreatureID::decode(value)); }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(!GetParam())
			perks.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
		perks.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		recording.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(recording, gameState());
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
		gameHandler->onAdvInterfaceReady(PlayerColor(1));
		attackerSideHero->setHeroType(HeroTypeID(72));
		attackerSideHero->level = 20;
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::BLACK_HARVEST_ID});
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), id("core:skeleton"), 20));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), id("core:skeletonWarrior"), 20));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), id("core:wight"), 10));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), id("core:stoneGolem"), 20));
	}
	void select(const char * perk)
	{
		attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, perk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID, perk));
	}
	CStack * own(CreatureID creature)
	{
		for(const auto * stack : battle()->battleGetAllStacks())
			if(stack->unitSide() == BattleSide::ATTACKER && stack->creatureId() == creature)
				return const_cast<CStack *>(stack);
		return nullptr;
	}
	void damage(CStack * stack, int count, bool destroy = false)
	{
		BattleStackAttacked attack;
		attack.stackAttacked = stack->unitId();
		attack.damageAmount = static_cast<int64_t>(count) * stack->getMaxHealth();
		stack->prepareAttacked(attack, gameHandler->getRandomGenerator(), destroy, battle::DamageProvenance::SPELL);
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(attack.newState);
		gameHandler->sendAndApply(changed);
	}
	void restore(CStack * stack, int count, EHealLevel level, EHealPower power)
	{
		auto state = stack->acquireState();
		int64_t amount = static_cast<int64_t>(count) * stack->getMaxHealth();
		state->heal(amount, level, power);
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.emplace_back(stack->unitId(), UnitChanges::EOperation::UPDATE);
		changed.changedStacks.back().data = state->save();
		changed.changedStacks.back().healthDelta = amount;
		gameHandler->sendAndApply(changed);
	}
	void finish()
	{
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
			}
		}
		ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
		ASSERT_EQ(recording.applied.size(), 1u);
		ASSERT_EQ(recording.captured.size(), 1u);
	}
};

TEST_P(NewHorizonsGraveKnowledgeRecoveryTest, OwnFormsFloorSeparatelyWhileHistoricalKnowledgeRaisesEnemyUndead)
{
	select(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	damage(own(id("core:skeleton")), 9);
	damage(own(id("core:skeletonWarrior")), 9);
	finish();
	const auto & summary = recording.applied.back().necromancy;
	EXPECT_EQ(summary.recoveredSkeletons, GetParam() ? 1 : 0);
	EXPECT_EQ(summary.recoveredSkeletonWarriors, GetParam() ? 1 : 0);
	EXPECT_EQ(summary.graveKnowledgeSkeletons, GetParam() ? 0 : 2);
	EXPECT_EQ(summary.skeletonsRaised, GetParam() ? 0 : 2);
	EXPECT_EQ(summary.manaRecovered, 0);
	EXPECT_TRUE(summary.active);
	EXPECT_EQ(summary.applied, !GetParam());
	EXPECT_FALSE(summary.blockedByArmyCapacity);
	EXPECT_EQ(necromancyPresentation::recoveryOutputs(summary).size(), GetParam() ? 2u : 0u);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), GetParam() ? 12 : 13);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), GetParam() ? 12 : 11);
}

TEST_P(NewHorizonsGraveKnowledgeRecoveryTest, GraveplateNeverFeedsCurrentConstructRaising)
{
	select(newHorizonsNecromancy::DEATH_LORD_ID);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	finish();
	const auto & summary = recording.applied.back().necromancy;
	EXPECT_EQ(summary.deathLordCasualties, GetParam() ? 0 : 20);
	EXPECT_EQ(summary.deathLordSkeletons, GetParam() ? 0 : 1);
	EXPECT_EQ(summary.skeletonsRaised, GetParam() ? 0 : 1);
}

TEST_P(NewHorizonsGraveKnowledgeRecoveryTest, DestroyedRemainsAndPermanentResurrectionCannotBeRecoveredAgain)
{
	select(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	auto * skeleton = own(id("core:skeleton"));
	auto * warrior = own(id("core:skeletonWarrior"));
	damage(skeleton, 10, true);
	damage(skeleton, 5);
	damage(warrior, 10);
	restore(warrior, 10, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	finish();
	const auto & summary = recording.applied.back().necromancy;
	EXPECT_EQ(summary.recoveredSkeletons, GetParam() ? 1 : 0);
	EXPECT_EQ(summary.recoveredSkeletonWarriors, 0);
	if(GetParam())
	{
		EXPECT_EQ(recording.captured.back().necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].at(id("core:skeleton")), 5);
	}
}

TEST_P(NewHorizonsGraveKnowledgeRecoveryTest, TemporaryResurrectionDoesNotEraseOriginalLossesThatStillExpire)
{
	select(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	auto * skeleton = own(id("core:skeleton"));
	damage(skeleton, 10);
	restore(skeleton, 5, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_EQ(skeleton->getCount(), 15);
	if(GetParam())
	{
		const auto before = skeleton->acquireState()->save();
		BattleResult probe;
		probe.winner = BattleSide::ATTACKER;
		newHorizonsNecromancy::captureOwnSkeletonCasualties(*skeleton, *skeleton,
			*attackerSideHero, 10, probe);
		EXPECT_EQ(skeleton->acquireState()->save(), before);
		EXPECT_EQ(probe.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].at(id("core:skeleton")), 10);
	}
	finish();
	EXPECT_EQ(recording.applied.back().necromancy.recoveredSkeletons, GetParam() ? 2 : 0);
	if(GetParam())
	{
		EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 12);
	}
}

TEST_P(NewHorizonsGraveKnowledgeRecoveryTest, TemporarySurplusDeathsDoNotCreateOriginalRosterLosses)
{
	select(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	auto * skeleton = own(id("core:skeleton"));
	restore(skeleton, 10, EHealLevel::OVERHEAL, EHealPower::ONE_BATTLE);
	ASSERT_EQ(skeleton->getCount(), 30);
	damage(skeleton, 10);
	ASSERT_EQ(skeleton->getCount(), 20);
	finish();
	EXPECT_EQ(recording.applied.back().necromancy.recoveredSkeletons, 0);
}

TEST_P(NewHorizonsGraveKnowledgeRecoveryTest, BlockedFreshRaisingDoesNotEraseSuccessfulOwnRecoveryOrAwardMana)
{
	select(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	const auto skeleton = id("core:skeleton");
	const auto capacity = attackerSideHero->getLeadershipSlotCapacity(skeleton)->maximum;
	ASSERT_GT(capacity, 10);
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, capacity));
	const std::array<const char *, 6> fillers = {"core:pikeman", "core:archer", "core:swordsman",
		"core:griffin", "core:monk", "core:cavalier"};
	for(size_t index = 0; index < fillers.size(); ++index)
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(static_cast<int>(index + 1)), id(fillers[index]), 1));
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), id("core:pikeman"), 100));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	damage(own(skeleton), 10);
	finish();
	const auto & summary = recording.applied.back().necromancy;
	EXPECT_TRUE(summary.active);
	EXPECT_FALSE(summary.applied);
	EXPECT_TRUE(summary.blockedByArmyCapacity);
	EXPECT_EQ(summary.recoveredSkeletons, GetParam() ? 2 : 0);
	EXPECT_EQ(summary.skeletonsRaised + summary.zombiesRaised + summary.wightsRaised, 0);
	EXPECT_EQ(summary.manaRecovered, 0);
	EXPECT_EQ(necromancyPresentation::recoveryOutputs(summary).size(), GetParam() ? 1u : 0u);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), capacity - (GetParam() ? 8 : 10));
}

INSTANTIATE_TEST_SUITE_P(CapturedPolicy, NewHorizonsGraveKnowledgeRecoveryTest, ::testing::Bool());

class NewHorizonsGraveKnowledgeAdmissionTest : public NewHorizonsReclaimerFixture
{
protected:
	virtual const char * basicPerk() const { return newHorizonsNecromancy::BLACK_HARVEST_ID; }
	void SetUp() override
	{
		NewHorizonsReclaimerFixture::SetUp();
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, basicPerk()});
		attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID});
	}
};

TEST_F(NewHorizonsGraveKnowledgeAdmissionTest, RecoveryGetsCapacityBeforeFreshRaisingAndNeverFundsManaOrOssuary)
{
	fullArmy(*attackerSideHero, false);
	const auto skeleton = id("core:skeleton");
	const auto capacity = attackerSideHero->getLeadershipSlotCapacity(skeleton)->maximum;
	ASSERT_GE(capacity, 11);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, capacity - 10));
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	result.necromancySpecialEligibilityCaptured = true;
	result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER][skeleton] = 50;
	result.casualties[BattleSide::ATTACKER][skeleton] = 50;
	auto summary = newHorizonsNecromancy::resolve(2, 10, 10, false, false, false, true, true, 0, 100,
		0, false, true, CreatureID::NONE, {}, {}, false, false, true, 0, 0, true);
	newHorizonsNecromancy::applyBattleSkeletonRecovery(*gameHandler, result, *attackerSideHero, summary);
	EXPECT_EQ(summary.recoveredSkeletons, 10);
	EXPECT_EQ(summary.manaRecovered, 0);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), capacity);
	newHorizonsNecromancy::PreparedBattleNecromancy prepared{summary, summary, false};
	EXPECT_FALSE(newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), prepared));
	// Delivery's independently tested Ossuary contract remains fresh-output-only.
	prepared.ossuary = true;
	const auto delivery = newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), prepared);
	ASSERT_TRUE(delivery);
	EXPECT_EQ(delivery->army, nearTown->getUpperArmy());
	EXPECT_EQ(delivery->summary.recoveredSkeletons, 10);
	ASSERT_EQ(delivery->additions.size(), 1u);
	EXPECT_EQ(delivery->additions.front().count, 2);
	EXPECT_EQ(delivery->summary.manaRecovered, 0);
}

TEST_F(NewHorizonsGraveKnowledgeAdmissionTest, RecoveryIsBoundedByRemainingLossAndAvailableLeadershipRoom)
{
	fullArmy(*attackerSideHero, false);
	const auto skeleton = id("core:skeleton");
	const auto warrior = id("core:skeletonWarrior");
	const auto capacity = attackerSideHero->getLeadershipSlotCapacity(skeleton)->maximum;
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, capacity - 1));
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	result.necromancySpecialEligibilityCaptured = true;
	result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER] = {{skeleton, 100}, {warrior, 100}};
	result.casualties[BattleSide::ATTACKER] = {{skeleton, 5}, {warrior, 50}};
	auto summary = newHorizonsNecromancy::resolve(2, 0, 0, false, false, false, true, true, 0, 0);
	newHorizonsNecromancy::applyBattleSkeletonRecovery(*gameHandler, result, *attackerSideHero, summary);
	EXPECT_EQ(summary.recoveredSkeletons, 1);
	EXPECT_EQ(summary.recoveredSkeletonWarriors, 0);
	EXPECT_EQ(summary.zombiesRaised, 0) << "Vidomina never converts restored own units";
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), capacity);
}

class NewHorizonsBoneSteelRecoveryTest : public NewHorizonsGraveKnowledgeAdmissionTest
{
protected:
	const char * basicPerk() const override { return newHorizonsNecromancy::MASTER_OF_BONES_ID; }
};

TEST_F(NewHorizonsBoneSteelRecoveryTest, UpgradedFreshOutputAndVidominaNeverTransformRecoveredOwnSkeletons)
{
	const auto skeleton = id("core:skeleton");
	const auto warrior = id("core:skeletonWarrior");
	nearTown->addBuilding(BuildingID::DWELL_LVL_1);
	nearTown->addBuilding(BuildingID::DWELL_LVL_1_UP);
	nearTown->creatures.at(0).second = {skeleton, warrior};
	dwelling(*nearTown); // Enable Vidomina's independent fresh-output specialty too.
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, 20));
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	result.necromancyEligibilityCaptured = result.necromancySpecialEligibilityCaptured = true;
	result.necromancyEligibleCasualties[BattleSide::DEFENDER][id("core:pikeman")] = 20;
	result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER][skeleton] = 10;
	result.casualties[BattleSide::ATTACKER][skeleton] = 10;
	auto prepared = newHorizonsNecromancy::prepareBattleNecromancy(result,
		gameState()->getCreatureCategoryRules(), 0, *attackerSideHero, owner());
	ASSERT_EQ(prepared.ordinary.skeletonCreature, warrior);
	ASSERT_EQ(prepared.enhanced.reclaimerZombiesRaised, 1);
	newHorizonsNecromancy::applyBattleSkeletonRecovery(*gameHandler, result, *attackerSideHero, prepared.ordinary);
	EXPECT_EQ(prepared.ordinary.recoveredSkeletons, 2);
	EXPECT_EQ(prepared.ordinary.recoveredSkeletonWarriors, 0);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(attackerSideHero->Slots().size(), 1u);
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->getCreatureID(), skeleton);
}

TEST_F(NewHorizonsGraveKnowledgeAdmissionTest, CloneSummonedAndUnboundCandidatesNeverEnterOwnRecoveryPool)
{
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), id("core:skeleton"), 20));
	CStack candidate(attackerSideHero->getStackPtr(SlotID(0)), PlayerColor(0), 999,
		BattleSide::ATTACKER, SlotID(0));
	// Every exclusion uses the same real original-roster deficit. A full-health
	// candidate would capture zero even if all exclusion guards were removed.
	int64_t injury = static_cast<int64_t>(candidate.getMaxHealth()) * 10;
	candidate.health.damage(injury, false);
	ASSERT_EQ(candidate.getCount(), 10);
	ASSERT_EQ(candidate.getKilled(), 10);
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	newHorizonsNecromancy::captureOwnSkeletonCasualties(candidate, candidate, *attackerSideHero, 10, result);
	ASSERT_EQ(result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].at(id("core:skeleton")), 10);

	result = {};
	result.winner = BattleSide::ATTACKER;
	candidate.cloned = true;
	newHorizonsNecromancy::captureOwnSkeletonCasualties(candidate, candidate, *attackerSideHero, 10, result);
	EXPECT_TRUE(result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].empty());

	result = {};
	result.winner = BattleSide::ATTACKER;
	candidate.cloned = false;
	candidate.summoned = true;
	newHorizonsNecromancy::captureOwnSkeletonCasualties(candidate, candidate, *attackerSideHero, 10, result);
	EXPECT_TRUE(result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].empty());

	result = {};
	result.winner = BattleSide::ATTACKER;
	candidate.summoned = false;
	candidate.base = nullptr;
	newHorizonsNecromancy::captureOwnSkeletonCasualties(candidate, candidate, *attackerSideHero, 10, result);
	EXPECT_TRUE(result.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].empty());
}
