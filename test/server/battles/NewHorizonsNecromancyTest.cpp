/*
 * NewHorizonsNecromancyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../SpellPointTestUtils.h"

#include "BattleTestFixture.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
using newHorizonsNecromancy::resolve;

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

/// The normal battle fixture records combat activity but intentionally ignores
/// result and system-message packs. These post-battle regressions also need to
/// verify that a capacity rejection is reported in the result without sending
/// a server complaint.
class NecromancyAdmissionRecordingServer final : public RecordingGameServer
{
public:
	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<SystemMessage *>(&pack))
			++systemMessages;
		if(const auto * results = dynamic_cast<const BattleResultsApplied *>(&pack))
			battleResults.push_back(*results);
		RecordingGameServer::applyPack(pack);
	}

	int systemMessages = 0;
	std::vector<BattleResultsApplied> battleResults;
};
}

TEST(NewHorizonsNecromancy, RankFormulaUsesExactLivingCountsAndBoneCollectorPoints)
{
	const auto basic = resolve(1, 99, 0, false, false, false, true, true, 0, 0);
	EXPECT_EQ(basic.percentage, 10);
	EXPECT_EQ(basic.skeletonsOffered, 9);
	EXPECT_EQ(basic.skeletonsRaised, 9);

	const auto advanced = resolve(2, 99, 0, false, false, false, true, true, 0, 0);
	EXPECT_EQ(advanced.skeletonsOffered, 19);

	const auto expert = resolve(3, 99, 0, false, false, false, true, true, 0, 0);
	EXPECT_EQ(expert.skeletonsOffered, 29);

	const auto collector = resolve(1, 99, 0, true, false, false, true, true, 0, 0);
	EXPECT_EQ(collector.percentage, 15);
	EXPECT_EQ(collector.skeletonsOffered, 14);
	EXPECT_TRUE(collector.applied);
}

TEST(NewHorizonsNecromancy, DarkConversionAutomaticallyUsesOnlyCompleteCoreGroups)
{
	const auto incompleteCoreGroup = resolve(1, 29, 29, false, false, true, true, true, 0, 0);
	EXPECT_EQ(incompleteCoreGroup.skeletonsOffered, 2);
	EXPECT_EQ(incompleteCoreGroup.zombiesRaised, 0);
	EXPECT_EQ(incompleteCoreGroup.skeletonsRaised, 2);
	EXPECT_FALSE(incompleteCoreGroup.darkConversionChosen);

	const auto completeCoreGroup = resolve(1, 30, 30, false, false, true, false, true, 0, 0);
	EXPECT_EQ(completeCoreGroup.skeletonsOffered, 3);
	EXPECT_EQ(completeCoreGroup.zombiesRaised, 1);
	EXPECT_EQ(completeCoreGroup.skeletonsRaised, 0);
	EXPECT_TRUE(completeCoreGroup.darkConversionChosen);
	EXPECT_TRUE(completeCoreGroup.applied);

	// Base Skeleton output is floored over all eligible casualties, but only
	// complete groups generated from the Core subset become Zombies.
	const auto mixedTiers = resolve(1, 100, 60, false, false, true, true, true, 0, 0);
	EXPECT_EQ(mixedTiers.skeletonsOffered, 10);
	EXPECT_EQ(mixedTiers.zombiesRaised, 2);
	EXPECT_EQ(mixedTiers.skeletonsRaised, 4);
	EXPECT_TRUE(mixedTiers.darkConversionChosen);

	// The global floor remains one Skeleton here; independently flooring the
	// Core subset must not lose that aggregate remainder.
	const auto aggregateRemainder = resolve(2, 5, 4, false, false, true, true, true, 0, 0);
	EXPECT_EQ(aggregateRemainder.skeletonsOffered, 1);
	EXPECT_EQ(aggregateRemainder.zombiesRaised, 0);
	EXPECT_EQ(aggregateRemainder.skeletonsRaised, 1);

	const auto blockedMixedTiers = resolve(1, 100, 60, false, false, true, true, false, 0, 0);
	EXPECT_TRUE(blockedMixedTiers.blockedByArmyCapacity);
	EXPECT_FALSE(blockedMixedTiers.applied);
	EXPECT_FALSE(blockedMixedTiers.darkConversionChosen);
	EXPECT_EQ(blockedMixedTiers.skeletonsRaised, 0);
	EXPECT_EQ(blockedMixedTiers.zombiesRaised, 0);
}

TEST(NewHorizonsNecromancy, SoulHarvesterUsesOnlyCompleteEliteGroupsAndLeavesRemainders)
{
	const auto completeEliteGroup = resolve(2, 30, 0, false, false, false, true, true, 0, 0,
		30, true, true);
	EXPECT_EQ(completeEliteGroup.skeletonsOffered, 6);
	EXPECT_EQ(completeEliteGroup.wightsRaised, 1);
	EXPECT_EQ(completeEliteGroup.skeletonsRaised, 0);
	EXPECT_EQ(completeEliteGroup.raisedCreature, creature("core:wight"));

	// Advanced Necromancy offers seven Skeletons from 35 Elite casualties. One
	// complete six-Skeleton group becomes a Wight; the seventh remains a Skeleton.
	const auto eliteRemainder = resolve(2, 35, 0, false, false, false, true, true, 0, 0,
		35, true, true);
	EXPECT_EQ(eliteRemainder.skeletonsOffered, 7);
	EXPECT_EQ(eliteRemainder.wightsRaised, 1);
	EXPECT_EQ(eliteRemainder.skeletonsRaised, 1);
	EXPECT_TRUE(eliteRemainder.applied);

	// Core and Champion casualties are not Soul Harvester inputs. Only captured
	// Elite casualties contribute to its independent six-Skeleton threshold.
	const auto nonEliteCasualties = resolve(2, 30, 30, false, false, false, true, true, 0, 0,
		0, true, true);
	EXPECT_EQ(nonEliteCasualties.skeletonsOffered, 6);
	EXPECT_EQ(nonEliteCasualties.wightsRaised, 0);
	EXPECT_EQ(nonEliteCasualties.skeletonsRaised, 6);

	// Champion casualties are also non-Elite input; the Resolver accepts only
	// the separately captured Elite count for this conversion.
	const auto championCasualties = resolve(2, 30, 0, false, false, false, true, true, 0, 0,
		0, true, true);
	EXPECT_EQ(championCasualties.wightsRaised, 0);
	EXPECT_EQ(championCasualties.skeletonsRaised, 6);
}

TEST(NewHorizonsNecromancy, DarkConversionAndSoulHarvesterProduceIndependentOutputs)
{
	// At Advanced rank, 65 Core + 65 Elite casualties yield 26 Skeletons.
	// The Core share funds four Zombies, the Elite share funds two Wights, and
	// the global rounding remainder stays as two Skeletons.
	const auto mixedTiers = resolve(2, 130, 65, false, false, true, true, true, 0, 0,
		65, true, true);
	EXPECT_EQ(mixedTiers.skeletonsOffered, 26);
	EXPECT_EQ(mixedTiers.zombiesRaised, 4);
	EXPECT_EQ(mixedTiers.wightsRaised, 2);
	EXPECT_EQ(mixedTiers.skeletonsRaised, 2);
	EXPECT_TRUE(mixedTiers.darkConversionChosen);
	EXPECT_TRUE(mixedTiers.applied);
}

TEST(NewHorizonsNecromancy, MasterOfBonesChangesOnlyTheSkeletonOutputForm)
{
	const auto baseSkeletons = resolve(3, 10, 0, false, false, false, true, true, 0, 0);
	ASSERT_TRUE(baseSkeletons.applied);
	EXPECT_EQ(baseSkeletons.skeletonsRaised, 3);
	EXPECT_EQ(baseSkeletons.skeletonCreature, CreatureID::NONE);

	const auto skeletonWarrior = creature("core:skeletonWarrior");
	const auto upgraded = resolve(3, 10, 0, false, false, false, true, true, 0, 0,
		0, false, true, skeletonWarrior);
	ASSERT_TRUE(upgraded.applied);
	EXPECT_EQ(upgraded.skeletonsOffered, 3);
	EXPECT_EQ(upgraded.skeletonsRaised, 3);
	EXPECT_EQ(upgraded.skeletonCreature, skeletonWarrior);
	EXPECT_EQ(upgraded.raisedCreature, skeletonWarrior);
	EXPECT_EQ(upgraded.zombiesRaised, 0);
	EXPECT_EQ(upgraded.wightsRaised, 0);
	EXPECT_TRUE(upgraded.isSkeletonOutputValid());
}

TEST(NewHorizonsNecromancy, MasterOfBonesKeepsItsFormAcrossMixedOutputsAndAtomicRejection)
{
	const auto skeletonWarrior = creature("core:skeletonWarrior");
	const auto mixed = resolve(2, 130, 65, false, false, true, true, true, 0, 0,
		65, true, true, skeletonWarrior);
	ASSERT_TRUE(mixed.applied);
	EXPECT_EQ(mixed.skeletonsRaised, 2);
	EXPECT_EQ(mixed.skeletonCreature, skeletonWarrior);
	EXPECT_EQ(mixed.zombiesRaised, 4);
	EXPECT_EQ(mixed.wightsRaised, 2);

	const auto blocked = resolve(2, 130, 65, false, false, true, true, true, 0, 0,
		65, true, false, skeletonWarrior);
	EXPECT_TRUE(blocked.blockedByArmyCapacity);
	EXPECT_FALSE(blocked.applied);
	EXPECT_EQ(blocked.skeletonsRaised, 0);
	EXPECT_EQ(blocked.zombiesRaised, 0);
	EXPECT_EQ(blocked.wightsRaised, 0);
	EXPECT_EQ(blocked.skeletonCreature, CreatureID::NONE);
	EXPECT_TRUE(blocked.isSkeletonOutputValid());
}

TEST(NewHorizonsNecromancy, DeathLordAndGraveKnowledgeUseIndependentFlooredRates)
{
	const auto deathLordBelowThreshold = resolve(2, 0, 0, false, false, false, true, true, 0, 0,
		0, false, true, CreatureID::NONE,
		newHorizonsNecromancy::SpecialCasualtyCounts{19, 19, 0}, {});
	EXPECT_EQ(deathLordBelowThreshold.deathLordCasualties, 19);
	EXPECT_EQ(deathLordBelowThreshold.deathLordSkeletons, 0);
	EXPECT_EQ(deathLordBelowThreshold.skeletonsOffered, 0);

	const auto deathLordAtThreshold = resolve(2, 0, 0, false, false, false, true, true, 0, 0,
		0, false, true, CreatureID::NONE,
		newHorizonsNecromancy::SpecialCasualtyCounts{20, 20, 0}, {});
	EXPECT_EQ(deathLordAtThreshold.deathLordSkeletons, 1);
	EXPECT_EQ(deathLordAtThreshold.skeletonsOffered, 1);
	EXPECT_TRUE(deathLordAtThreshold.applied);

	const auto graveKnowledgeBelowThreshold = resolve(2, 0, 0, false, false, false, true, true, 0, 0,
		0, false, true, CreatureID::NONE, {},
		newHorizonsNecromancy::SpecialCasualtyCounts{4, 4, 0});
	EXPECT_EQ(graveKnowledgeBelowThreshold.graveKnowledgeCasualties, 4);
	EXPECT_EQ(graveKnowledgeBelowThreshold.graveKnowledgeSkeletons, 0);
	EXPECT_EQ(graveKnowledgeBelowThreshold.skeletonsOffered, 0);

	const auto graveKnowledgeAtThreshold = resolve(2, 0, 0, false, false, false, true, true, 0, 0,
		0, false, true, CreatureID::NONE, {},
		newHorizonsNecromancy::SpecialCasualtyCounts{5, 5, 0});
	EXPECT_EQ(graveKnowledgeAtThreshold.graveKnowledgeSkeletons, 1);
	EXPECT_EQ(graveKnowledgeAtThreshold.skeletonsOffered, 1);
	EXPECT_TRUE(graveKnowledgeAtThreshold.applied);

	// Grave Knowledge is a fixed 20% pool, independent of Necromancy rank and
	// Bone Collector's ordinary-rate bonus.
	const auto basicWithCollector = resolve(1, 0, 0, true, false, false, true, true, 0, 0,
		0, false, true, CreatureID::NONE, {},
		newHorizonsNecromancy::SpecialCasualtyCounts{5, 5, 0});
	EXPECT_EQ(basicWithCollector.percentage, 15);
	EXPECT_EQ(basicWithCollector.graveKnowledgeSkeletons, 1);

	const auto expertWithoutCollector = resolve(3, 0, 0, false, false, false, true, true, 0, 0,
		0, false, true, CreatureID::NONE, {},
		newHorizonsNecromancy::SpecialCasualtyCounts{5, 5, 0});
	EXPECT_EQ(expertWithoutCollector.percentage, 30);
	EXPECT_EQ(expertWithoutCollector.graveKnowledgeSkeletons, 1);
}

TEST(NewHorizonsNecromancy, SpecialCasualtyCategorySharesFeedCoreConversion)
{
	const auto nonliving = resolve(2, 0, 0, false, false, true, true, true, 0, 0,
		0, false, true, CreatureID::NONE,
		newHorizonsNecromancy::SpecialCasualtyCounts{120, 60, 60}, {});
	EXPECT_EQ(nonliving.deathLordCasualties, 120);
	EXPECT_EQ(nonliving.deathLordSkeletons, 6);
	EXPECT_EQ(nonliving.zombiesRaised, 1);
	EXPECT_EQ(nonliving.skeletonsRaised, 3);
	EXPECT_EQ(nonliving.wightsRaised, 0);

	const auto undead = resolve(2, 0, 0, false, false, true, true, true, 0, 0,
		0, false, true, CreatureID::NONE, {},
		newHorizonsNecromancy::SpecialCasualtyCounts{45, 15, 30});
	EXPECT_EQ(undead.graveKnowledgeCasualties, 45);
	EXPECT_EQ(undead.graveKnowledgeSkeletons, 9);
	EXPECT_EQ(undead.zombiesRaised, 1);
	EXPECT_EQ(undead.skeletonsRaised, 6);
	EXPECT_EQ(undead.wightsRaised, 0);
}

TEST(NewHorizonsNecromancy, SpecialGeneratedSkeletonsRemainReportedWhenCapacityBlocksAllOutputs)
{
	const auto blocked = resolve(2, 0, 0, false, false, true, true, true, 0, 0,
		0, true, false, CreatureID::NONE, {},
		newHorizonsNecromancy::SpecialCasualtyCounts{50, 20, 30});
	EXPECT_EQ(blocked.skeletonsOffered, 10);
	EXPECT_TRUE(blocked.blockedByArmyCapacity);
	EXPECT_FALSE(blocked.applied);
	EXPECT_EQ(blocked.skeletonsRaised, 0);
	EXPECT_EQ(blocked.zombiesRaised, 0);
	EXPECT_EQ(blocked.wightsRaised, 0);
	EXPECT_EQ(blocked.graveKnowledgeCasualties, 50);
	EXPECT_EQ(blocked.graveKnowledgeSkeletons, 10);
}

TEST(NewHorizonsNecromancy, ThreeOutputCapacityRejectionIsAtomic)
{
	// The same three output kinds need distinct destinations. With only the
	// Skeleton and Zombie destinations available, no part of the result applies.
	const auto blocked = resolve(2, 130, 65, false, true, true, true, true, 0, 100,
		65, true, false);
	EXPECT_EQ(blocked.skeletonsOffered, 26);
	EXPECT_TRUE(blocked.blockedByArmyCapacity);
	EXPECT_FALSE(blocked.applied);
	EXPECT_FALSE(blocked.darkConversionChosen);
	EXPECT_EQ(blocked.skeletonsRaised, 0);
	EXPECT_EQ(blocked.zombiesRaised, 0);
	EXPECT_EQ(blocked.wightsRaised, 0);
	EXPECT_EQ(blocked.manaRecovered, 0);
}

TEST(NewHorizonsNecromancy, DestinationReservationUsesTwoFreeSlotsAndRejectsOneAtomically)
{
	const auto twoSlots = newHorizonsNecromancy::reserveDestinations(
		SlotID(), SlotID(), {SlotID(2), SlotID(5)}, 1, 3);
	ASSERT_TRUE(twoSlots.fits);
	EXPECT_EQ(twoSlots.skeleton, SlotID(2));
	EXPECT_EQ(twoSlots.zombie, SlotID(5));
	EXPECT_NE(twoSlots.skeleton, twoSlots.zombie);

	const auto oneSlot = newHorizonsNecromancy::reserveDestinations(
		SlotID(), SlotID(), {SlotID(4)}, 1, 3);
	EXPECT_FALSE(oneSlot.fits);
	EXPECT_EQ(oneSlot.skeleton, SlotID(4));
	EXPECT_FALSE(oneSlot.zombie.validSlot());

	const auto existingSkeleton = newHorizonsNecromancy::reserveDestinations(
		SlotID(1), SlotID(), {SlotID(4)}, 1, 3);
	EXPECT_TRUE(existingSkeleton.fits);
	EXPECT_EQ(existingSkeleton.skeleton, SlotID(1));
	EXPECT_EQ(existingSkeleton.zombie, SlotID(4));
}

TEST(NewHorizonsNecromancy, BlackHarvestIsCappedByManaCapacity)
{
	const auto harvested = resolve(3, 1000, 0, false, false, false, true, true, 0, 100);
	EXPECT_EQ(harvested.skeletonsRaised, 300);
	EXPECT_EQ(harvested.manaRecovered, 10);

	const auto nearlyFull = resolve(3, 1000, 0, false, false, false, true, true, 95, 100);
	EXPECT_EQ(nearlyFull.manaRecovered, 5);

	// Passing current mana as the limit is the server's no-Black-Harvest gate.
	const auto noHarvest = resolve(3, 1000, 0, false, false, false, true, true, 0, 0);
	EXPECT_EQ(noHarvest.manaRecovered, 0);

	// Black Harvest counts final creatures, not the Skeleton-equivalent inputs.
	// Three hundred offered Skeletons become fifty Wights, so the actual output
	// restores five Mana instead of the ten Mana the unconverted count would cap at.
	const auto convertedOutput = resolve(3, 1000, 0, false, false, false, true, true, 0, 100,
		1000, true, true);
	EXPECT_EQ(convertedOutput.skeletonsOffered, 300);
	EXPECT_EQ(convertedOutput.wightsRaised, 50);
	EXPECT_EQ(convertedOutput.skeletonsRaised, 0);
	EXPECT_EQ(convertedOutput.manaRecovered, 5);
}

class NewHorizonsNecromancyRuntimeTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
	}

	void verifyTemporaryBufferCleanupAfterSpend(int32_t spent, int32_t expectedRemainingTemporary, int32_t expectedBuffer)
	{
		ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->manaLimit(), 10);
		attackerSideHero->initializeSpellPoints(5, 2);

		Bonus combatMana;
		combatMana.type = BonusType::COMBAT_MANA_BONUS;
		combatMana.val = 8;
		GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
		gameHandler->sendAndApply(grantCombatMana);

		startBattle();
		auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
		ASSERT_EQ(attackerSide.initialNormalSpellPoints, 5);
		ASSERT_EQ(attackerSide.initialBufferSpellPoints, 2);
		ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
		ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 10);

		gameHandler->grantBufferSpellPoints(attackerSideHero->id, 3);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
		gameHandler->spendSpellPoints(attackerSideHero->id, spent);
		EXPECT_EQ(attackerSide.temporaryBufferRemaining, expectedRemainingTemporary);

		BattleResultsApplied applied;
		applied.battleID = BattleID(0);
		gameState()->apply(applied);

		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), expectedBuffer);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), 5 + expectedBuffer);
	}

	void verifyNegativeCombatManaPenalty(int32_t penalty, int32_t expectedNormal, int32_t expectedBuffer)
	{
		ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		attackerSideHero->initializeSpellPoints(5, 2);

		Bonus combatMana;
		combatMana.type = BonusType::COMBAT_MANA_BONUS;
		combatMana.val = penalty;
		GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
		gameHandler->sendAndApply(grantCombatMana);

		startBattle();
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), expectedNormal);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), expectedBuffer);

		BattleCancelled cancelled;
		cancelled.battleID = BattleID(0);
		gameState()->apply(cancelled);

		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 2);
	}
};

class NewHorizonsDisintegrateStackTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}

	CStack * addTenPikemen()
	{
		return addStack(BattleSide::ATTACKER, creature("core:pikeman"), BattleHex(leftHex), 10);
	}

	static void giveGuaranteedRebirth(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
		stack->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	}
};

TEST_F(NewHorizonsDisintegrateStackTest, LethalDestroyRemainsPreservesOrdinaryCasualties)
{
	CStack * target = addTenPikemen();
	ASSERT_NE(target, nullptr);
	const int64_t unitHealth = target->getMaxHealth();
	auto state = target->acquireState();

	int64_t ordinaryDamage = unitHealth * 8;
	state->damage(ordinaryDamage);
	ASSERT_EQ(state->getCount(), 2);

	BattleStackAttacked attacked;
	attacked.damageAmount = unitHealth * 2;
	CStack::prepareAttacked(attacked, gameHandler->getRandomGenerator(), state, true);

	EXPECT_EQ(attacked.killedAmount, 2);
	EXPECT_FALSE(state->alive());
	EXPECT_FALSE(state->ghostPending);
	EXPECT_EQ(state->getUnusableRemains(), 2);

	int64_t resurrection = unitHealth * 10;
	const auto healed = state->heal(resurrection, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(healed.resurrectedCount, 8);
	EXPECT_EQ(state->getCount(), 8);
	EXPECT_EQ(state->getUnusableRemains(), 2);
}

TEST_F(NewHorizonsDisintegrateStackTest, LethalDestroyRemainsGhostsWhenEveryCasualtyIsDestroyed)
{
	CStack * target = addTenPikemen();
	ASSERT_NE(target, nullptr);
	const int64_t unitHealth = target->getMaxHealth();
	auto state = target->acquireState();

	BattleStackAttacked attacked;
	attacked.damageAmount = unitHealth * 10;
	CStack::prepareAttacked(attacked, gameHandler->getRandomGenerator(), state, true);

	EXPECT_EQ(attacked.killedAmount, 10);
	EXPECT_FALSE(state->alive());
	EXPECT_TRUE(state->ghostPending);
	EXPECT_EQ(state->getUnusableRemains(), 10);
}

TEST_F(NewHorizonsDisintegrateStackTest, RebirthRestoresOnlyOrdinaryCasualties)
{
	CStack * target = addTenPikemen();
	ASSERT_NE(target, nullptr);
	giveGuaranteedRebirth(target);
	const int64_t unitHealth = target->getMaxHealth();
	auto state = target->acquireState();

	int64_t ordinaryDamage = unitHealth * 8;
	state->damage(ordinaryDamage);
	ASSERT_EQ(state->getCount(), 2);

	BattleStackAttacked attacked;
	attacked.damageAmount = unitHealth * 2;
	CStack::prepareAttacked(attacked, gameHandler->getRandomGenerator(), state, true);

	EXPECT_EQ(attacked.killedAmount, 2);
	EXPECT_TRUE(attacked.willRebirth());
	EXPECT_TRUE(state->alive());
	EXPECT_FALSE(state->ghostPending);
	EXPECT_EQ(state->getCount(), 8);
	EXPECT_EQ(state->getUnusableRemains(), 2);
}

class NewHorizonsNecromancyAITest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		// The attacker is the computer winner in this scenario.  The defender
		// remains human so the normal battle-result confirmation still drives the
		// authoritative post-battle continuation.
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
		startGame();
	}
};

/// Same computer-winner full-flow fixture, with a recorder installed after
/// setup so the assertions can inspect authoritative result/error packs.
class NewHorizonsNecromancyAdmissionAITest : public NewHorizonsNecromancyAITest
{
protected:
	enum class SkeletonDwellingScenario
	{
		ABSENT,
		OWNED_UPGRADE_UNBUILT,
		OWNED_UPGRADE_BUILT,
		FOREIGN_UPGRADE_BUILT
	};

	SkeletonDwellingScenario skeletonDwellingScenario = SkeletonDwellingScenario::ABSENT;
	CGTownInstance * skeletonDwellingTown = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsNecromancyAITest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void SetUp() override
	{
		// Build with a Necromancer-class hero from the outset. Hero capability
		// rules are captured during initialization, so changing the prototype
		// afterward (as the compact base fixture does) would retain Castle caps.
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";

		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NecromancyCapacityTest")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(72), PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{token, 1}});
		if(skeletonDwellingScenario != SkeletonDwellingScenario::ABSENT)
		{
			const auto townOwner = skeletonDwellingScenario == SkeletonDwellingScenario::FOREIGN_UPGRADE_BUILT
				? PlayerColor(1) : PlayerColor(0);
			builder.town({10, 10, 0}, FactionID::NECROPOLIS, townOwner).townGarrison({});
		}
		startWithMap(std::move(builder));

		const auto towns = gameState()->getMap().getObjects<CGTownInstance>();
		if(skeletonDwellingScenario == SkeletonDwellingScenario::ABSENT)
		{
			ASSERT_TRUE(towns.empty());
			RecordProperty("master_of_bones_town_setup", "absent");
		}
		else
		{
			ASSERT_EQ(towns.size(), 1u);
			skeletonDwellingTown = towns.front();
			ASSERT_NE(skeletonDwellingTown, nullptr);
			ASSERT_EQ(skeletonDwellingTown->getFactionID(), FactionID::NECROPOLIS);
			const bool foreign = skeletonDwellingScenario == SkeletonDwellingScenario::FOREIGN_UPGRADE_BUILT;
			ASSERT_EQ(skeletonDwellingTown->getOwner(), foreign ? PlayerColor(1) : PlayerColor(0));

			skeletonDwellingTown->removeAllBuildings();
			skeletonDwellingTown->addBuilding(BuildingID::DWELL_LVL_1);
			const bool upgradeBuilt = skeletonDwellingScenario == SkeletonDwellingScenario::OWNED_UPGRADE_BUILT || foreign;
			if(upgradeBuilt)
				skeletonDwellingTown->addBuilding(BuildingID::DWELL_LVL_1_UP);

			const auto * townType = skeletonDwellingTown->getTown();
			ASSERT_NE(townType, nullptr);
			ASSERT_TRUE(townType->buildings.contains(BuildingID::DWELL_LVL_1));
			ASSERT_TRUE(townType->buildings.contains(BuildingID::DWELL_LVL_1_UP));
			ASSERT_GE(townType->creatures.size(), 1u);
			ASSERT_GE(townType->creatures.front().size(), 2u);
			EXPECT_EQ(townType->creatures.front()[0], creature("core:skeleton"));
			EXPECT_EQ(townType->creatures.front()[1], creature("core:skeletonWarrior"));
			ASSERT_GE(skeletonDwellingTown->creatures.size(), 1u);
			auto & offeredCreatures = skeletonDwellingTown->creatures.front().second;
			offeredCreatures.clear();
			offeredCreatures.push_back(creature("core:skeleton"));
			if(upgradeBuilt)
				offeredCreatures.push_back(creature("core:skeletonWarrior"));
			EXPECT_EQ(skeletonDwellingTown->hasBuilt(BuildingID::DWELL_LVL_1_UP), upgradeBuilt);
			EXPECT_EQ(vstd::contains(offeredCreatures, creature("core:skeletonWarrior")), upgradeBuilt);

			RecordProperty("master_of_bones_town_setup", foreign ? "foreign_built"
				: upgradeBuilt ? "owned_built" : "owned_upgrade_unbuilt");
			RecordProperty("master_of_bones_town_owner", foreign ? "player1" : "player0");
			RecordProperty("master_of_bones_upgrade_dwelling_configured", "true");
			RecordProperty("master_of_bones_upgrade_dwelling_built", upgradeBuilt ? "true" : "false");
			RecordProperty("master_of_bones_upgrade_creature", "core:skeletonWarrior");
		}

		recordingServer = std::make_unique<NecromancyAdmissionRecordingServer>();
		recordingServer->gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(*recordingServer, gameState());
		gameHandler->randomizer->setSeed(seed);

		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		makeNeutralForCapacityTest(attackerSideHero);
		makeNeutralForCapacityTest(defenderSideHero);
		// Human-facing dialog queries remain dormant until the adventure UI
		// reports ready. These native full-flow tests answer those queries
		// directly, so mark both simulated controllers ready first.
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
		gameHandler->onAdvInterfaceReady(PlayerColor(1));
	}

	void TearDown() override
	{
		gameHandler.reset();
		recordingServer.reset();
		NewHorizonsNecromancyAITest::TearDown();
	}

	void prepareNecromancerArmy(bool fillAllSlots)
	{
		const auto necromancy = SecondarySkill::decode("new-horizons:necromancy");
		ASSERT_GE(necromancy, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(necromancy), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);

		const auto skeleton = creature("core:skeleton");
		const auto capacity = attackerSideHero->getLeadershipSlotCapacity(skeleton);
		ASSERT_TRUE(capacity);
		ASSERT_EQ(capacity->leadership, 725);
		ASSERT_EQ(capacity->maximum, 16);
		attackerSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, capacity->maximum));

		if(fillAllSlots)
			fillFillerSlots(1, GameConstants::ARMY_SIZE - 1);

		ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());
		ASSERT_FALSE(attackerSideHero->hasActivePerk(
			"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"));
	}

	void fillFillerSlots(int firstSlot, int count)
	{
		const std::array<const char *, GameConstants::ARMY_SIZE - 1> fillerCreatures = {
			"core:pikeman", "core:archer", "core:swordsman", "core:griffin", "core:monk", "core:cavalier"
		};
		ASSERT_LE(count, static_cast<int>(fillerCreatures.size()));
		for(int i = 0; i < count; ++i)
		{
			const auto filler = creature(fillerCreatures[static_cast<size_t>(i)]);
			const auto fillerCapacity = attackerSideHero->getLeadershipSlotCapacity(filler);
			ASSERT_TRUE(fillerCapacity);
			ASSERT_GE(fillerCapacity->maximum, 1);
			ASSERT_TRUE(attackerSideHero->setCreature(SlotID(firstSlot + i), filler, 1));
		}
	}

	void setMixedCoreEliteCasualties(int32_t coreCount, int32_t eliteCount)
	{
		const auto core = creature("core:pikeman");
		const auto elite = creature("core:monk");
		const auto coreCategory = gameState()->getCreatureCategory(core);
		const auto eliteCategory = gameState()->getCreatureCategory(elite);
		ASSERT_TRUE(coreCategory);
		ASSERT_TRUE(eliteCategory);
		ASSERT_EQ(coreCategory->category, newHorizonsCreatures::CreatureCategory::CORE);
		ASSERT_EQ(eliteCategory->category, newHorizonsCreatures::CreatureCategory::ELITE);

		defenderSideHero->clearSlots();
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), core, coreCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), elite, eliteCount));
	}

	void assertSpecialPerkRegistryRow(const std::string & perkId, const std::string & expectedName,
		const std::string & propertyPrefix)
	{
		const auto & perks = attackerSideHero->getPerkState().rules
			["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector();
		const auto row = std::find_if(perks.begin(), perks.end(), [&](const auto & perk)
		{
			return perk["id"].String() == perkId;
		});
		ASSERT_NE(row, perks.end());
		EXPECT_EQ((*row)["name"].String(), expectedName);
		EXPECT_EQ((*row)["requires"].String(), "advanced");
		EXPECT_EQ((*row)["effect"]["status"].String(), "active");
		RecordProperty(propertyPrefix + "_registry_status", (*row)["effect"]["status"].String());
	}

	void prepareAdvancedSpecialNecromancer(const std::string & perkId, const std::string & perkName,
		const std::string & propertyPrefix)
	{
		prepareNecromancerArmy(false);
		assertSpecialPerkRegistryRow(perkId, perkName, propertyPrefix);
		const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
		ASSERT_GE(necromancyIndex, 0);
		const SecondarySkill necromancy(necromancyIndex);

		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::DARK_CONVERSION_ID, MasteryLevel::BASIC));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DARK_CONVERSION_ID));
		gameHandler->levelUpHero(attackerSideHero, necromancy, false);
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(necromancy), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero, perkId, MasteryLevel::ADVANCED));
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection(newHorizonsNecromancy::SKILL_ID, perkId));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID, perkId));

		RecordProperty(propertyPrefix + "_activation_override_applied", "false");
		RecordProperty(propertyPrefix + "_skill_rank", "advanced");
		RecordProperty(propertyPrefix + "_selected", "true");
		RecordProperty(propertyPrefix + "_selected_perk", perkId);
		RecordProperty(propertyPrefix + "_basic_perk", newHorizonsNecromancy::DARK_CONVERSION_ID);
	}

	void prepareAdvancedNecromancerWithoutSpecialPerk()
	{
		prepareNecromancerArmy(false);
		const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
		ASSERT_GE(necromancyIndex, 0);
		const SecondarySkill necromancy(necromancyIndex);
		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::DARK_CONVERSION_ID, MasteryLevel::BASIC));
		gameHandler->levelUpHero(attackerSideHero, necromancy, false);
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(necromancy), MasteryLevel::ADVANCED);
		EXPECT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DEATH_LORD_ID));
		EXPECT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID));
	}

	void setSpecialCasualties(const char * coreId, const char * eliteId,
		int32_t coreCount, int32_t eliteCount, BonusType expectedBonus)
	{
		const auto core = creature(coreId);
		const auto elite = creature(eliteId);
		const auto coreCategory = gameState()->getCreatureCategory(core);
		const auto eliteCategory = gameState()->getCreatureCategory(elite);
		ASSERT_TRUE(coreCategory);
		ASSERT_TRUE(eliteCategory);
		ASSERT_EQ(coreCategory->category, newHorizonsCreatures::CreatureCategory::CORE);
		ASSERT_EQ(eliteCategory->category, newHorizonsCreatures::CreatureCategory::ELITE);
		ASSERT_TRUE(core.toCreature()->hasBonusOfType(expectedBonus));
		ASSERT_TRUE(elite.toCreature()->hasBonusOfType(expectedBonus));

		defenderSideHero->clearSlots();
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), core, coreCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), elite, eliteCount));
	}

	void resolveAdmissionBattle()
	{
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		resolveBattleDialogsOnly();
		const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
		EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
		resolveLevelUpDialogs();
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);
		EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	}

	bool selectPerkThroughNormalOffer(CGHeroInstance * hero, const std::string & perkId, int requiredRank)
	{
		const std::string skillId = newHorizonsNecromancy::SKILL_ID;
		const auto rankLookup = [hero](const std::string & requestedSkill)
		{
			return hero->getPerkSkillRank(requestedSkill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto candidate = std::find_if(offer.begin(), offer.end(), [&](const auto & entry)
			{
				return entry.selection.skillId == skillId && entry.selection.perkId == perkId;
			});
			if(candidate == offer.end() || candidate->requiredRank != requiredRank)
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), candidate));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			return true;
		}
		return false;
	}

	int32_t armyCreatureCount(const CGHeroInstance & hero, CreatureID creatureId) const
	{
		int32_t total = 0;
		for(const auto & [slot, stack] : hero.Slots())
		{
			(void)slot;
			if(stack->getCreatureID() == creatureId)
				total += stack->getCount();
		}
		return total;
	}

	void makeNeutralForCapacityTest(CGHeroInstance * hero)
	{
		for(const auto & bonus : hero->getHeroType()->specialty)
			hero->removeBonus(bonus);
		for(int i = 0; i < LIBRARY->skillh->size(); ++i)
			hero->setSecSkillLevel(SecondarySkill(i), 0, ChangeValueMode::ABSOLUTE);
		for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
			PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
			hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
	}

	void resolveBattleDialogsOnly()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
			}
		}
	}

	void resolveLevelUpDialogs()
	{
		for(int remainingLevelUps = 10; remainingLevelUps > 0; --remainingLevelUps)
		{
			auto followup = gameHandler->queries->topQuery(PlayerColor(0));
			if(!followup)
				break;
			ASSERT_EQ(followup->getType(), QueryType::HeroLevelUpDialog);
			ASSERT_TRUE(gameHandler->queryReply(followup->queryID, 0, PlayerColor(0)));
		}
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);
	}

	void resolveBattleDialogs()
	{
		resolveBattleDialogsOnly();
		resolveLevelUpDialogs();
	}

	std::unique_ptr<NecromancyAdmissionRecordingServer> recordingServer;
};

TEST_F(NewHorizonsNecromancyAdmissionAITest, LegalDeathLordRaisesMixedCoreAndEliteNonlivingCasualties)
{
	prepareAdvancedSpecialNecromancer(newHorizonsNecromancy::DEATH_LORD_ID, "Death Lord", "death_lord");
	setSpecialCasualties("core:ironGolem", "core:earthElemental", 60, 60, BonusType::NON_LIVING);
	resolveAdmissionBattle();

	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & result = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(result.active);
	EXPECT_EQ(result.rank, MasteryLevel::ADVANCED);
	EXPECT_EQ(result.eligibleCasualties, 0);
	EXPECT_EQ(result.deathLordCasualties, 120);
	EXPECT_EQ(result.graveKnowledgeCasualties, 0);
	EXPECT_EQ(result.deathLordSkeletons, 6);
	EXPECT_EQ(result.graveKnowledgeSkeletons, 0);
	EXPECT_EQ(result.skeletonsOffered, 6);
	EXPECT_EQ(result.zombiesRaised, 1);
	EXPECT_EQ(result.skeletonsRaised, 3);
	EXPECT_EQ(result.wightsRaised, 0);
	EXPECT_TRUE(result.darkConversionChosen);
	EXPECT_TRUE(result.applied);
	EXPECT_FALSE(result.blockedByArmyCapacity);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, creature("core:skeleton")), 19);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, creature("core:zombie")), 1);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, LegalGraveKnowledgeRaisesMixedCoreAndEliteUndeadCasualties)
{
	prepareAdvancedSpecialNecromancer(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID,
		"Grave Knowledge", "grave_knowledge");
	setSpecialCasualties("core:skeleton", "core:vampire", 15, 30, BonusType::UNDEAD);
	resolveAdmissionBattle();

	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & result = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(result.active);
	EXPECT_EQ(result.rank, MasteryLevel::ADVANCED);
	EXPECT_EQ(result.eligibleCasualties, 0);
	EXPECT_EQ(result.deathLordCasualties, 0);
	EXPECT_EQ(result.graveKnowledgeCasualties, 45);
	EXPECT_EQ(result.deathLordSkeletons, 0);
	EXPECT_EQ(result.graveKnowledgeSkeletons, 9);
	EXPECT_EQ(result.skeletonsOffered, 9);
	EXPECT_EQ(result.zombiesRaised, 1);
	EXPECT_EQ(result.skeletonsRaised, 6);
	EXPECT_EQ(result.wightsRaised, 0);
	EXPECT_TRUE(result.darkConversionChosen);
	EXPECT_TRUE(result.applied);
	EXPECT_FALSE(result.blockedByArmyCapacity);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, creature("core:skeleton")), 22);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, creature("core:zombie")), 1);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, NonlivingAndUndeadCasualtiesAreNotSpecialWithoutPerks)
{
	prepareAdvancedNecromancerWithoutSpecialPerk();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:ironGolem"), 50));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature("core:vampire"), 50));
	resolveAdmissionBattle();

	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & result = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(result.active);
	EXPECT_EQ(result.rank, MasteryLevel::ADVANCED);
	EXPECT_EQ(result.eligibleCasualties, 0);
	EXPECT_EQ(result.deathLordCasualties, 0);
	EXPECT_EQ(result.graveKnowledgeCasualties, 0);
	EXPECT_EQ(result.deathLordSkeletons, 0);
	EXPECT_EQ(result.graveKnowledgeSkeletons, 0);
	EXPECT_EQ(result.skeletonsOffered, 0);
	EXPECT_EQ(result.skeletonsRaised, 0);
	EXPECT_EQ(result.zombiesRaised, 0);
	EXPECT_EQ(result.wightsRaised, 0);
	EXPECT_FALSE(result.applied);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, creature("core:skeleton")), 16);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

/// Specialized admission fixture for post-battle Soul Harvester cases. It uses
/// the production registry unchanged so the ordinary legal-offer path is tested.
class NewHorizonsNecromancySoulHarvesterAdmissionAITest
	: public NewHorizonsNecromancyAdmissionAITest
{
protected:
	void assertSoulHarvesterRegistryRow()
	{
		const auto & perks = attackerSideHero->getPerkState().rules
			["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector();
		const auto soulHarvester = std::find_if(perks.begin(), perks.end(), [](const auto & perk)
		{
			return perk["id"].String() == newHorizonsNecromancy::SOUL_HARVESTER_ID;
		});
		ASSERT_NE(soulHarvester, perks.end());
		EXPECT_EQ((*soulHarvester)["name"].String(), "Soul Harvester");
		EXPECT_EQ((*soulHarvester)["requires"].String(), "advanced");
		EXPECT_EQ((*soulHarvester)["effect"]["status"].String(), "active");
		EXPECT_EQ((*soulHarvester)["description"].String(),
			"When resolving Necromancy, every complete group of 6 Skeletons generated from eligible Elite-tier casualties is automatically raised as 1 Wight. Wights remain Core-tier; the conversion's input is Elite-tier casualties, not its output.");
		EXPECT_EQ((*soulHarvester)["effect"]["description"].String(),
			(*soulHarvester)["description"].String());
		RecordProperty("soul_harvester_registry_id", (*soulHarvester)["id"].String());
		RecordProperty("soul_harvester_registry_name", (*soulHarvester)["name"].String());
		RecordProperty("soul_harvester_registry_required_rank", (*soulHarvester)["requires"].String());
		RecordProperty("soul_harvester_registry_description", (*soulHarvester)["description"].String());
		RecordProperty("soul_harvester_registry_effect_status", (*soulHarvester)["effect"]["status"].String());
	}

	void prepareAdvancedSoulHarvesterNecromancer()
	{
		prepareNecromancerArmy(false);
		assertSoulHarvesterRegistryRow();
		const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
		ASSERT_GE(necromancyIndex, 0);
		const SecondarySkill necromancy(necromancyIndex);
		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::DARK_CONVERSION_ID, MasteryLevel::BASIC));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DARK_CONVERSION_ID));

		// Advancing through CGameHandler exercises the ordinary preceding-tier gate.
		gameHandler->levelUpHero(attackerSideHero, necromancy, false);
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(necromancy), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::SOUL_HARVESTER_ID, MasteryLevel::ADVANCED));
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::SOUL_HARVESTER_ID));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::SOUL_HARVESTER_ID));
		RecordProperty("soul_harvester_activation_override_applied", "false");
		RecordProperty("soul_harvester_skill_rank", "advanced");
		RecordProperty("soul_harvester_selected", "true");
		RecordProperty("soul_harvester_selected_perk", newHorizonsNecromancy::SOUL_HARVESTER_ID);
		RecordProperty("soul_harvester_basic_perk", newHorizonsNecromancy::DARK_CONVERSION_ID);
	}

	void expectThreeOutputCapacityBlock(size_t freeSlotCount)
	{
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_LE(freeSlotCount, static_cast<size_t>(GameConstants::ARMY_SIZE - 1));
		prepareAdvancedSoulHarvesterNecromancer();
		fillFillerSlots(1, GameConstants::ARMY_SIZE - 1 - static_cast<int>(freeSlotCount));
		ASSERT_EQ(attackerSideHero->getFreeSlots().size(), freeSlotCount);

		const auto skeleton = creature("core:skeleton");
		const auto zombie = creature("core:zombie");
		const auto wight = creature("core:wight");
		setMixedCoreEliteCasualties(65, 65);
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		resolveBattleDialogsOnly();
		const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
		EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
		resolveLevelUpDialogs();
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);

		ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
		ASSERT_EQ(recordingServer->battleResults.size(), 1u);
		const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
		ASSERT_TRUE(necromancyResult.active);
		EXPECT_EQ(necromancyResult.eligibleCasualties, 130);
		EXPECT_EQ(necromancyResult.skeletonsOffered, 26);
		EXPECT_TRUE(necromancyResult.blockedByArmyCapacity);
		EXPECT_FALSE(necromancyResult.applied);
		EXPECT_FALSE(necromancyResult.darkConversionChosen);
		EXPECT_EQ(necromancyResult.skeletonsRaised, 0);
		EXPECT_EQ(necromancyResult.zombiesRaised, 0);
		EXPECT_EQ(necromancyResult.wightsRaised, 0);
		EXPECT_EQ(necromancyResult.manaRecovered, 0);
		EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
		EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 16);
		EXPECT_EQ(armyCreatureCount(*attackerSideHero, zombie), 0);
		EXPECT_EQ(armyCreatureCount(*attackerSideHero, wight), 0);
		EXPECT_EQ(recordingServer->systemMessages, 0);
	}

	void resolveBattleWithoutNecromancyQuery()
	{
		resolveBattleDialogsOnly();
		const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
		EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
		resolveLevelUpDialogs();
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);
	}
};

/// Master of Bones must be selected through ordinary rank-gated offers; the
/// town setup varies only the ownership/build state of the configured upgrade.
class NewHorizonsNecromancyMasterOfBonesAdmissionAITest
	: public NewHorizonsNecromancyAdmissionAITest
{
protected:
	void assertMasterOfBonesRegistryRow()
	{
		const auto & perks = attackerSideHero->getPerkState().rules
			["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector();
		const auto masterOfBones = std::find_if(perks.begin(), perks.end(), [](const auto & perk)
		{
			return perk["id"].String() == newHorizonsNecromancy::MASTER_OF_BONES_ID;
		});
		ASSERT_NE(masterOfBones, perks.end());
		EXPECT_EQ((*masterOfBones)["name"].String(), "Master of Bones");
		EXPECT_EQ((*masterOfBones)["requires"].String(), "expert");
		EXPECT_EQ((*masterOfBones)["effect"]["status"].String(), "active");
		EXPECT_EQ((*masterOfBones)["description"].String(),
			"Skeletons raised by Necromancy are raised as their upgraded form when the appropriate Necropolis upgrade is available to the player.");
		EXPECT_EQ((*masterOfBones)["effect"]["description"].String(),
			(*masterOfBones)["description"].String());
		RecordProperty("master_of_bones_registry_id", (*masterOfBones)["id"].String());
		RecordProperty("master_of_bones_registry_name", (*masterOfBones)["name"].String());
		RecordProperty("master_of_bones_registry_required_rank", (*masterOfBones)["requires"].String());
		RecordProperty("master_of_bones_registry_description", (*masterOfBones)["description"].String());
		RecordProperty("master_of_bones_registry_effect_status", (*masterOfBones)["effect"]["status"].String());
	}

	void prepareExpertMasterOfBonesNecromancer()
	{
		prepareNecromancerArmy(false);
		assertMasterOfBonesRegistryRow();
		const auto necromancyId = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
		ASSERT_GE(necromancyId, 0);
		const SecondarySkill necromancy(necromancyId);

		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::CORPSE_PRESERVATION_ID, MasteryLevel::BASIC));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
		gameHandler->levelUpHero(attackerSideHero, necromancy, false);
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(necromancy), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::SOUL_HARVESTER_ID, MasteryLevel::ADVANCED));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::SOUL_HARVESTER_ID));
		gameHandler->levelUpHero(attackerSideHero, necromancy, false);
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(necromancy), MasteryLevel::EXPERT);
		ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
			newHorizonsNecromancy::MASTER_OF_BONES_ID, MasteryLevel::EXPERT));
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::MASTER_OF_BONES_ID));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::MASTER_OF_BONES_ID));

		RecordProperty("master_of_bones_activation_override_applied", "false");
		RecordProperty("master_of_bones_skill_rank", "expert");
		RecordProperty("master_of_bones_selected", "true");
		RecordProperty("master_of_bones_selected_perk", newHorizonsNecromancy::MASTER_OF_BONES_ID);
		RecordProperty("master_of_bones_basic_perk", newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
		RecordProperty("master_of_bones_advanced_perk", newHorizonsNecromancy::SOUL_HARVESTER_ID);
	}

	void expectPostBattleSkeletonForm(CreatureID expectedForm)
	{
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		prepareExpertMasterOfBonesNecromancer();

		const auto baseSkeleton = creature("core:skeleton");
		const auto skeletonWarrior = creature("core:skeletonWarrior");
		const auto pikeman = creature("core:pikeman");
		ASSERT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 10));

		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		resolveBattleDialogsOnly();
		const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
		EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
		resolveLevelUpDialogs();
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);

		ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
		ASSERT_EQ(recordingServer->battleResults.size(), 1u);
		const auto & result = recordingServer->battleResults.back().necromancy;
		ASSERT_TRUE(result.active);
		EXPECT_EQ(result.rank, MasteryLevel::EXPERT);
		EXPECT_EQ(result.eligibleCasualties, 10);
		EXPECT_EQ(result.skeletonsOffered, 3);
		EXPECT_EQ(result.skeletonsRaised, 3);
		EXPECT_EQ(result.skeletonCreature, expectedForm);
		EXPECT_EQ(result.raisedCreature, expectedForm == CreatureID::NONE ? baseSkeleton : expectedForm);
		EXPECT_TRUE(result.applied);
		EXPECT_FALSE(result.blockedByArmyCapacity);
		EXPECT_EQ(armyCreatureCount(*attackerSideHero, baseSkeleton), 16 + (expectedForm == CreatureID::NONE ? 3 : 0));
		EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeletonWarrior), expectedForm == CreatureID::NONE ? 0 : 3);
		EXPECT_EQ(recordingServer->systemMessages, 0);
	}
};

class NewHorizonsNecromancyMasterOfBonesOwnedBuiltAdmissionAITest
	: public NewHorizonsNecromancyMasterOfBonesAdmissionAITest
{
public:
	NewHorizonsNecromancyMasterOfBonesOwnedBuiltAdmissionAITest()
	{
		skeletonDwellingScenario = SkeletonDwellingScenario::OWNED_UPGRADE_BUILT;
	}
};

class NewHorizonsNecromancyMasterOfBonesOwnedUnbuiltAdmissionAITest
	: public NewHorizonsNecromancyMasterOfBonesAdmissionAITest
{
public:
	NewHorizonsNecromancyMasterOfBonesOwnedUnbuiltAdmissionAITest()
	{
		skeletonDwellingScenario = SkeletonDwellingScenario::OWNED_UPGRADE_UNBUILT;
	}
};

class NewHorizonsNecromancyMasterOfBonesForeignBuiltAdmissionAITest
	: public NewHorizonsNecromancyMasterOfBonesAdmissionAITest
{
public:
	NewHorizonsNecromancyMasterOfBonesForeignBuiltAdmissionAITest()
	{
		skeletonDwellingScenario = SkeletonDwellingScenario::FOREIGN_UPGRADE_BUILT;
	}
};

class NewHorizonsNecromancyMasterOfBonesNoTownAdmissionAITest
	: public NewHorizonsNecromancyMasterOfBonesAdmissionAITest
{
};

TEST_F(NewHorizonsNecromancyMasterOfBonesOwnedBuiltAdmissionAITest, PostBattleRaisesAsConfiguredSkeletonWarrior)
{
	ASSERT_NE(skeletonDwellingTown, nullptr);
	ASSERT_TRUE(skeletonDwellingTown->hasBuilt(BuildingID::DWELL_LVL_1_UP));
	expectPostBattleSkeletonForm(creature("core:skeletonWarrior"));
}

TEST_F(NewHorizonsNecromancyMasterOfBonesOwnedUnbuiltAdmissionAITest, BuildableUpgradeAloneKeepsBaseSkeletonForm)
{
	ASSERT_NE(skeletonDwellingTown, nullptr);
	ASSERT_TRUE(skeletonDwellingTown->getTown()->buildings.contains(BuildingID::DWELL_LVL_1_UP));
	ASSERT_FALSE(skeletonDwellingTown->hasBuilt(BuildingID::DWELL_LVL_1_UP));
	expectPostBattleSkeletonForm(CreatureID::NONE);
}

TEST_F(NewHorizonsNecromancyMasterOfBonesForeignBuiltAdmissionAITest, ForeignBuiltUpgradeKeepsBaseSkeletonForm)
{
	ASSERT_NE(skeletonDwellingTown, nullptr);
	ASSERT_TRUE(skeletonDwellingTown->hasBuilt(BuildingID::DWELL_LVL_1_UP));
	ASSERT_EQ(skeletonDwellingTown->getOwner(), PlayerColor(1));
	expectPostBattleSkeletonForm(CreatureID::NONE);
}

TEST_F(NewHorizonsNecromancyMasterOfBonesNoTownAdmissionAITest, MissingUpgradeKeepsBaseSkeletonForm)
{
	ASSERT_EQ(skeletonDwellingTown, nullptr);
	expectPostBattleSkeletonForm(CreatureID::NONE);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, CasualtySnapshotExcludesUndeadAndNonliving)
{
	const auto living = creature("core:pikeman");
	const auto undead = creature("core:skeleton");
	const auto nonliving = creature("core:ironGolem");
	ASSERT_TRUE(living.toCreature());
	ASSERT_TRUE(undead.toCreature());
	ASSERT_TRUE(nonliving.toCreature());
	ASSERT_TRUE(undead.toCreature()->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_TRUE(nonliving.toCreature()->hasBonusOfType(BonusType::NON_LIVING));

	std::map<CreatureID, si32> casualties;
	casualties[living] = 7;
	casualties[undead] = 11;
	casualties[nonliving] = 13;
	casualties[CreatureID::NONE] = 99;
	EXPECT_EQ(newHorizonsNecromancy::countLivingEligibleCasualties(casualties), 7);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, QueryRejectsForgedChoiceAndAcceptsOnlyServerOffer)
{
	const auto skeleton = creature("core:skeleton");
	const auto zombie = creature("core:zombie");
	std::optional<CreatureID> selected;
	auto query = std::make_shared<CNecromancyQuery>(gameHandler.get(), PlayerColor(0),
		std::vector<CreatureID>{skeleton, zombie},
		[&selected](std::optional<CreatureID> choice){ selected = choice; });

	EXPECT_FALSE(query->isValidReply(std::nullopt));
	EXPECT_FALSE(query->isValidReply(0));
	EXPECT_FALSE(query->isValidReply(3));
	EXPECT_TRUE(query->isValidReply(1));
	EXPECT_TRUE(query->isValidReply(2));

	query->setReply(3);
	query->onRemoval(PlayerColor(0));
	EXPECT_FALSE(selected.has_value());
	query->setReply(2);
	query->onRemoval(PlayerColor(0));
	ASSERT_TRUE(selected.has_value());
	EXPECT_EQ(*selected, zombie);
}

TEST_F(NewHorizonsNecromancyAITest, ComputerWinnerAutomaticallyConvertsCompleteCoreGroups)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	CGHeroInstance * const defeatedHero = defenderSideHero;

	// BattleTestFixture uses a Castle hero for its compact setup.  Switching the
	// prototype before the battle is enough for the saved faction identity and
	// New Horizons rank path used by this focused post-battle test.
	attackerSideHero->setHeroType(HeroTypeID(72)); // Septienna, Necropolis.
	const auto necromancy = SecondarySkill::decode("new-horizons:necromancy");
	ASSERT_GE(necromancy, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(necromancy), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"});
	ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"));
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_GE(attackerSideHero->getFreeSlots().size(), 2u);

	// Keep the casualties in the original army and use an explicitly classified
	// Core creature. Basic Necromancy creates 10 Skeletons from 101 casualties;
	// the automatic conversion leaves 3 Zombies and the one-Skeleton remainder.
	const auto pikeman = creature("core:pikeman");
	const auto pikemanCategory = gameState()->getCreatureCategory(pikeman);
	ASSERT_TRUE(pikemanCategory);
	ASSERT_EQ(pikemanCategory->category, newHorizonsCreatures::CreatureCategory::CORE);
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 101));
	const auto skeleton = creature("core:skeleton");
	const auto zombie = creature("core:zombie");
	const auto armyCount = [](const CGHeroInstance & hero, CreatureID creatureId)
	{
		int32_t total = 0;
		for(const auto & [slot, stack] : hero.Slots())
		{
			(void)slot;
			if(stack->getCreatureID() == creatureId)
				total += stack->getCount();
		}
		return total;
	};
	const int32_t skeletonsBefore = armyCount(*attackerSideHero, skeleton);
	const int32_t zombiesBefore = armyCount(*attackerSideHero, zombie);
	// Level-up packets are emitted only after the adventure interface is ready.
	// These simulated controllers answer their ordinary dialogs directly.
	gameHandler->onAdvInterfaceReady(PlayerColor(0));
	gameHandler->onAdvInterfaceReady(PlayerColor(1));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));

	// Resolve ordinary battle-result dialogs; Dark Conversion itself is
	// automatic and does not insert a choice query for the computer winner.
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto dialog = gameHandler->queries->topQuery(player);
		if(dialog && dialog->getType() == QueryType::BattleDialog)
		{
			ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
		}
	}

	const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
	EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
	for(int remainingLevelUps = 10; remainingLevelUps > 0; --remainingLevelUps)
	{
		auto levelUp = gameHandler->queries->topQuery(PlayerColor(0));
		if(!levelUp)
			break;
		ASSERT_EQ(levelUp->getType(), QueryType::HeroLevelUpDialog);
		ASSERT_TRUE(gameHandler->queryReply(levelUp->queryID, 0, PlayerColor(0)));
	}
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);

	const auto zombieSlot = attackerSideHero->getSlotFor(zombie);
	const auto skeletonSlot = attackerSideHero->getSlotFor(skeleton);
	ASSERT_TRUE(zombieSlot.validSlot());
	ASSERT_TRUE(skeletonSlot.validSlot());
	ASSERT_TRUE(attackerSideHero->hasStackAtSlot(zombieSlot));
	ASSERT_TRUE(attackerSideHero->hasStackAtSlot(skeletonSlot));
	EXPECT_NE(zombieSlot, skeletonSlot);
	EXPECT_EQ(armyCount(*attackerSideHero, zombie) - zombiesBefore, 3);
	EXPECT_EQ(armyCount(*attackerSideHero, skeleton) - skeletonsBefore, 1);
	EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);

	// The defeated hero is retained in the pool, not destroyed. It must no
	// longer point at the BattleInfo that BattleEnded just erased; post-battle
	// magic-rule and mana reads must remain safe on that pooled object.
	ASSERT_EQ(defeatedHero->battle, nullptr);
	auto * pooledDefeatedHero = gameState()->getMap().tryGetFromHeroPool(defeatedHero->getHeroTypeID());
	ASSERT_EQ(pooledDefeatedHero, defeatedHero);
	ASSERT_EQ(pooledDefeatedHero->battle, nullptr);
	EXPECT_TRUE(newHorizonsMagic::spellPointRulesActive(pooledDefeatedHero->getMagicRules()));
	EXPECT_LE(pooledDefeatedHero->getNormalSpellPoints(), pooledDefeatedHero->manaLimit());
	EXPECT_GE(pooledDefeatedHero->getBufferSpellPoints(), 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, PostBattleRaisesSkeletonsInSpareSlotWhenExistingStackIsAtLeadershipCap)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(false);

	const auto gargoyle = creature("core:stoneGargoyle");
	const auto skeleton = creature("core:skeleton");
	ASSERT_TRUE(gargoyle.toCreature());
	ASSERT_FALSE(gargoyle.toCreature()->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_FALSE(gargoyle.toCreature()->hasBonusOfType(BonusType::NON_LIVING));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), gargoyle, 100));

	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogs();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	ASSERT_EQ(necromancyResult.eligibleCasualties, 100);
	ASSERT_EQ(necromancyResult.rank, 1);
	const auto expected = resolve(necromancyResult.rank,
		necromancyResult.eligibleCasualties, 0, false, false, false, true, true, 0, 0);
	ASSERT_TRUE(expected.applied);
	EXPECT_EQ(necromancyResult.skeletonsRaised, expected.skeletonsRaised);
	EXPECT_FALSE(necromancyResult.blockedByArmyCapacity);
	EXPECT_TRUE(necromancyResult.applied);

	int32_t totalSkeletons = 0;
	int32_t skeletonStacks = 0;
	for(const auto & [slot, stack] : attackerSideHero->Slots())
	{
		if(stack->getCreatureID() == skeleton)
		{
			totalSkeletons += stack->getCount();
			++skeletonStacks;
		}
		const auto capacity = attackerSideHero->getLeadershipSlotCapacity(stack->getCreatureID());
		ASSERT_TRUE(capacity);
		EXPECT_LE(stack->getCount(), capacity->maximum) << "slot " << slot.getNum();
	}
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(totalSkeletons, 16 + expected.skeletonsRaised);
	EXPECT_EQ(skeletonStacks, 2);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, PostBattleNecromancyWithNoFreeSlotIsReportedBlockedWithoutComplaint)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(true);
	ASSERT_EQ(attackerSideHero->stacksCount(), GameConstants::ARMY_SIZE);
	ASSERT_TRUE(attackerSideHero->getFreeSlots().empty());

	const auto gargoyle = creature("core:stoneGargoyle");
	const auto skeleton = creature("core:skeleton");
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), gargoyle, 100));

	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogs();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	ASSERT_EQ(necromancyResult.eligibleCasualties, 100);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 10);
	EXPECT_TRUE(necromancyResult.blockedByArmyCapacity);
	EXPECT_FALSE(necromancyResult.applied);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 0);
	EXPECT_EQ(necromancyResult.zombiesRaised, 0);

	int32_t totalSkeletons = 0;
	for(const auto & [slot, stack] : attackerSideHero->Slots())
	{
		if(stack->getCreatureID() == skeleton)
			totalSkeletons += stack->getCount();
		const auto capacity = attackerSideHero->getLeadershipSlotCapacity(stack->getCreatureID());
		ASSERT_TRUE(capacity);
		EXPECT_LE(stack->getCount(), capacity->maximum) << "slot " << slot.getNum();
	}
	EXPECT_EQ(totalSkeletons, 16);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, DarkConversionAutomaticallyConvertsCoreShareOfMixedTierCasualties)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(false);

	attackerSideHero->applyPerkSelection({
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"));
	ASSERT_GE(attackerSideHero->getFreeSlots().size(), 2u);

	const auto skeleton = creature("core:skeleton");
	const auto zombie = creature("core:zombie");
	setMixedCoreEliteCasualties(60, 40);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogsOnly();
	const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
	EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
	resolveLevelUpDialogs();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	ASSERT_TRUE(necromancyResult.applied);
	EXPECT_EQ(necromancyResult.rank, 1);
	EXPECT_EQ(necromancyResult.eligibleCasualties, 100);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 10);
	EXPECT_TRUE(necromancyResult.darkConversionAvailable);
	EXPECT_TRUE(necromancyResult.darkConversionChosen);
	EXPECT_FALSE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 4);
	EXPECT_EQ(necromancyResult.zombiesRaised, 2);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 20);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, zombie), 2);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancySoulHarvesterAdmissionAITest, PostBattleResolvesCoreAndEliteConversionsTogether)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareAdvancedSoulHarvesterNecromancer();
	setMixedCoreEliteCasualties(60, 60);

	const auto skeleton = creature("core:skeleton");
	const auto zombie = creature("core:zombie");
	const auto wight = creature("core:wight");
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleWithoutNecromancyQuery();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	EXPECT_TRUE(necromancyResult.applied);
	EXPECT_FALSE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.rank, MasteryLevel::ADVANCED);
	EXPECT_EQ(necromancyResult.eligibleCasualties, 120);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 24);
	EXPECT_TRUE(necromancyResult.darkConversionChosen);
	EXPECT_EQ(necromancyResult.zombiesRaised, 4);
	EXPECT_EQ(necromancyResult.wightsRaised, 2);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 0);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 16);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, zombie), 4);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, wight), 2);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancySoulHarvesterAdmissionAITest, PostBattleAdmissionKeepsRemainderAcrossThreeOutputKinds)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareAdvancedSoulHarvesterNecromancer();
	ASSERT_GE(attackerSideHero->getFreeSlots().size(), 3u);
	setMixedCoreEliteCasualties(65, 65);

	const auto skeleton = creature("core:skeleton");
	const auto zombie = creature("core:zombie");
	const auto wight = creature("core:wight");
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleWithoutNecromancyQuery();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	EXPECT_TRUE(necromancyResult.applied);
	EXPECT_FALSE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.eligibleCasualties, 130);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 26);
	EXPECT_EQ(necromancyResult.zombiesRaised, 4);
	EXPECT_EQ(necromancyResult.wightsRaised, 2);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 2);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 18);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, zombie), 4);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, wight), 2);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancySoulHarvesterAdmissionAITest, ThreeOutputAdmissionRejectsOneFreeSlotAtomically)
{
	expectThreeOutputCapacityBlock(1);
}

TEST_F(NewHorizonsNecromancySoulHarvesterAdmissionAITest, ThreeOutputAdmissionRejectsTwoFreeSlotsAtomically)
{
	expectThreeOutputCapacityBlock(2);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, EliteCasualtiesStaySkeletonsWithoutSelectedSoulHarvester)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(false);
	const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
	ASSERT_GE(necromancyIndex, 0);
	const SecondarySkill necromancy(necromancyIndex);
	ASSERT_TRUE(selectPerkThroughNormalOffer(attackerSideHero,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID, MasteryLevel::BASIC));
	gameHandler->levelUpHero(attackerSideHero, necromancy, false);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(necromancy), MasteryLevel::ADVANCED);
	const auto & registeredPerks = attackerSideHero->getPerkState().rules
		["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector();
	const auto soulHarvester = std::find_if(registeredPerks.begin(), registeredPerks.end(), [](const auto & perk)
	{
		return perk["id"].String() == newHorizonsNecromancy::SOUL_HARVESTER_ID;
	});
	ASSERT_NE(soulHarvester, registeredPerks.end());
	EXPECT_EQ((*soulHarvester)["effect"]["status"].String(), "active");
	EXPECT_EQ((*soulHarvester)["requires"].String(), "advanced");
	ASSERT_FALSE(attackerSideHero->getPerkState().hasSelection(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::SOUL_HARVESTER_ID));
	ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::SOUL_HARVESTER_ID));
	RecordProperty("soul_harvester_registry_id", (*soulHarvester)["id"].String());
	RecordProperty("soul_harvester_registry_name", (*soulHarvester)["name"].String());
	RecordProperty("soul_harvester_registry_required_rank", (*soulHarvester)["requires"].String());
	RecordProperty("soul_harvester_registry_description", (*soulHarvester)["description"].String());
	RecordProperty("soul_harvester_registry_effect_status", (*soulHarvester)["effect"]["status"].String());
	RecordProperty("soul_harvester_activation_override_applied", "false");
	RecordProperty("soul_harvester_skill_rank", "advanced");
	RecordProperty("soul_harvester_selected", "false");
	RecordProperty("soul_harvester_selected_perk", "none");
	RecordProperty("soul_harvester_basic_perk", newHorizonsNecromancy::CORPSE_PRESERVATION_ID);

	const auto skeleton = creature("core:skeleton");
	const auto monk = creature("core:monk");
	const auto wight = creature("core:wight");
	const auto monkCategory = gameState()->getCreatureCategory(monk);
	ASSERT_TRUE(monkCategory);
	ASSERT_EQ(monkCategory->category, newHorizonsCreatures::CreatureCategory::ELITE);
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), monk, 60));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogsOnly();
	const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
	EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
	resolveLevelUpDialogs();
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	EXPECT_EQ(necromancyResult.eligibleCasualties, 60);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 12);
	EXPECT_TRUE(necromancyResult.applied);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 12);
	EXPECT_EQ(necromancyResult.wightsRaised, 0);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 28);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, wight), 0);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, DarkConversionRejectsMixedAutomaticOutputAtomicallyWhenOneSlotRemains)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(false);
	fillFillerSlots(1, GameConstants::ARMY_SIZE - 2);
	ASSERT_EQ(attackerSideHero->getFreeSlots().size(), 1u);
	attackerSideHero->applyPerkSelection({
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"));

	const auto skeleton = creature("core:skeleton");
	const auto zombie = creature("core:zombie");
	setMixedCoreEliteCasualties(60, 40);
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogsOnly();
	const auto followup = gameHandler->queries->topQuery(PlayerColor(0));
	EXPECT_TRUE(!followup || followup->getType() != QueryType::NecromancyChoice);
	resolveLevelUpDialogs();
	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	EXPECT_EQ(necromancyResult.rank, 1);
	EXPECT_EQ(necromancyResult.eligibleCasualties, 100);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 10);
	EXPECT_TRUE(necromancyResult.darkConversionAvailable);
	EXPECT_FALSE(necromancyResult.darkConversionChosen);
	EXPECT_FALSE(necromancyResult.applied);
	EXPECT_TRUE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 0);
	EXPECT_EQ(necromancyResult.zombiesRaised, 0);
	EXPECT_EQ(necromancyResult.manaRecovered, 0);
	EXPECT_EQ(recordingServer->battleResults.back().raisedStack.getCreature(), nullptr);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 16);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, zombie), 0);
	for(const auto & [slot, stack] : attackerSideHero->Slots())
	{
		const auto capacity = attackerSideHero->getLeadershipSlotCapacity(stack->getCreatureID());
		ASSERT_TRUE(capacity);
		EXPECT_LE(stack->getCount(), capacity->maximum) << "slot " << slot.getNum();
	}
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, PostBattleFillsCapacityAcrossDuplicateSkeletonStacks)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(false);

	const auto skeleton = creature("core:skeleton");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, 11));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), skeleton, 11));
	fillFillerSlots(2, GameConstants::ARMY_SIZE - 2);
	ASSERT_EQ(attackerSideHero->stacksCount(), GameConstants::ARMY_SIZE);
	ASSERT_TRUE(attackerSideHero->getFreeSlots().empty());

	const auto gargoyle = creature("core:stoneGargoyle");
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), gargoyle, 100));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogs();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	ASSERT_TRUE(necromancyResult.applied);
	EXPECT_FALSE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 10);

	int32_t skeletonStacks = 0;
	int32_t totalSkeletons = 0;
	for(const auto & [slot, stack] : attackerSideHero->Slots())
	{
		if(stack->getCreatureID() == skeleton)
		{
			++skeletonStacks;
			totalSkeletons += stack->getCount();
		}
		const auto capacity = attackerSideHero->getLeadershipSlotCapacity(stack->getCreatureID());
		ASSERT_TRUE(capacity);
		EXPECT_LE(stack->getCount(), capacity->maximum) << "slot " << slot.getNum();
	}
	EXPECT_EQ(skeletonStacks, 2);
	EXPECT_EQ(totalSkeletons, 32);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 16);
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAITest, BattleResultExcludesDestroyRemainsCasualtiesAfterGhostRemoval)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);

	attackerSideHero->setHeroType(HeroTypeID(72)); // Septienna, Necropolis.
	const auto necromancy = SecondarySkill::decode("new-horizons:necromancy");
	ASSERT_GE(necromancy, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(necromancy), MasteryLevel::ADVANCED,
		ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());

	const auto pikeman = creature("core:pikeman");
	const auto skeleton = creature("core:skeleton");
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 10));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), skeleton, 1));

	// Keep the ghost in the battle's stack list, but remove it through the same
	// BattleUnitsChanged path used by BattleFlowProcessor before finalization.
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	CStack * target = nullptr;
	for(const auto * stack : battle()->battleGetStacksIf([](const CStack *) { return true; }))
	{
		if(stack->unitSide() == BattleSide::DEFENDER && stack->creatureId() == pikeman)
		{
			target = const_cast<CStack *>(stack);
			break;
		}
	}
	ASSERT_NE(target, nullptr);

	const auto applyDamage = [this](CStack * stack, int64_t amount, bool destroyRemains)
	{
		BattleStackAttacked attacked;
		attacked.stackAttacked = stack->unitId();
		attacked.damageAmount = amount;
		stack->prepareAttacked(attacked, gameHandler->getRandomGenerator(), destroyRemains);
		ASSERT_EQ(attacked.newState.id, stack->unitId());
		ASSERT_EQ(attacked.newState.healthDelta, -amount);
		if(destroyRemains)
		{
			ASSERT_EQ(attacked.killedAmount, 3);
			ASSERT_EQ(attacked.newState.data["state"]["health"]["unusableRemains"].Integer(), 3);
			ASSERT_EQ(attacked.newState.data["state"]["health"]["fullUnits"].Integer(), 0);
			ASSERT_EQ(attacked.newState.data["state"]["health"]["firstHPleft"].Integer(), 0);
		}

		BattleUnitsChanged injured;
		injured.battleID = BattleID(0);
		injured.changedStacks.emplace_back(attacked.newState.id, UnitChanges::EOperation::UPDATE);
		injured.changedStacks.back().data = std::move(attacked.newState.data);
		injured.changedStacks.back().healthDelta = attacked.newState.healthDelta;
		gameHandler->sendAndApply(injured);
	};

	const int64_t unitHealth = target->getMaxHealth();
	applyDamage(target, unitHealth * 7, false);
	ASSERT_EQ(target->getCount(), 3);
	applyDamage(target, unitHealth * 3, true);
	ASSERT_EQ(target->getCount(), 0);
	ASSERT_EQ(target->getUnusableRemains(), 3);
	ASSERT_EQ(target->getKilled(), 10);
	ASSERT_FALSE(target->alive());
	ASSERT_EQ(target->getUnusableRemains(), 3);

	BattleUnitsChanged removeGhost;
	removeGhost.battleID = BattleID(0);
	removeGhost.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(removeGhost);
	ASSERT_TRUE(target->isGhost());
	ASSERT_EQ(target->getUnusableRemains(), 3);

	// The remaining Skeleton keeps the battle alive long enough for the first
	// stack to be removed.  It is ineligible itself, so only the seven ordinary
	// Pikeman casualties should feed Advanced Necromancy (20% => one Skeleton).
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto dialog = gameHandler->queries->topQuery(player);
		if(dialog && dialog->getType() == QueryType::BattleDialog)
		{
			ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
		}
	}

	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);
	const auto skeletonSlot = attackerSideHero->getSlotFor(skeleton);
	ASSERT_TRUE(skeletonSlot.validSlot());
	ASSERT_TRUE(attackerSideHero->hasStackAtSlot(skeletonSlot));
	EXPECT_EQ(attackerSideHero->getStackCount(skeletonSlot), 1);
	EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
}

TEST(NewHorizonsNecromancy, ResultSummaryAndEligibilityAreVersionGated)
{
	BattleResultsApplied outgoing;
	outgoing.battleID = BattleID(1);
	outgoing.necromancy.active = true;
	outgoing.necromancy.rank = 3;
	outgoing.necromancy.percentage = 35;
	outgoing.necromancy.eligibleCasualties = 100;
	outgoing.necromancy.skeletonsOffered = 35;
	outgoing.necromancy.skeletonsRaised = 2;
	outgoing.necromancy.zombiesRaised = 11;
	outgoing.necromancy.wightsRaised = 7;
	outgoing.necromancy.deathLordCasualties = 31;
	outgoing.necromancy.graveKnowledgeCasualties = 29;
	outgoing.necromancy.deathLordSkeletons = 8;
	outgoing.necromancy.graveKnowledgeSkeletons = 5;
	outgoing.necromancy.manaRecovered = 10;
	outgoing.necromancy.raisedCreature = creature("core:zombie");
	outgoing.necromancy.skeletonCreature = creature("core:skeletonWarrior");

	CMemorySerializer wire;
	wire.oser & outgoing;
	BattleResultsApplied incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.necromancy.rank, 3);
	EXPECT_EQ(incoming.necromancy.skeletonsRaised, 2);
	EXPECT_EQ(incoming.necromancy.zombiesRaised, 11);
	EXPECT_EQ(incoming.necromancy.wightsRaised, 7);
	EXPECT_EQ(incoming.necromancy.deathLordCasualties, 31);
	EXPECT_EQ(incoming.necromancy.graveKnowledgeCasualties, 29);
	EXPECT_EQ(incoming.necromancy.deathLordSkeletons, 8);
	EXPECT_EQ(incoming.necromancy.graveKnowledgeSkeletons, 5);
	EXPECT_EQ(incoming.necromancy.manaRecovered, 10);
	EXPECT_EQ(incoming.necromancy.skeletonCreature, creature("core:skeletonWarrior"));

	CMemorySerializer directWire;
	directWire.oser & outgoing.necromancy;
	newHorizonsNecromancy::NecromancyResult directIncoming;
	directWire.iser & directIncoming;
	EXPECT_EQ(directIncoming.wightsRaised, 7);
	EXPECT_EQ(directIncoming.deathLordCasualties, 31);
	EXPECT_EQ(directIncoming.graveKnowledgeCasualties, 29);
	EXPECT_EQ(directIncoming.deathLordSkeletons, 8);
	EXPECT_EQ(directIncoming.graveKnowledgeSkeletons, 5);
	EXPECT_EQ(directIncoming.skeletonCreature, creature("core:skeletonWarrior"));

	const auto necromancyWithoutWightsVersion = ESerializationVersion::NEW_HORIZONS_NECROMANCY;
	newHorizonsNecromancy::NecromancyResult legacyResult = outgoing.necromancy;
	legacyResult.wightsRaised = 0;
	legacyResult.deathLordCasualties = 0;
	legacyResult.graveKnowledgeCasualties = 0;
	legacyResult.deathLordSkeletons = 0;
	legacyResult.graveKnowledgeSkeletons = 0;
	legacyResult.skeletonCreature = CreatureID::NONE;
	CMemorySerializer legacyResultWire;
	legacyResultWire.oser.version = necromancyWithoutWightsVersion;
	legacyResultWire.oser & legacyResult;
	legacyResultWire.iser.version = necromancyWithoutWightsVersion;
	newHorizonsNecromancy::NecromancyResult legacyResultIncoming;
	legacyResultIncoming.wightsRaised = 9;
	legacyResultIncoming.deathLordCasualties = 31;
	legacyResultIncoming.graveKnowledgeCasualties = 29;
	legacyResultIncoming.deathLordSkeletons = 8;
	legacyResultIncoming.graveKnowledgeSkeletons = 5;
	legacyResultIncoming.skeletonCreature = creature("core:skeletonWarrior");
	legacyResultWire.iser & legacyResultIncoming;
	EXPECT_EQ(legacyResultIncoming.wightsRaised, 0);
	EXPECT_EQ(legacyResultIncoming.deathLordCasualties, 0);
	EXPECT_EQ(legacyResultIncoming.graveKnowledgeCasualties, 0);
	EXPECT_EQ(legacyResultIncoming.deathLordSkeletons, 0);
	EXPECT_EQ(legacyResultIncoming.graveKnowledgeSkeletons, 0);
	EXPECT_EQ(legacyResultIncoming.skeletonCreature, CreatureID::NONE);

	CMemorySerializer legacyOuterWire;
	legacyOuterWire.oser.version = necromancyWithoutWightsVersion;
	BattleResultsApplied legacyOuter = outgoing;
	legacyOuter.necromancy.wightsRaised = 0;
	legacyOuter.necromancy.deathLordCasualties = 0;
	legacyOuter.necromancy.graveKnowledgeCasualties = 0;
	legacyOuter.necromancy.deathLordSkeletons = 0;
	legacyOuter.necromancy.graveKnowledgeSkeletons = 0;
	legacyOuter.necromancy.skeletonCreature = CreatureID::NONE;
	legacyOuterWire.oser & legacyOuter;
	legacyOuterWire.iser.version = necromancyWithoutWightsVersion;
	BattleResultsApplied legacyOuterIncoming;
	legacyOuterIncoming.necromancy.wightsRaised = 9;
	legacyOuterIncoming.necromancy.deathLordCasualties = 31;
	legacyOuterIncoming.necromancy.graveKnowledgeCasualties = 29;
	legacyOuterIncoming.necromancy.deathLordSkeletons = 8;
	legacyOuterIncoming.necromancy.graveKnowledgeSkeletons = 5;
	legacyOuterIncoming.necromancy.skeletonCreature = creature("core:skeletonWarrior");
	legacyOuterWire.iser & legacyOuterIncoming;
	EXPECT_EQ(legacyOuterIncoming.necromancy.wightsRaised, 0);
	EXPECT_EQ(legacyOuterIncoming.necromancy.deathLordCasualties, 0);
	EXPECT_EQ(legacyOuterIncoming.necromancy.graveKnowledgeCasualties, 0);
	EXPECT_EQ(legacyOuterIncoming.necromancy.deathLordSkeletons, 0);
	EXPECT_EQ(legacyOuterIncoming.necromancy.graveKnowledgeSkeletons, 0);
	EXPECT_EQ(legacyOuterIncoming.necromancy.skeletonCreature, CreatureID::NONE);

	const auto necromancyWithoutSkeletonFormVersion = ESerializationVersion::NEW_HORIZONS_NECROMANCY_WIGHTS;
	newHorizonsNecromancy::NecromancyResult legacySkeletonResult = outgoing.necromancy;
	legacySkeletonResult.skeletonCreature = CreatureID::NONE;
	legacySkeletonResult.deathLordCasualties = 0;
	legacySkeletonResult.graveKnowledgeCasualties = 0;
	legacySkeletonResult.deathLordSkeletons = 0;
	legacySkeletonResult.graveKnowledgeSkeletons = 0;
	CMemorySerializer legacySkeletonResultWire;
	legacySkeletonResultWire.oser.version = necromancyWithoutSkeletonFormVersion;
	legacySkeletonResultWire.oser & legacySkeletonResult;
	legacySkeletonResultWire.iser.version = necromancyWithoutSkeletonFormVersion;
	newHorizonsNecromancy::NecromancyResult legacySkeletonResultIncoming;
	legacySkeletonResultIncoming.skeletonCreature = creature("core:skeletonWarrior");
	legacySkeletonResultWire.iser & legacySkeletonResultIncoming;
	EXPECT_EQ(legacySkeletonResultIncoming.wightsRaised, 7);
	EXPECT_EQ(legacySkeletonResultIncoming.skeletonCreature, CreatureID::NONE);

	CMemorySerializer legacySkeletonOuterWire;
	legacySkeletonOuterWire.oser.version = necromancyWithoutSkeletonFormVersion;
	BattleResultsApplied legacySkeletonOuter = outgoing;
	legacySkeletonOuter.necromancy.skeletonCreature = CreatureID::NONE;
	legacySkeletonOuter.necromancy.deathLordCasualties = 0;
	legacySkeletonOuter.necromancy.graveKnowledgeCasualties = 0;
	legacySkeletonOuter.necromancy.deathLordSkeletons = 0;
	legacySkeletonOuter.necromancy.graveKnowledgeSkeletons = 0;
	legacySkeletonOuterWire.oser & legacySkeletonOuter;
	legacySkeletonOuterWire.iser.version = necromancyWithoutSkeletonFormVersion;
	BattleResultsApplied legacySkeletonOuterIncoming;
	legacySkeletonOuterIncoming.necromancy.skeletonCreature = creature("core:skeletonWarrior");
	legacySkeletonOuterWire.iser & legacySkeletonOuterIncoming;
	EXPECT_EQ(legacySkeletonOuterIncoming.necromancy.wightsRaised, 7);
	EXPECT_EQ(legacySkeletonOuterIncoming.necromancy.skeletonCreature, CreatureID::NONE);

	const auto necromancyWithoutSpecialCasualtiesVersion = ESerializationVersion::NEW_HORIZONS_NECROMANCY_SKELETON_FORM;
	newHorizonsNecromancy::NecromancyResult legacySpecialResult = outgoing.necromancy;
	legacySpecialResult.deathLordCasualties = 0;
	legacySpecialResult.graveKnowledgeCasualties = 0;
	legacySpecialResult.deathLordSkeletons = 0;
	legacySpecialResult.graveKnowledgeSkeletons = 0;
	CMemorySerializer legacySpecialResultWire;
	legacySpecialResultWire.oser.version = necromancyWithoutSpecialCasualtiesVersion;
	legacySpecialResultWire.oser & legacySpecialResult;
	legacySpecialResultWire.iser.version = necromancyWithoutSpecialCasualtiesVersion;
	newHorizonsNecromancy::NecromancyResult legacySpecialResultIncoming;
	legacySpecialResultIncoming.deathLordCasualties = 31;
	legacySpecialResultIncoming.graveKnowledgeCasualties = 29;
	legacySpecialResultIncoming.deathLordSkeletons = 8;
	legacySpecialResultIncoming.graveKnowledgeSkeletons = 5;
	legacySpecialResultWire.iser & legacySpecialResultIncoming;
	EXPECT_EQ(legacySpecialResultIncoming.deathLordCasualties, 0);
	EXPECT_EQ(legacySpecialResultIncoming.graveKnowledgeCasualties, 0);
	EXPECT_EQ(legacySpecialResultIncoming.deathLordSkeletons, 0);
	EXPECT_EQ(legacySpecialResultIncoming.graveKnowledgeSkeletons, 0);

	CMemorySerializer legacySpecialOuterWire;
	legacySpecialOuterWire.oser.version = necromancyWithoutSpecialCasualtiesVersion;
	BattleResultsApplied legacySpecialOuter = outgoing;
	legacySpecialOuter.necromancy.deathLordCasualties = 0;
	legacySpecialOuter.necromancy.graveKnowledgeCasualties = 0;
	legacySpecialOuter.necromancy.deathLordSkeletons = 0;
	legacySpecialOuter.necromancy.graveKnowledgeSkeletons = 0;
	legacySpecialOuterWire.oser & legacySpecialOuter;
	legacySpecialOuterWire.iser.version = necromancyWithoutSpecialCasualtiesVersion;
	BattleResultsApplied legacySpecialOuterIncoming;
	legacySpecialOuterIncoming.necromancy.deathLordCasualties = 31;
	legacySpecialOuterIncoming.necromancy.graveKnowledgeCasualties = 29;
	legacySpecialOuterIncoming.necromancy.deathLordSkeletons = 8;
	legacySpecialOuterIncoming.necromancy.graveKnowledgeSkeletons = 5;
	legacySpecialOuterWire.iser & legacySpecialOuterIncoming;
	EXPECT_EQ(legacySpecialOuterIncoming.necromancy.deathLordCasualties, 0);
	EXPECT_EQ(legacySpecialOuterIncoming.necromancy.graveKnowledgeCasualties, 0);
	EXPECT_EQ(legacySpecialOuterIncoming.necromancy.deathLordSkeletons, 0);
	EXPECT_EQ(legacySpecialOuterIncoming.necromancy.graveKnowledgeSkeletons, 0);

	newHorizonsNecromancy::NecromancyResult unsupportedDirect;
	unsupportedDirect.wightsRaised = 1;
	CMemorySerializer unsupportedDirectWire;
	unsupportedDirectWire.oser.version = necromancyWithoutWightsVersion;
	EXPECT_THROW(unsupportedDirectWire.oser & unsupportedDirect, std::runtime_error);
	EXPECT_TRUE(unsupportedDirectWire.extractBuffer().empty())
		<< "An older result writer must reject a nonzero Wight count before writing bytes";

	BattleResultsApplied unsupportedOuter;
	unsupportedOuter.battleID = BattleID(4);
	unsupportedOuter.necromancy.wightsRaised = 1;
	CMemorySerializer unsupportedOuterWire;
	unsupportedOuterWire.oser.version = necromancyWithoutWightsVersion;
	EXPECT_THROW(unsupportedOuterWire.oser & unsupportedOuter, std::runtime_error);
	EXPECT_TRUE(unsupportedOuterWire.extractBuffer().empty())
		<< "An older outer packet writer must reject a nonzero Wight count before writing bytes";

	newHorizonsNecromancy::NecromancyResult unsupportedSkeletonDirect;
	unsupportedSkeletonDirect.skeletonsRaised = 1;
	unsupportedSkeletonDirect.skeletonCreature = creature("core:skeletonWarrior");
	CMemorySerializer unsupportedSkeletonDirectWire;
	unsupportedSkeletonDirectWire.oser.version = necromancyWithoutSkeletonFormVersion;
	EXPECT_THROW(unsupportedSkeletonDirectWire.oser & unsupportedSkeletonDirect, std::runtime_error);
	EXPECT_TRUE(unsupportedSkeletonDirectWire.extractBuffer().empty())
		<< "An older result writer must reject an upgraded Skeleton form before writing bytes";

	BattleResultsApplied unsupportedSkeletonOuter;
	unsupportedSkeletonOuter.battleID = BattleID(6);
	unsupportedSkeletonOuter.necromancy.skeletonsRaised = 1;
	unsupportedSkeletonOuter.necromancy.skeletonCreature = creature("core:skeletonWarrior");
	CMemorySerializer unsupportedSkeletonOuterWire;
	unsupportedSkeletonOuterWire.oser.version = necromancyWithoutSkeletonFormVersion;
	EXPECT_THROW(unsupportedSkeletonOuterWire.oser & unsupportedSkeletonOuter, std::runtime_error);
	EXPECT_TRUE(unsupportedSkeletonOuterWire.extractBuffer().empty())
		<< "An older outer packet writer must reject an upgraded Skeleton form before writing bytes";

	newHorizonsNecromancy::NecromancyResult unsupportedSpecialDirect;
	unsupportedSpecialDirect.deathLordCasualties = 1;
	CMemorySerializer unsupportedSpecialDirectWire;
	unsupportedSpecialDirectWire.oser.version = necromancyWithoutSpecialCasualtiesVersion;
	EXPECT_THROW(unsupportedSpecialDirectWire.oser & unsupportedSpecialDirect, std::runtime_error);
	EXPECT_TRUE(unsupportedSpecialDirectWire.extractBuffer().empty())
		<< "An older result writer must reject special casualty fields before writing bytes";

	BattleResultsApplied unsupportedSpecialOuter;
	unsupportedSpecialOuter.battleID = BattleID(7);
	unsupportedSpecialOuter.necromancy.graveKnowledgeSkeletons = 1;
	CMemorySerializer unsupportedSpecialOuterWire;
	unsupportedSpecialOuterWire.oser.version = necromancyWithoutSpecialCasualtiesVersion;
	EXPECT_THROW(unsupportedSpecialOuterWire.oser & unsupportedSpecialOuter, std::runtime_error);
	EXPECT_TRUE(unsupportedSpecialOuterWire.extractBuffer().empty())
		<< "An older outer packet writer must reject special casualty fields before writing bytes";

	newHorizonsNecromancy::NecromancyResult invalidSkeletonForm;
	invalidSkeletonForm.skeletonCreature = creature("core:skeletonWarrior");
	CMemorySerializer invalidSkeletonFormWire;
	EXPECT_THROW(invalidSkeletonFormWire.oser & invalidSkeletonForm, std::runtime_error);
	EXPECT_TRUE(invalidSkeletonFormWire.extractBuffer().empty());

	newHorizonsNecromancy::NecromancyResult invalidDirect;
	invalidDirect.wightsRaised = -1;
	CMemorySerializer invalidDirectWire;
	EXPECT_THROW(invalidDirectWire.oser & invalidDirect, std::runtime_error);
	EXPECT_TRUE(invalidDirectWire.extractBuffer().empty());

	BattleResultsApplied invalidOuter;
	invalidOuter.battleID = BattleID(5);
	invalidOuter.necromancy.wightsRaised = -1;
	CMemorySerializer invalidOuterWire;
	EXPECT_THROW(invalidOuterWire.oser & invalidOuter, std::runtime_error);
	EXPECT_TRUE(invalidOuterWire.extractBuffer().empty());

	newHorizonsNecromancy::NecromancyResult invalidSpecialDirect;
	invalidSpecialDirect.deathLordSkeletons = -1;
	CMemorySerializer invalidSpecialDirectWire;
	EXPECT_THROW(invalidSpecialDirectWire.oser & invalidSpecialDirect, std::runtime_error);
	EXPECT_TRUE(invalidSpecialDirectWire.extractBuffer().empty());

	BattleResultsApplied invalidSpecialOuter;
	invalidSpecialOuter.battleID = BattleID(8);
	invalidSpecialOuter.necromancy.graveKnowledgeCasualties = -1;
	CMemorySerializer invalidSpecialOuterWire;
	EXPECT_THROW(invalidSpecialOuterWire.oser & invalidSpecialOuter, std::runtime_error);
	EXPECT_TRUE(invalidSpecialOuterWire.extractBuffer().empty());

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CANONICAL_ORDERS;
	BattleResultsApplied legacyPayload = outgoing;
	EXPECT_THROW(old.oser & legacyPayload, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());

	BattleResult result;
	result.battleID = BattleID(2);
	result.necromancyEligibilityCaptured = true;
	result.necromancyEligibleCasualties[BattleSide::DEFENDER][creature("core:pikeman")] = 7;
	result.necromancySpecialEligibilityCaptured = true;
	result.necromancyNonlivingEligibleCasualties[BattleSide::DEFENDER][creature("core:ironGolem")] = 13;
	result.necromancyUndeadEligibleCasualties[BattleSide::DEFENDER][creature("core:vampire")] = 17;
	CMemorySerializer resultWire;
	resultWire.oser & result;
	BattleResult resultDecoded;
	resultWire.iser & resultDecoded;
	EXPECT_TRUE(resultDecoded.necromancyEligibilityCaptured);
	EXPECT_EQ(resultDecoded.necromancyEligibleCasualties[BattleSide::DEFENDER][creature("core:pikeman")], 7);
	EXPECT_TRUE(resultDecoded.necromancySpecialEligibilityCaptured);
	EXPECT_EQ(resultDecoded.necromancyNonlivingEligibleCasualties[BattleSide::DEFENDER]
		[creature("core:ironGolem")], 13);
	EXPECT_EQ(resultDecoded.necromancyUndeadEligibleCasualties[BattleSide::DEFENDER]
		[creature("core:vampire")], 17);

	BattleResult legacySpecialCapture = result;
	legacySpecialCapture.necromancySpecialEligibilityCaptured = false;
	legacySpecialCapture.necromancyNonlivingEligibleCasualties = {};
	legacySpecialCapture.necromancyUndeadEligibleCasualties = {};
	CMemorySerializer legacySpecialCaptureWire;
	legacySpecialCaptureWire.oser.version = necromancyWithoutSpecialCasualtiesVersion;
	legacySpecialCaptureWire.oser & legacySpecialCapture;
	legacySpecialCaptureWire.iser.version = necromancyWithoutSpecialCasualtiesVersion;
	BattleResult legacySpecialCaptureDecoded;
	legacySpecialCaptureDecoded.necromancySpecialEligibilityCaptured = true;
	legacySpecialCaptureDecoded.necromancyNonlivingEligibleCasualties[BattleSide::ATTACKER]
		[creature("core:ironGolem")] = 99;
	legacySpecialCaptureDecoded.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER]
		[creature("core:vampire")] = 99;
	legacySpecialCaptureWire.iser & legacySpecialCaptureDecoded;
	EXPECT_FALSE(legacySpecialCaptureDecoded.necromancySpecialEligibilityCaptured);
	EXPECT_TRUE(legacySpecialCaptureDecoded.necromancyNonlivingEligibleCasualties[BattleSide::ATTACKER].empty());
	EXPECT_TRUE(legacySpecialCaptureDecoded.necromancyUndeadEligibleCasualties[BattleSide::ATTACKER].empty());
	EXPECT_TRUE(legacySpecialCaptureDecoded.necromancyNonlivingEligibleCasualties[BattleSide::DEFENDER].empty());
	EXPECT_TRUE(legacySpecialCaptureDecoded.necromancyUndeadEligibleCasualties[BattleSide::DEFENDER].empty());
	EXPECT_EQ(legacySpecialCaptureDecoded.necromancyEligibleCasualties[BattleSide::DEFENDER]
		[creature("core:pikeman")], 7);

	BattleResult unsupportedSpecialCapture;
	unsupportedSpecialCapture.battleID = BattleID(9);
	unsupportedSpecialCapture.necromancySpecialEligibilityCaptured = true;
	unsupportedSpecialCapture.necromancyUndeadEligibleCasualties[BattleSide::DEFENDER]
		[creature("core:vampire")] = 1;
	CMemorySerializer unsupportedSpecialCaptureWire;
	unsupportedSpecialCaptureWire.oser.version = necromancyWithoutSpecialCasualtiesVersion;
	EXPECT_THROW(unsupportedSpecialCaptureWire.oser & unsupportedSpecialCapture, std::runtime_error);
	EXPECT_TRUE(unsupportedSpecialCaptureWire.extractBuffer().empty())
		<< "An older BattleResult writer must reject special casualty capture before writing bytes";
}

TEST_F(NewHorizonsNecromancyRuntimeTest, LegacyHeroDoesNotEnterNewHorizonsResolver)
{
	ASSERT_NE(attackerSideHero, nullptr);
	EXPECT_FALSE(attackerSideHero->usesNewHorizonsNecromancy());
	BattleResult legacy;
	legacy.battleID = BattleID(3);
	legacy.winner = BattleSide::ATTACKER;
	legacy.casualties[BattleSide::DEFENDER][creature("core:pikeman")] = 10;
	// A legacy hero still exposes the old health-weighted entry point.  This
	// test intentionally leaves the old skill absent: the result is empty, but
	// the New Horizons resolver must not silently replace that path.
	EXPECT_FALSE(attackerSideHero->calculateNecromancy(legacy).getCreature());
}

TEST_F(NewHorizonsNecromancyRuntimeTest, GameStateAppliesHarvestAfterBattleManaClamp)
{
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 5);
	startBattle();
	const auto initialMana = battle()->getSide(BattleSide::ATTACKER).initialMana;
	ASSERT_EQ(initialMana, 5);

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	applied.victor = PlayerColor(0);
	applied.loser = PlayerColor(1);
	applied.necromancy.active = true;
	applied.necromancy.applied = true;
	applied.necromancy.manaRecovered = 3;
	setTestSpellPointTotal(attackerSideHero, 2); // Three mana spent during combat.
	gameState()->apply(applied);

	EXPECT_EQ(attackerSideHero->getManaAvailable(), initialMana);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, PersistentBufferGrantSurvivesAfterTemporaryBufferIsFullySpent)
{
	verifyTemporaryBufferCleanupAfterSpend(10, 0, 3);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, PersistentBufferGrantAndOriginalBufferSurvivePartialTemporarySpend)
{
	verifyTemporaryBufferCleanupAfterSpend(5, 3, 5);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, WraithManaDrainSpendsCombatBufferFirstAndCleanupKeepsPersistentGrant)
{
	ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->initializeSpellPoints(5, 2);
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:wraith"), 1));

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);

	startBattle();
	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSide.initialNormalSpellPoints, 5);
	ASSERT_EQ(attackerSide.initialBufferSpellPoints, 2);
	ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 10);

	// This Buffer is persistent and arrives after the combat-only bonus. Mana
	// Drain must still consume the remaining temporary portion first.
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 3);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
	beginCombat();

	const auto wraithID = creature("core:wraith");
	const auto wraiths = battle()->battleGetStacksIf([&](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER && stack->unitType()->getId() == wraithID;
	});
	ASSERT_EQ(wraiths.size(), 1u);
	const auto * wraith = wraiths.front();
	ASSERT_TRUE(wraith->hasBonusOfType(BonusType::MANA_DRAIN));

	for(int remainingActivations = 0; remainingActivations < 8 && !wraith->drainedMana; ++remainingActivations)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), wraith->unitId())
			<< "Mana Drain is applied before the Wraith's activation is published";
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}

	ASSERT_TRUE(wraith->drainedMana);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 11);
	EXPECT_EQ(attackerSide.temporaryBufferRemaining, 6);

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 5)
		<< "Cleanup removes the six unspent combat-only points while preserving original + new persistent Buffer";
}

TEST_F(NewHorizonsNecromancyRuntimeTest, NegativeCombatManaBonusDrainsBufferBeforeNormalAndCancelRestoresPools)
{
	verifyNegativeCombatManaPenalty(-4, 3, 0);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, MinimumCombatManaBonusCannotOverflowAndCancelRestoresPools)
{
	verifyNegativeCombatManaPenalty(std::numeric_limits<int32_t>::min(), 0, 0);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, NormalRestorationDuringCombatSurvivesSuccessfulResult)
{
	ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->initializeSpellPoints(5, 2);

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);

	startBattle();
	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 10);

	gameHandler->restoreSpellPoints(attackerSideHero->id, 4);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 3);
	ASSERT_EQ(attackerSideHero->getNormalSpellPoints(), 9);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 13);

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 9);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 5);
}

TEST_F(NewHorizonsNecromancyRuntimeTest, CombatManaBufferGrantSaturatesAtInt32Maximum)
{
	ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->initializeSpellPoints(5, std::numeric_limits<int32_t>::max() - 3);

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = std::numeric_limits<int32_t>::max();
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);

	startBattle();
	const auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(attackerSide.additionalMana, std::numeric_limits<int32_t>::max());
	EXPECT_EQ(attackerSide.temporaryBufferRemaining, 3);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), std::numeric_limits<int32_t>::max());

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), std::numeric_limits<int32_t>::max() - 3);
}
