/*
 * SpellPointCapacityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/BattleStartSnapshotFixture.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
using KnowledgeArtifact = std::pair<ArtifactID, ArtifactPosition>;

const std::array<KnowledgeArtifact, 4> & knowledgeArtifacts()
{
	// Identifier decoding requires the initialized game library, not static
	// initialization before the test environment has loaded its content.
	static const std::array<KnowledgeArtifact, 4> artifacts = {{
		{ArtifactID(ArtifactID::decode("core:thunderHelmet")), ArtifactPosition::HEAD},
		{ArtifactID(ArtifactID::decode("core:swordOfJudgement")), ArtifactPosition::RIGHT_HAND},
		{ArtifactID(ArtifactID::decode("core:necklaceOfDragonteeth")), ArtifactPosition::NECK},
		{ArtifactID(ArtifactID::decode("core:dragonWingTabard")), ArtifactPosition::SHOULDERS}
	}};
	return artifacts;
}

class SpellPointCapacityTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module for its saved spell-point rules";

		startGame();
		ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);

		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void setKnowledge(CGHeroInstance * hero, int32_t value)
	{
		SetPrimarySkill change;
		change.id = hero->id;
		change.which = PrimarySkill::KNOWLEDGE;
		change.val = value;
		change.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(change);
	}

	void setNormal(CGHeroInstance * hero, int32_t value)
	{
		SetMana change(hero->id, SetMana::Operation::SET_NORMAL, value);
		gameState()->apply(change);
	}

	void grantBuffer(CGHeroInstance * hero, int32_t value)
	{
		SetMana change(hero->id, SetMana::Operation::GRANT_BUFFER, value);
		gameState()->apply(change);
	}

	void equipKnowledgeSet(CGHeroInstance * hero)
	{
		for(const auto & [artifact, slot] : knowledgeArtifacts())
		{
			ASSERT_NE(artifact, ArtifactID::NONE);
			ASSERT_EQ(hero->getArt(slot), nullptr);
			giveArtifact(hero, artifact, slot);
		}
	}

	void eraseKnowledgeSet(CGHeroInstance * hero)
	{
		BulkEraseArtifacts erase;
		erase.artHolder = hero->id;
		for(const auto & [artifact, slot] : knowledgeArtifacts())
		{
			(void)artifact;
			erase.posPack.push_back(slot);
		}
		gameState()->apply(erase);
	}

	void expectPools(const CGHeroInstance * hero, int32_t normal, int32_t buffer, int32_t maximum)
	{
		EXPECT_EQ(hero->getNormalSpellPoints(), normal);
		EXPECT_EQ(hero->getBufferSpellPoints(), buffer);
		EXPECT_EQ(hero->manaLimit(), maximum);
		EXPECT_EQ(hero->getManaAvailable(), static_cast<int64_t>(normal) + buffer);
	}
};
}

TEST_F(SpellPointCapacityTest, KnowledgeEquipmentRaisesCapacityWithoutRefillAndRemovalClampsOnlyNormal)
{
	setKnowledge(attackerSideHero, 80);
	setNormal(attackerSideHero, 80);
	grantBuffer(attackerSideHero, 17);
	expectPools(attackerSideHero, 80, 17, 80);

	// These four real artifacts contribute +20 effective Knowledge in total.
	equipKnowledgeSet(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 100);
	expectPools(attackerSideHero, 80, 17, 100);

	setNormal(attackerSideHero, 100);
	expectPools(attackerSideHero, 100, 17, 100);
	eraseKnowledgeSet(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 80);
	expectPools(attackerSideHero, 80, 17, 80);

	// Restoring the same capacity later cannot recover the points lost on removal.
	equipKnowledgeSet(attackerSideHero);
	expectPools(attackerSideHero, 80, 17, 100);
}

TEST_F(SpellPointCapacityTest, EqualCapacityBulkArtifactSwapDoesNotClampEitherHeroMidTransaction)
{
	setKnowledge(attackerSideHero, 80);
	setKnowledge(defenderSideHero, 80);
	setNormal(attackerSideHero, 80);
	setNormal(defenderSideHero, 80);
	grantBuffer(attackerSideHero, 17);
	grantBuffer(defenderSideHero, 29);
	equipKnowledgeSet(attackerSideHero);
	equipKnowledgeSet(defenderSideHero);
	setNormal(attackerSideHero, 100);
	setNormal(defenderSideHero, 100);
	expectPools(attackerSideHero, 100, 17, 100);
	expectPools(defenderSideHero, 100, 29, 100);

	BulkMoveArtifacts swap(PlayerColor(0), attackerSideHero->id, defenderSideHero->id, true);
	for(const auto & [artifact, slot] : knowledgeArtifacts())
	{
		(void)artifact;
		swap.artsPack0.emplace_back(slot, slot);
		swap.artsPack1.emplace_back(slot, slot);
	}
	gameState()->apply(swap);

	// The visitor removes both sets before putting either back. Only the complete
	// top-level pack has the unchanged capacities, so neither hero may lose Normal.
	expectPools(attackerSideHero, 100, 17, 100);
	expectPools(defenderSideHero, 100, 29, 100);
}

TEST_F(SpellPointCapacityTest, IntelligenceRaisesKnowledgeCapacityByThirtyPercentWithoutFillingIt)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 100);
	grantBuffer(attackerSideHero, 37);
	expectPools(attackerSideHero, 100, 37, 100);

	const int wisdomId = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(wisdomId, 0);
	SetSecSkill rank;
	rank.id = attackerSideHero->id;
	rank.which = SecondarySkill(wisdomId);
	rank.val = 1;
	rank.mode = ChangeValueMode::ABSOLUTE;
	gameState()->apply(rank);

	HeroPerkChosen intelligence;
	intelligence.hero = attackerSideHero->id;
	intelligence.selection = {"new-horizons:wisdom", "new-horizons:wisdom.intelligence"};
	gameState()->apply(intelligence);

	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.intelligence"));
	expectPools(attackerSideHero, 100, 37, 130);

	equipKnowledgeSet(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 120);
	expectPools(attackerSideHero, 100, 37, 156);

	setNormal(attackerSideHero, 156);
	eraseKnowledgeSet(attackerSideHero);
	expectPools(attackerSideHero, 130, 37, 130);
	equipKnowledgeSet(attackerSideHero);
	expectPools(attackerSideHero, 130, 37, 156);
}

TEST_F(SpellPointCapacityTest, InvalidManaMutationPayloadsAreRejectedWithoutChangingEitherPool)
{
	setKnowledge(attackerSideHero, 80);
	setNormal(attackerSideHero, 40);
	grantBuffer(attackerSideHero, 30);
	expectPools(attackerSideHero, 40, 30, 80);

	SetMana legacyWithTypedAmount;
	legacyWithTypedAmount.hid = attackerSideHero->id;
	legacyWithTypedAmount.amount = 1;
	EXPECT_THROW(gameState()->apply(legacyWithTypedAmount), std::runtime_error);
	expectPools(attackerSideHero, 40, 30, 80);

	SetMana invalidMode;
	invalidMode.hid = attackerSideHero->id;
	invalidMode.mode = static_cast<ChangeValueMode>(99);
	EXPECT_THROW(gameState()->apply(invalidMode), std::runtime_error);
	expectPools(attackerSideHero, 40, 30, 80);
}

TEST_F(SpellPointCapacityTest, SnapshotRestoreDuringBattleIsRejectedAtomically)
{
	setKnowledge(attackerSideHero, 10);
	attackerSideHero->initializeSpellPoints(5, 2);

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);
	startBattle();

	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
	expectPools(attackerSideHero, 5, 10, 10);

	SetMana snapshot(attackerSideHero->id, SetMana::Operation::RESTORE_SNAPSHOT, 2, 7);
	EXPECT_THROW(gameState()->apply(snapshot), std::runtime_error);

	expectPools(attackerSideHero, 5, 10, 10);
	EXPECT_EQ(attackerSide.temporaryBufferRemaining, 8);
}

TEST_F(SpellPointCapacityTest, RejectedManaDrainLeavesPoolsTemporaryProvenanceAndTriggerFlagUnchanged)
{
	setKnowledge(attackerSideHero, 10);
	setNormal(attackerSideHero, 5);
	grantBuffer(attackerSideHero, 2);

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);
	startBattle();

	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 10);
	grantBuffer(attackerSideHero, 3);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
	beginCombat();

	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	const auto * triggerStack = battle()->battleGetStackByID(battle()->battleActiveUnit()->unitId());
	ASSERT_NE(triggerStack, nullptr);
	ASSERT_FALSE(triggerStack->drainedMana);
	for(const int amount : {-1, 19}) // negative and greater than all available Normal + Buffer
	{
		BattleTriggerEffect drain;
		drain.battleID = BattleID(0);
		drain.stackID = triggerStack->unitId();
		drain.effect = BonusType::MANA_DRAIN;
		drain.val = amount;
		drain.additionalInfo = static_cast<int>(attackerSideHero->id.getNum());
		EXPECT_THROW(gameState()->apply(drain), std::runtime_error) << amount;

		expectPools(attackerSideHero, 5, 13, 10);
		EXPECT_EQ(attackerSide.temporaryBufferRemaining, 8);
		EXPECT_FALSE(triggerStack->drainedMana);
	}
}

TEST_F(SpellPointCapacityTest, BattleStartRejectsInvalidSecondSideSnapshotBeforeMutationOrAttachment)
{
	setKnowledge(attackerSideHero, 80);
	setKnowledge(defenderSideHero, 80);
	setNormal(attackerSideHero, 30);
	setNormal(defenderSideHero, 25);
	grantBuffer(attackerSideHero, 40);
	grantBuffer(defenderSideHero, 50);
	const auto beforeBattle = gameState()->saveToMemory();

	startBattle();
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);
	auto * replicaAttacker = replica->getHero(attackerSideHero->id);
	auto * replicaDefender = replica->getHero(defenderSideHero->id);
	ASSERT_NE(replicaAttacker, nullptr);
	ASSERT_NE(replicaDefender, nullptr);
	ASSERT_EQ(replicaAttacker->battle, nullptr);
	ASSERT_EQ(replicaDefender->battle, nullptr);
	expectPools(replicaAttacker, 30, 40, 80);
	expectPools(replicaDefender, 25, 50, 80);

	BattleStart malformed;
	malformed.battleID = BattleID(0);
	malformed.info = battleStartFixture::snapshot(*battle(), replica.get());
	auto & attackerSnapshot = malformed.info->getSide(BattleSide::ATTACKER);
	attackerSnapshot.initialNormalSpellPoints = 11;
	attackerSnapshot.initialBufferSpellPoints = 7;
	auto & defenderSnapshot = malformed.info->getSide(BattleSide::DEFENDER);
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto handler = std::make_shared<CGameHandler>(restoredServer, replica);
	const std::array<std::array<int32_t, 3>, 4> invalidPools = {{
		{{-1, 50, 0}}, {{25, -1, 0}}, {{25, 50, -1}},
		{{25, 50, std::numeric_limits<int32_t>::max() - 49}}
	}};
	for(const auto & pools : invalidPools)
	{
		defenderSnapshot.initialNormalSpellPoints = pools[0];
		defenderSnapshot.initialBufferSpellPoints = pools[1];
		defenderSnapshot.temporaryBufferRemaining = pools[2];
		EXPECT_THROW(handler->sendAndApply(malformed), std::runtime_error);
		EXPECT_TRUE(replica->currentBattles.empty());
		EXPECT_EQ(replicaAttacker->battle, nullptr);
		EXPECT_EQ(replicaDefender->battle, nullptr);
		expectPools(replicaAttacker, 30, 40, 80);
		expectPools(replicaDefender, 25, 50, 80);
	}
}

TEST_F(SpellPointCapacityTest, HeroSerializationPreservesDistinctNormalAndBufferPools)
{
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(30, 50);
	const auto restored = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->areSpellPointsInitialized());
	EXPECT_EQ(restored->getNormalSpellPoints(), 30);
	EXPECT_EQ(restored->getBufferSpellPoints(), 50);
	EXPECT_EQ(restored->getManaAvailable(), 80);
}

TEST_F(SpellPointCapacityTest, LegacyHeroSerializationDoesNotManufactureBuffer)
{
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(80, 0);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	memory.iser.cb = gameState().get();
	memory.oser & attackerSideHero;
	std::unique_ptr<CGHeroInstance> restored;
	memory.iser & restored;
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->areSpellPointsInitialized());
	EXPECT_EQ(restored->getNormalSpellPoints(), 80);
	EXPECT_EQ(restored->getBufferSpellPoints(), 0);
}

TEST_F(SpellPointCapacityTest, LegacyHeroSerializationRejectsLossOfBuffer)
{
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(30, 50);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	EXPECT_THROW(memory.oser & attackerSideHero, std::runtime_error);
}

TEST_F(SpellPointCapacityTest, BattleSerializationPreservesInitialPoolsAndTemporaryBuffer)
{
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(30, 50);
	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grant(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grant);
	startBattle();
	SetMana spend(attackerSideHero->id, SetMana::Operation::SPEND, 3);
	gameState()->apply(spend);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto & side = restored->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.initialNormalSpellPoints, 30);
	EXPECT_EQ(side.initialBufferSpellPoints, 50);
	EXPECT_EQ(side.temporaryBufferRemaining, 5);
}
