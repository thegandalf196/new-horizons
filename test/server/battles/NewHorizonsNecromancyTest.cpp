/*
 * NewHorizonsNecromancyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../SpellPointTestUtils.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <limits>

namespace
{
using newHorizonsNecromancy::resolve;

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}
}

TEST(NewHorizonsNecromancy, RankFormulaUsesExactLivingCountsAndBoneCollectorPoints)
{
	const auto basic = resolve(1, 99, false, false, false, false, true, true, 0, 0);
	EXPECT_EQ(basic.percentage, 10);
	EXPECT_EQ(basic.skeletonsOffered, 9);
	EXPECT_EQ(basic.skeletonsRaised, 9);

	const auto advanced = resolve(2, 99, false, false, false, false, true, true, 0, 0);
	EXPECT_EQ(advanced.skeletonsOffered, 19);

	const auto expert = resolve(3, 99, false, false, false, false, true, true, 0, 0);
	EXPECT_EQ(expert.skeletonsOffered, 29);

	const auto collector = resolve(1, 99, true, false, false, false, true, true, 0, 0);
	EXPECT_EQ(collector.percentage, 15);
	EXPECT_EQ(collector.skeletonsOffered, 14);
	EXPECT_TRUE(collector.applied);
}

TEST(NewHorizonsNecromancy, DarkConversionKeepsRemainderAndRequiresAllDestinations)
{
	const auto converted = resolve(1, 100, false, false, true, true, true, true, 0, 0);
	EXPECT_EQ(converted.skeletonsOffered, 10);
	EXPECT_EQ(converted.zombiesRaised, 3);
	EXPECT_EQ(converted.skeletonsRaised, 1);
	EXPECT_TRUE(converted.darkConversionChosen);

	const auto blocked = resolve(1, 100, false, false, true, true, true, false, 0, 0);
	EXPECT_TRUE(blocked.blockedByArmyCapacity);
	EXPECT_FALSE(blocked.applied);
	EXPECT_EQ(blocked.skeletonsRaised, 0);
	EXPECT_EQ(blocked.zombiesRaised, 0);

	const auto skeletonOnly = resolve(1, 100, false, false, true, false, true, false, 0, 0);
	EXPECT_TRUE(skeletonOnly.applied);
	EXPECT_EQ(skeletonOnly.skeletonsRaised, 10);
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
	const auto harvested = resolve(3, 1000, false, false, false, false, true, true, 0, 100);
	EXPECT_EQ(harvested.skeletonsRaised, 300);
	EXPECT_EQ(harvested.manaRecovered, 10);

	const auto nearlyFull = resolve(3, 1000, false, false, false, false, true, true, 95, 100);
	EXPECT_EQ(nearlyFull.manaRecovered, 5);

	// Passing current mana as the limit is the server's no-Black-Harvest gate.
	const auto noHarvest = resolve(3, 1000, false, false, false, false, true, true, 0, 0);
	EXPECT_EQ(noHarvest.manaRecovered, 0);
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

TEST_F(NewHorizonsNecromancyAITest, ComputerWinnerReceivesAndResumesAuthoritativeDarkConversionQuery)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);

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

	// Make the casualties part of the original defending army so the battle
	// result records all 101 deaths (units injected after battle start have no
	// original-stack baseline). Basic Necromancy offers ten Skeletons, so the
	// Zombie choice needs two distinct destinations: 3 Zombies + 1 Skeleton.
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:peasant"), 101));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));

	// Resolve whichever ordinary battle-result dialogs the compact fixture
	// created. In a real game each controller answers its own dialog; this test
	// then inspects the server-owned choice exposed for the computer winner.
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto dialog = gameHandler->queries->topQuery(player);
		if(dialog && dialog->getType() == QueryType::BattleDialog)
		{
			ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
		}
	}

	auto necromancyQuery = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(necromancyQuery, nullptr);
	ASSERT_EQ(necromancyQuery->getType(), QueryType::NecromancyChoice);
	EXPECT_FALSE(gameHandler->queryReply(necromancyQuery->queryID, 3, PlayerColor(0)));
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), necromancyQuery);
	ASSERT_TRUE(gameHandler->queryReply(necromancyQuery->queryID, 2, PlayerColor(0)));
	for(int remainingLevelUps = 10; remainingLevelUps > 0; --remainingLevelUps)
	{
		auto followup = gameHandler->queries->topQuery(PlayerColor(0));
		if(!followup)
			break;
		// The large casualty fixture may grant a normal post-battle level-up.
		// Resolve it so cleanup can complete; it is independent of conversion.
		ASSERT_EQ(followup->getType(), QueryType::HeroLevelUpDialog);
		ASSERT_TRUE(gameHandler->queryReply(followup->queryID, 0, PlayerColor(0)));
	}
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);

	const auto zombie = creature("core:zombie");
	const auto skeleton = creature("core:skeleton");
	const auto zombieSlot = attackerSideHero->getSlotFor(zombie);
	const auto skeletonSlot = attackerSideHero->getSlotFor(skeleton);
	ASSERT_TRUE(zombieSlot.validSlot());
	ASSERT_TRUE(skeletonSlot.validSlot());
	ASSERT_TRUE(attackerSideHero->hasStackAtSlot(zombieSlot));
	ASSERT_TRUE(attackerSideHero->hasStackAtSlot(skeletonSlot));
	EXPECT_NE(zombieSlot, skeletonSlot);
	EXPECT_EQ(attackerSideHero->getStackCount(zombieSlot), 3);
	EXPECT_EQ(attackerSideHero->getStackCount(skeletonSlot), 1);
	EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
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
	outgoing.necromancy.manaRecovered = 10;
	outgoing.necromancy.raisedCreature = creature("core:zombie");

	CMemorySerializer wire;
	wire.oser & outgoing;
	BattleResultsApplied incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.necromancy.rank, 3);
	EXPECT_EQ(incoming.necromancy.skeletonsRaised, 2);
	EXPECT_EQ(incoming.necromancy.zombiesRaised, 11);
	EXPECT_EQ(incoming.necromancy.manaRecovered, 10);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CANONICAL_ORDERS;
	BattleResultsApplied legacyPayload = outgoing;
	EXPECT_THROW(old.oser & legacyPayload, std::runtime_error);

	BattleResult result;
	result.battleID = BattleID(2);
	result.necromancyEligibilityCaptured = true;
	result.necromancyEligibleCasualties[BattleSide::DEFENDER][creature("core:pikeman")] = 7;
	CMemorySerializer resultWire;
	resultWire.oser & result;
	BattleResult resultDecoded;
	resultWire.iser & resultDecoded;
	EXPECT_TRUE(resultDecoded.necromancyEligibilityCaptured);
	EXPECT_EQ(resultDecoded.necromancyEligibleCasualties[BattleSide::DEFENDER][creature("core:pikeman")], 7);
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
