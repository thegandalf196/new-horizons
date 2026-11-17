/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/ScopedStackEffectQueries.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"

class ScopedStackEffectQueriesTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	void prepare()
	{
		useRevisedOrders = true; // Use the current finite Command profile for this query fixture.
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(93), 3);
		ASSERT_NE(target, nullptr);
	}
	std::shared_ptr<Bonus> ordinaryEffect()
	{
		return std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK,
			BonusSource::OTHER, 1, BonusSourceID());
	}
};

TEST_F(ScopedStackEffectQueriesTest, RawExportRemovalPreservesUpdaterAndNativeGenerationCache)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto raw = ordinaryEffect(); raw->updater = std::make_shared<TimesStackSizeUpdater>(2, 2, 1, 1);
	target->addNewBonus(raw);
	const CSelector select = Selector::type()(BonusType::LUCK);
	const auto nativeCached = target->getAllBonuses(select, "scoped-luck-sentinel");
	ASSERT_EQ(nativeCached->totalValue(), 2);
	const auto generation = PreparedBonusGraphStorage::currentGeneration(), version = target->getTreeVersion();
	{
		battle::ScopedStackEffectQueries scope(*battle(), {target});
		EXPECT_EQ(scope.current(target->unitId()).valOfBonuses(BonusType::LUCK), 2);
		scope.removeMatchingExports(target->unitId(), CSelector([](const Bonus * value)
		{
			return value->type == BonusType::LUCK && value->val == 1;
		}));
		EXPECT_FALSE(scope.current(target->unitId()).hasBonusOfType(BonusType::LUCK));
		EXPECT_EQ(raw->val, 1);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(target->getTreeVersion(), version);
	EXPECT_EQ(target->getAllBonuses(select, "scoped-luck-sentinel"), nativeCached);
	EXPECT_TRUE(target->hasBonusOfType(BonusType::LUCK));
}

TEST_F(ScopedStackEffectQueriesTest, InheritedSamePointerBonusSurvivesLocalAndRecursiveRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto shared = ordinaryEffect(); attackerSideHero->addNewBonus(shared); target->addNewBonus(shared);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	{
		battle::ScopedStackEffectQueries scope(*battle(), {target});
		std::shared_ptr<Bonus> exact;
		for(const auto & value : scope.exportedRecords(target->unitId()))
			if(value->type == BonusType::LUCK) exact = value;
		ASSERT_NE(exact, nullptr);
		scope.removeExported(target->unitId(), exact);
		EXPECT_TRUE(scope.current(target->unitId()).hasBonusOfType(BonusType::LUCK));
		scope.removeMatchingExports(target->unitId(), Selector::type()(BonusType::LUCK));
		EXPECT_TRUE(scope.current(target->unitId()).hasBonusOfType(BonusType::LUCK));
		EXPECT_EQ(shared->val, 1);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_TRUE(target->hasBonusOfType(BonusType::LUCK));
}

TEST_F(ScopedStackEffectQueriesTest, CanonicalPendingLimiterSeesPrivateGuardianAppendAndRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto conditional = ordinaryEffect(); conditional->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::GUARDIAN_SPIRIT);
	target->addNewBonus(conditional);
	ASSERT_FALSE(target->hasBonusOfType(BonusType::LUCK));
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	{
		battle::ScopedStackEffectQueries scope(*battle(), {target});
		EXPECT_FALSE(scope.current(target->unitId()).hasBonusOfType(BonusType::LUCK));
		auto guardian = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
			BonusSource::OTHER, 10, BonusSourceID()); guardian->turnsRemain = 2;
		scope.appendExported(target->unitId(), guardian);
		EXPECT_TRUE(scope.current(target->unitId()).hasBonusOfType(BonusType::LUCK));
		scope.removeMatchingExports(target->unitId(), Selector::type()(BonusType::GUARDIAN_SPIRIT));
		EXPECT_FALSE(scope.current(target->unitId()).hasBonusOfType(BonusType::LUCK));
		EXPECT_FALSE(target->hasBonusOfType(BonusType::LUCK));
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ScopedStackEffectQueriesTest, FormSourceAndAcquireStateEnvironmentStayPrivateWithoutNativeAttachments)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto alternate = creatureByName("core:peasant");
	target->beginBattleForm(alternate, 2);
	const auto formedMaximum = target->getMaxHealth();
	target->endBattleForm();
	const auto before = target->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration(), version = target->getTreeVersion();
	{
		battle::ScopedStackEffectQueries scope(*battle(), {target});
		auto & state = scope.state(target->unitId());
		state.beginBattleForm(alternate, 2);
		EXPECT_EQ(state.unitType()->getId(), alternate);
		EXPECT_EQ(state.getMaxHealth(), formedMaximum);
		auto prepared = state.acquireState(); // Frozen vector preparation uses this exact retained ancestry.
		EXPECT_NO_THROW(prepared->getAvailableHealth());
		EXPECT_NO_THROW(prepared->magicResistance());
		state.endBattleForm();
		EXPECT_EQ(state.unitType()->getId(), target->unitType()->getId());
		EXPECT_EQ(state.getMaxHealth(), target->getMaxHealth());
		EXPECT_NO_THROW(scope.accessibility(target->unitId()));
	}
	EXPECT_EQ(target->save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(target->getTreeVersion(), version);
}

TEST_F(ScopedStackEffectQueriesTest, ReadonlySourceLookupIncludesNontargetOccurrencesWithoutMutationEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * source = addStack(BattleSide::DEFENDER, creatureByName("core:beholder"), BattleHex(95), 2);
	ASSERT_NE(source, nullptr);
	const auto native = source->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	{
		battle::ScopedStackEffectQueries scope(*battle(), {target});
		const auto * current = scope.find(source->unitId());
		ASSERT_NE(current, nullptr); EXPECT_NE(current, source);
		EXPECT_EQ(current->unitId(), source->unitId());
		EXPECT_EQ(current->getCount(), source->getCount());
		EXPECT_EQ(current->unitSide(), source->unitSide());
		EXPECT_EQ(scope.find(target->unitId()), &scope.current(target->unitId()));
		EXPECT_EQ(scope.find(std::numeric_limits<uint32_t>::max()), nullptr);
		EXPECT_THROW(scope.state(source->unitId()), std::invalid_argument);
	}
	EXPECT_EQ(source->save(), native);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
