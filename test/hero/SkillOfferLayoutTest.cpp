/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/callback/SkillOfferLayout.h"

namespace
{
const SecondarySkill A(0), B(1), C(2), D(3);
auto first = [](const std::set<SecondarySkill> & pool) { return *pool.begin(); };
}

TEST(SkillOfferLayout, LeftUpgradeAndRightNewUseSeparateChooserPools)
{
	std::vector<std::set<SecondarySkill>> seen;
	const auto choices = newHorizonsHeroes::splitSkillOffers({C, D}, {A, B}, 2,
		[&](const auto & pool) { seen.push_back(pool); return *pool.begin(); });
	EXPECT_EQ(choices, (std::vector<SecondarySkill>{C, A}));
	EXPECT_EQ(seen, (std::vector<std::set<SecondarySkill>>{{C, D}, {A, B}}));
}

TEST(SkillOfferLayout, MissingCategoryFallsBackWithoutDuplicates)
{
	EXPECT_EQ(newHorizonsHeroes::splitSkillOffers({A, B}, {}, 2, first),
		(std::vector<SecondarySkill>{A, B}));
	EXPECT_EQ(newHorizonsHeroes::splitSkillOffers({}, {A, B}, 2, first),
		(std::vector<SecondarySkill>{A, B}));
	EXPECT_EQ(newHorizonsHeroes::splitSkillOffers({A}, {A}, 2, first),
		(std::vector<SecondarySkill>{A}));
}

TEST(SkillOfferLayout, ExhaustedPoolAndZeroLimitNeverCallChooser)
{
	const auto forbidden = [](const auto &) -> SecondarySkill
		{ ADD_FAILURE() << "Unexpected chooser call"; return A; };
	EXPECT_TRUE(newHorizonsHeroes::splitSkillOffers({}, {}, 2, forbidden).empty());
	EXPECT_TRUE(newHorizonsHeroes::splitSkillOffers({A}, {B}, 0, forbidden).empty());
	EXPECT_EQ(newHorizonsHeroes::splitSkillOffers({}, {B}, 2, first),
		(std::vector<SecondarySkill>{B}));
}

TEST(SkillOfferLayout, RejectsForgedChooserResults)
{
	EXPECT_THROW(newHorizonsHeroes::splitSkillOffers({A}, {B}, 2,
		[](const auto &) { return D; }), std::runtime_error);
}
