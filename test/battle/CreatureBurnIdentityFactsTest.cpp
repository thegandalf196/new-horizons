/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/GameLibrary.h"
namespace
{
struct WithoutLibrary
{
	GameLibrary * saved = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = saved; }
};
}
TEST(CreatureBurnIdentityFactsTest, RegisteredCapturedPairClassifiesBothHeroesAfterLibraryRetires)
{
	const auto identities = newHorizonsCreatureBurn::captureIdentityFacts();
	ASSERT_TRUE(identities.rashka.hasValue()); ASSERT_TRUE(identities.ignissa.hasValue());
	ASSERT_NE(identities.rashka, identities.ignissa);
	const auto rashka = newHorizonsCreatureBurn::marker(identities.rashka, 1, 5);
	const auto ignissa = newHorizonsCreatureBurn::marker(identities.ignissa, 2, 7);
	ASSERT_TRUE(newHorizonsCreatureBurn::isBurn(&rashka)); ASSERT_TRUE(newHorizonsCreatureBurn::isBurn(&ignissa));
	WithoutLibrary absent;
	EXPECT_TRUE(newHorizonsCreatureBurn::isBurnCaptured(&rashka, identities));
	EXPECT_TRUE(newHorizonsCreatureBurn::isBurnCaptured(&ignissa, identities));
	EXPECT_FALSE(newHorizonsCreatureBurn::isBurnCaptured(nullptr, identities));
}
TEST(CreatureBurnIdentityFactsTest, UnknownFactsAreLazyForUnrelatedRecordsAndNeverAcceptClaimedBurn)
{
	const auto identities = newHorizonsCreatureBurn::captureIdentityFacts();
	auto claimed = newHorizonsCreatureBurn::marker(identities.rashka, 1, 5);
	Bonus unrelated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 1, BonusSourceID());
	WithoutLibrary absent;
	const newHorizonsCreatureBurn::IdentityFacts unknown;
	EXPECT_FALSE(newHorizonsCreatureBurn::isBurnCaptured(&unrelated, unknown));
	EXPECT_THROW(newHorizonsCreatureBurn::isBurnCaptured(&claimed, unknown), std::logic_error);
	auto partial = identities; partial.ignissa = HeroTypeID();
	EXPECT_THROW(newHorizonsCreatureBurn::isBurnCaptured(&claimed, partial), std::logic_error);
	auto equal = identities; equal.ignissa = equal.rashka;
	EXPECT_THROW(newHorizonsCreatureBurn::isBurnCaptured(&claimed, equal), std::logic_error);
	auto malformed = claimed; malformed.val = 0;
	EXPECT_THROW(newHorizonsCreatureBurn::isBurnCaptured(&malformed, identities), std::runtime_error);
	malformed = claimed; malformed.source = BonusSource::OTHER;
	EXPECT_THROW(newHorizonsCreatureBurn::isBurnCaptured(&malformed, identities), std::runtime_error);
}
