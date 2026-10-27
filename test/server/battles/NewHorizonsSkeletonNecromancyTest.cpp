/*
 * NewHorizonsSkeletonNecromancyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/queries/BattleQueries.h"

namespace
{
newHorizonsNecromancy::NecromancyResult output(bool current, bool room = true,
	CreatureID form = CreatureID::NONE)
{
	return newHorizonsNecromancy::resolve(3, 200, 100, true, true, true,
		room, false, 95, 100, 100, true, false, form,
		{80, 40, 40}, {50, 25, 25}, true, true, false, 0, 0, current);
}
}

TEST(NewHorizonsSkeletonNecromancy, NewPerksKeepLivingRaisingAndManaWithoutEnemySpecialPoolsOrConversions)
{
	const auto value = output(true);
	EXPECT_EQ(value.percentage, 35);
	EXPECT_EQ(value.eligibleCasualties, 200);
	EXPECT_EQ(value.deathLordSkeletons, 0);
	EXPECT_EQ(value.graveKnowledgeSkeletons, 0);
	EXPECT_EQ(value.skeletonsOffered, 70);
	EXPECT_EQ(value.skeletonsRaised, 70);
	EXPECT_EQ(value.zombiesRaised, 0);
	EXPECT_EQ(value.wightsRaised, 0);
	EXPECT_EQ(value.boneDragonsRaised, 0);
	EXPECT_EQ(value.lordOfDeadSkeletonsConsumed, 0);
	EXPECT_FALSE(value.corpsePreservation);
	EXPECT_FALSE(value.darkConversionAvailable);
	EXPECT_FALSE(value.darkConversionChosen);
	EXPECT_EQ(value.manaRecovered, 5);
	EXPECT_TRUE(value.applied);
	const auto legacy = output(false);
	EXPECT_TRUE(legacy.blockedByArmyCapacity)
		<< "Historical multi-output perks still require their Zombie/Wight/Dragon slots";
	EXPECT_EQ(legacy.skeletonsOffered, 87);
}

TEST(NewHorizonsSkeletonNecromancy, RankRatesAndHistoricalConversionsKeepTheirSeparatePolicies)
{
	for(const int rank : {1, 2, 3})
	{
		const auto current = newHorizonsNecromancy::resolve(rank, 99, 49, false, true, true,
			true, true, 0, 0, 50, true, true, CreatureID::NONE, {}, {}, true, true, true, 0, 0, true);
		EXPECT_EQ(current.percentage, rank * 10);
		EXPECT_EQ(current.skeletonsRaised, 99 * rank / 10);
		EXPECT_EQ(current.zombiesRaised + current.wightsRaised + current.boneDragonsRaised, 0);
	}
	const auto historical = newHorizonsNecromancy::resolve(3, 200, 100, true, true, true,
		true, true, 0, 0, 100, true, true, CreatureID::NONE, {}, {}, true, true, true);
	EXPECT_GT(historical.zombiesRaised, 0);
	EXPECT_GT(historical.wightsRaised, 0);
	EXPECT_EQ(historical.boneDragonsRaised, 1);
}

TEST(NewHorizonsSkeletonNecromancy, SkeletonCapacityRemainsAtomicAndBoneSteelKeepsSelectedForm)
{
	const auto blocked = output(true, false);
	EXPECT_TRUE(blocked.blockedByArmyCapacity);
	EXPECT_EQ(blocked.skeletonsOffered, 70);
	EXPECT_EQ(blocked.skeletonsRaised, 0);
	EXPECT_EQ(blocked.manaRecovered, 0);
	const CreatureID upgraded(CreatureID::decode("core:skeletonWarrior"));
	const auto raised = output(true, true, upgraded);
	EXPECT_EQ(raised.skeletonCreature, upgraded);
	EXPECT_EQ(raised.raisedCreature, upgraded);
	EXPECT_EQ(raised.skeletonsRaised, 70);
}

TEST(NewHorizonsSkeletonNecromancy, VidominaStillReclaimsResidualSkeletonsIndependentlyOfPerks)
{
	const auto ordinary = output(true);
	const auto specialty = newHorizonsNecromancy::reclaimResidualSkeletons(ordinary, 100, 95, 100);
	EXPECT_EQ(specialty.reclaimerZombiesRaised, 23);
	EXPECT_EQ(specialty.zombiesRaised, 23);
	EXPECT_EQ(specialty.skeletonsRaised, 1);
	EXPECT_EQ(specialty.skeletonsOffered, ordinary.skeletonsOffered);
}

class NewHorizonsSkeletonCorpseTest : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		if(!GetParam())
			perks.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
		perks.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		ASSERT_EQ(newHorizonsNecromancy::policy(attackerSideHero->getPerkState().rules).has_value(), GetParam());
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
		gameHandler->onAdvInterfaceReady(PlayerColor(1));
		attackerSideHero->setHeroType(HeroTypeID(72)); // Existing Septienna fixture identity, Necropolis.
		const auto skill = SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), CreatureID::SKELETON, 1));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0),
			CreatureID(CreatureID::decode("core:pikeman")), 41));
		ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());
		ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
		ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::BONE_COLLECTOR_ID));
	}
};

TEST_P(NewHorizonsSkeletonCorpseTest, OrdinaryMagicalKillsNeedNoPerkButDestroyedRemainsStayExcluded)
{
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	CStack * target = nullptr;
	for(const auto * stack : battle()->battleGetAllStacks())
		if(stack->unitSide() == BattleSide::DEFENDER
			&& stack->creatureId() == CreatureID(CreatureID::decode("core:pikeman")))
			target = const_cast<CStack *>(stack);
	ASSERT_NE(target, nullptr);
	const auto damage = [&](int count, bool destroy)
	{
		BattleStackAttacked attacked;
		attacked.stackAttacked = target->unitId();
		attacked.damageAmount = count * target->getMaxHealth();
		target->prepareAttacked(attacked, gameHandler->getRandomGenerator(), destroy,
			battle::DamageProvenance::SPELL);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(attacked.newState);
		gameHandler->sendAndApply(update);
	};
	damage(30, false);
	damage(10, true);
	ASSERT_EQ(target->getCount(), 1);
	ASSERT_EQ(target->getUnusableRemains(), 10);
	ASSERT_EQ(target->getMagicalCasualties(), 30);
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
	const auto slot = attackerSideHero->getSlotFor(CreatureID::SKELETON);
	ASSERT_TRUE(slot.validSlot());
	EXPECT_EQ(attackerSideHero->getStackCount(slot), GetParam() ? 7 : 1);
}

INSTANTIATE_TEST_SUITE_P(CapturedPolicy, NewHorizonsSkeletonCorpseTest, ::testing::Bool());
