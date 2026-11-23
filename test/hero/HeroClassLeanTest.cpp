/*
 * HeroClassLeanTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 * Hero-window Might/Magic class lean: rounding and class-growth source.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"

#include "../../client/windows/HeroClassLean.h"
#include "../../lib/GameConstants.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"

using heroClassLean::Lean;
using heroClassLean::fromGrowth;

TEST(HeroClassLeanTest, RoundedPartsAlwaysSumToOneHundred)
{
	for(int attack = 0; attack <= 12; ++attack)
		for(int defense = 0; defense <= 12; ++defense)
			for(int power = 0; power <= 12; ++power)
				for(int knowledge = 0; knowledge <= 12; ++knowledge)
				{
					const auto lean = fromGrowth({attack, defense, power, knowledge});
					ASSERT_EQ(lean.mightPercent + lean.magicPercent, 100);
					ASSERT_GE(lean.mightPercent, 0);
					ASSERT_LE(lean.mightPercent, 100);
					const int total = attack + defense + power + knowledge;
					if(total == 0)
						continue;
					// Never more than half a percent away from the exact share.
					const int exactTimesTwoHundred = 200 * (attack + defense);
					ASSERT_LE(std::abs(lean.mightPercent * 2 * total - exactTimesTwoHundred), total);
				}
}

TEST(HeroClassLeanTest, ZeroOrInvalidGrowthShowsAnEvenSplit)
{
	EXPECT_EQ(fromGrowth({0, 0, 0, 0}), (Lean{50, 50}));
	EXPECT_EQ(fromGrowth({-1, 0, 0, 1}), (Lean{50, 50}));
	EXPECT_EQ(fromGrowth({1, 1, -3, 1}), (Lean{50, 50}));
}

TEST(HeroClassLeanTest, RoundsHalfUpAndKeepsMagicAsTheRemainder)
{
	EXPECT_EQ(fromGrowth({1, 0, 7, 0}), (Lean{13, 87})); // 12.5%
	EXPECT_EQ(fromGrowth({1, 0, 0, 2}), (Lean{33, 67})); // 33.3%
	EXPECT_EQ(fromGrowth({1, 1, 0, 1}), (Lean{67, 33})); // 66.7%
	EXPECT_EQ(fromGrowth({0, 0, 3, 4}), (Lean{0, 100}));
	EXPECT_EQ(fromGrowth({5, 0, 0, 0}), (Lean{100, 0}));
}

TEST(HeroClassLeanTest, OwnerExampleNecromancerGrowth)
{
	// Necromancer growth Attack 1, Defense 4, Spell Power 10, Knowledge 5.
	EXPECT_EQ(fromGrowth({1, 4, 10, 5}), (Lean{25, 75}));
}

class HeroClassLeanGame : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	/// One player-owned hero per identifier, on a diagonal so none overlap.
	std::vector<CGHeroInstance *> startWithHeroes(const std::vector<const char *> & identifiers)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0));
		for(size_t index = 0; index < identifiers.size(); ++index)
		{
			const int coordinate = 5 + 3 * static_cast<int>(index);
			builder.hero({coordinate, coordinate, 0}, HeroTypeID(HeroTypeID::decode(identifiers[index])), PlayerColor(0))
				.heroExperience(0).heroGarrison({{CreatureID(0), 1}});
		}
		startWithMap(std::move(builder));
		std::vector<CGHeroInstance *> heroes;
		for(size_t index = 0; index < identifiers.size(); ++index)
		{
			const int coordinate = 5 + 3 * static_cast<int>(index);
			heroes.push_back(findHeroAt({coordinate, coordinate, 0}));
		}
		return heroes;
	}

	static std::optional<Lean> leanOf(const CGHeroInstance & hero)
	{
		return heroClassLean::forHero(hero);
	}
};

TEST_F(HeroClassLeanGame, ClassesShowTheirGrowthBalance)
{
	const std::array<std::tuple<const char *, const char *, Lean>, 5> expected = {{
		{"core:orrin", "knight", {75, 25}},
		{"core:xsi", "necromancer", {25, 75}},
		{"core:piquedram", "alchemist", {50, 50}},
		{"core:yog", "barbarian", {90, 10}},
		{"core:solmyr", "wizard", {10, 90}},
	}};
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	std::vector<const char *> identifiers;
	for(const auto & row : expected)
		identifiers.push_back(std::get<0>(row));
	const auto heroes = startWithHeroes(identifiers);
	ASSERT_EQ(heroes.size(), expected.size());
	for(size_t row = 0; row < expected.size(); ++row)
	{
		const auto & [hero, heroClass, lean] = expected[row];
		SCOPED_TRACE(hero);
		const auto * instance = heroes[row];
		ASSERT_NE(instance, nullptr);
		ASSERT_TRUE(instance->usesPrimaryGrowth());
		ASSERT_EQ(instance->getHeroClass()->getJsonKey(), std::string("core:") + heroClass);

		const auto & configured = rules["classProfiles"][std::string("core:") + heroClass]["growth"].Vector();
		ASSERT_EQ(configured.size(), 4u);
		const auto growth = heroClassLean::classGrowth(*instance);
		ASSERT_TRUE(growth.has_value());
		for(size_t index = 0; index < configured.size(); ++index)
			EXPECT_EQ((*growth)[index], configured[index].Integer());
		EXPECT_EQ(leanOf(*instance), lean);
	}
}

TEST_F(HeroClassLeanGame, IgnoresTheHerosActualAttributes)
{
	auto * xsi = startWithHeroes({"core:xsi"}).front();
	ASSERT_NE(xsi, nullptr);
	ASSERT_EQ(leanOf(*xsi), (Lean{25, 75}));

	// Owner example values, then an extreme fighting spread, a higher level
	// and a large permanent Attack bonus: none of them describe the class.
	const std::array<std::array<int, 4>, 3> attributes = {{{5, 20, 50, 25}, {99, 99, 0, 0}, {0, 0, 99, 99}}};
	for(const auto & values : attributes)
	{
		for(int index = 0; index < GameConstants::PRIMARY_SKILLS; ++index)
			xsi->setPrimarySkill(PrimarySkill(index), values[index], ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(leanOf(*xsi), (Lean{25, 75}));
	}
	xsi->level = 30;
	xsi->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 500, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK)));
	xsi->nodeHasChanged();
	EXPECT_GE(xsi->getPrimSkillLevel(PrimarySkill::ATTACK), 500);
	EXPECT_EQ(leanOf(*xsi), (Lean{25, 75}));
}
