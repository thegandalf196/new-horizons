/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/DamageCacheAdmission.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <stdexcept>

namespace
{
// Test-only, query-pure callback: the sole failure is explicit and does not
// mutate gameplay. Production scopes are admitted canonical sources.
class ObservedHeroBattle final : public BattleInfo
{
public:
	const CGHeroInstance * hero = nullptr;
	mutable unsigned queries = 0;
	mutable bool fail = false;
	ObservedHeroBattle() : BattleInfo(nullptr) {}
	const CGHeroInstance * getSideHero(BattleSide side) const override
	{
		++queries;
		if(fail)
		{
			fail = false;
			throw std::runtime_error("visible hero query failure");
		}
		return side == BattleSide::ATTACKER ? hero : nullptr;
	}
};
}

class OrdinaryDamageObservationTest : public ProjectedTargetHitFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		ProjectedTargetHitFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}
};

TEST_F(OrdinaryDamageObservationTest, IndependentActorsShareLazyWholeStateFact)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	const auto first = child->getForUpdate(braced->unitId());
	const auto second = child->getForUpdate(incoming->unitId());
	auto source = std::make_shared<ObservedHeroBattle>();
	source->hero = attackerSideHero;
	{
		OrdinaryDamageObservation observation(source);
		DamageCacheAdmission a(*first, *source, &observation);
		DamageCacheAdmission b(*second, *source, &observation);
		EXPECT_EQ(source->queries, 0u);
		EXPECT_TRUE(a.ordinaryCreatureDamageIsStateful());
		EXPECT_TRUE(b.ordinaryCreatureDamageIsStateful());
		EXPECT_EQ(source->queries, 1u);
	}
	// A new scope after a policy mutation must not retain the prior true value.
	auto captured = attackerSideHero->getPrimaryGrowthRules();
	captured["additionalSpecialties"]["ordinaryCreatureSpecialties"]["ridersExploitDamagePercent"].Integer() = 0;
	attackerSideHero->replaceCapturedPrimaryGrowthRules(captured);
	source->queries = 0;
	OrdinaryDamageObservation next(source);
	DamageCacheAdmission a(*first, *source, &next), b(*second, *source, &next);
	EXPECT_FALSE(a.ordinaryCreatureDamageIsStateful());
	EXPECT_FALSE(b.ordinaryCreatureDamageIsStateful());
	EXPECT_EQ(source->queries, 2u);
}

TEST_F(OrdinaryDamageObservationTest, CallbackMismatchAndFailureRetainExactFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	const auto actor = child->getForUpdate(braced->unitId());
	auto source = std::make_shared<ObservedHeroBattle>();
	source->hero = attackerSideHero;
	source->fail = true;
	OrdinaryDamageObservation observation(source);
	DamageCacheAdmission first(*actor, *source, &observation);
	EXPECT_THROW(first.ordinaryCreatureDamageIsStateful(), std::runtime_error);
	DamageCacheAdmission retry(*actor, *source, &observation);
	EXPECT_TRUE(retry.ordinaryCreatureDamageIsStateful());
	EXPECT_TRUE(first.ordinaryCreatureDamageIsStateful());
	EXPECT_EQ(source->queries, 2u);
	auto sibling = std::make_shared<ObservedHeroBattle>();
	DamageCacheAdmission other(*actor, *sibling, &observation);
	EXPECT_FALSE(other.ordinaryCreatureDamageIsStateful());
	EXPECT_EQ(sibling->queries, 2u);
	EXPECT_EQ(source->queries, 2u);
}

TEST_F(OrdinaryDamageObservationTest, NewScopeObservesProjectedRosterBonusWithoutVersionKey)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
	const auto actor = child->getForUpdate(braced->unitId());
	const auto victim = child->getForUpdate(incoming->unitId());
	{
		OrdinaryDamageObservation observation(child);
		EXPECT_EQ(observation.ordinaryCreatureDamageIsStateful(),
			newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*child));
	}
	victim->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE, BonusType::DYNAMIC_CREATURE_DAMAGE,
		BonusSource::OTHER, 0, BonusSourceID())});
	OrdinaryDamageObservation next(child);
	DamageCacheAdmission admission(*actor, *child, &next);
	EXPECT_TRUE(admission.ordinaryCreatureDamageIsStateful());
	EXPECT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*child));
}

TEST_F(OrdinaryDamageObservationTest, IndependentDamageQueriesAndOriginalFallbackKeepExactValues)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto first = child->getForUpdate(braced->unitId());
	const auto second = child->getForUpdate(incoming->unitId());
	DamageCache plain, observed;
	OrdinaryDamageObservation observation(child);
	for(unsigned repeat = 0; repeat < 2; ++repeat)
	{
		EXPECT_EQ(observed.getDamage(first.get(), second.get(), child, nullptr, nullptr, &observation),
			plain.getDamage(first.get(), second.get(), child));
		EXPECT_EQ(observed.getDamage(second.get(), first.get(), child, nullptr, nullptr, &observation),
			plain.getDamage(second.get(), first.get(), child));
		EXPECT_EQ(observed.getOriginalDamage(first.get(), second.get(), child, nullptr, &observation),
			plain.getOriginalDamage(first.get(), second.get(), child));
		EXPECT_EQ(AttackPossibility::calculateDamageReduce(first.get(), second.get(), 10, observed,
			child, nullptr, &observation), AttackPossibility::calculateDamageReduce(first.get(), second.get(),
			10, plain, child));
	}
}
