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
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <array>
#include <limits>
#include <memory>
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

/// Same computer-winner full-flow fixture, with a recorder installed after
/// setup so the assertions can inspect authoritative result/error packs.
class NewHorizonsNecromancyAdmissionAITest : public NewHorizonsNecromancyAITest
{
protected:
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
		startWithMap(std::move(builder));

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
		necromancyResult.eligibleCasualties, false, false, false, false, true, true, 0, 0);
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

TEST_F(NewHorizonsNecromancyAdmissionAITest, DarkConversionPreviewRejectsMixedOutputThatNeedsTwoSlots)
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

	const auto gargoyle = creature("core:stoneGargoyle");
	const auto skeleton = creature("core:skeleton");
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), gargoyle, 100));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	// Basic Necromancy offers ten Skeletons: those fit in the single free slot.
	// Converting them needs both one Skeleton (the remainder) and three Zombies,
	// so the preview must not offer the impossible mixed result.
	resolveBattleDialogs();

	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	EXPECT_EQ(necromancyResult.rank, 1);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 10);
	EXPECT_TRUE(necromancyResult.darkConversionAvailable);
	EXPECT_FALSE(necromancyResult.darkConversionChosen);
	EXPECT_TRUE(necromancyResult.applied);
	EXPECT_FALSE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 10);
	EXPECT_EQ(necromancyResult.zombiesRaised, 0);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(6)), 10);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(6)), skeleton.toCreature());
	EXPECT_EQ(recordingServer->systemMessages, 0);
}

TEST_F(NewHorizonsNecromancyAdmissionAITest, DarkConversionRechecksArmyCapacityAfterChoiceQuery)
{
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	prepareNecromancerArmy(false);
	// With two empty slots, both Skeleton-only and mixed Zombie output fit.
	fillFillerSlots(1, GameConstants::ARMY_SIZE - 3);
	ASSERT_EQ(attackerSideHero->getFreeSlots().size(), 2u);
	attackerSideHero->applyPerkSelection({
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"));

	const auto gargoyle = creature("core:stoneGargoyle");
	const auto zombie = creature("core:zombie");
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), gargoyle, 100));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogsOnly();

	auto necromancyQuery = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(necromancyQuery, nullptr);
	ASSERT_EQ(necromancyQuery->getType(), QueryType::NecromancyChoice);
	ASSERT_TRUE(necromancyQuery->isValidReply(2)); // Zombie was legal when offered.

	// Model an authoritative army-capacity change while the choice is pending.
	// The remaining empty slot can hold the Skeleton remainder or the Zombies,
	// but not both; the selected mixed result must now fail atomically.
	const auto monk = creature("core:monk");
	const auto monkCapacity = attackerSideHero->getLeadershipSlotCapacity(monk);
	ASSERT_TRUE(monkCapacity);
	ASSERT_GE(monkCapacity->maximum, 1);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(5), monk, 1));
	ASSERT_EQ(attackerSideHero->getFreeSlots().size(), 1u);

	ASSERT_TRUE(gameHandler->queryReply(necromancyQuery->queryID, 2, PlayerColor(0)));
	resolveLevelUpDialogs();
	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	ASSERT_EQ(recordingServer->battleResults.size(), 1u);
	const auto & necromancyResult = recordingServer->battleResults.back().necromancy;
	ASSERT_TRUE(necromancyResult.active);
	EXPECT_EQ(necromancyResult.rank, 1);
	EXPECT_TRUE(necromancyResult.darkConversionAvailable);
	EXPECT_TRUE(necromancyResult.darkConversionChosen);
	EXPECT_EQ(necromancyResult.skeletonsOffered, 10);
	EXPECT_FALSE(necromancyResult.applied);
	EXPECT_TRUE(necromancyResult.blockedByArmyCapacity);
	EXPECT_EQ(necromancyResult.skeletonsRaised, 0);
	EXPECT_EQ(necromancyResult.zombiesRaised, 0);
	EXPECT_EQ(necromancyResult.manaRecovered, 0);
	EXPECT_EQ(recordingServer->battleResults.back().raisedStack.getCreature(), nullptr);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(5)), monk.toCreature());
	const auto zombieSlot = attackerSideHero->getSlotFor(zombie);
	ASSERT_TRUE(zombieSlot.validSlot());
	EXPECT_FALSE(attackerSideHero->hasStackAtSlot(zombieSlot));
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
