/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsAerialSupremacyFixture.h"
#include "../../../lib/battle/NewHorizonsParalyzingGaze.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
namespace
{
class NewHorizonsAerialGazeTest : public NewHorizonsAerialSupremacyBattleTest
{
protected:
	void mapLoaded(CMap * map) override
	{
		NewHorizonsAerialSupremacyBattleTest::mapLoaded(map);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ajit")));
	}
	void installGaze()
	{
		SetStackEffect effect;
		effect.battleID = battle()->getBattleID();
		effect.toAdd.emplace_back(griffin->unitId(), std::vector<Bonus>{newHorizonsParalyzingGaze::marker()});
		gameHandler->sendAndApply(effect);
	}
	void accept(const char * perk)
	{
		const auto rank = [this](const std::string & id) { return defenderSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = defenderSideHero->getPerkState().prepareOffer(rank, seed, defenderSideHero->getPerkOfferWeightContext());
			const auto choice = std::ranges::find_if(offer, [perk](const auto & entry)
				{ return entry.selection.skillId == "new-horizons:armorer" && entry.selection.perkId == perk; });
			if(choice == offer.end()) continue;
			gameHandler->levelUpHero(defenderSideHero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(defenderSideHero->hasActivePerk("new-horizons:armorer", perk));
			return;
		}
		FAIL() << "No legitimate Defiant offer";
	}
	void acquireDefiant()
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:armorer"));
		// BattleTestFixture clears ranks but retains authored perk selections.
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(!defenderSideHero->getPerkState().canAdvanceSkillNormally("new-horizons:armorer", MasteryLevel::BASIC))
		{
			ASSERT_NO_FATAL_FAILURE(accept("new-horizons:armorer.countercharge"));
		}
		ASSERT_TRUE(defenderSideHero->getPerkState().canAdvanceSkillNormally("new-horizons:armorer", MasteryLevel::BASIC));
		ASSERT_EQ(defenderSideHero->getPerkSkillRank("new-horizons:armorer"), MasteryLevel::BASIC);
		ASSERT_TRUE(defenderSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept("new-horizons:armorer.defiant"));
	}
};
}
TEST_F(NewHorizonsAerialGazeTest, GazeCancelsOneTypedOpportunityAndConsumesRegularAerialLimitWithoutFlight)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	installGaze();
	const auto origin = griffin->getPosition();
	const auto health = shooter->getAvailableHealth();
	const auto charges = griffin->counterAttacks.available();
	ASSERT_TRUE(shoot());
	EXPECT_EQ(server.attacks.size(), 1);
	EXPECT_EQ(griffin->getPosition(), origin);
	EXPECT_EQ(shooter->getAvailableHealth(), health);
	EXPECT_EQ(griffin->counterAttacks.available(), charges - 1);
	EXPECT_EQ(griffin->aerialRetaliationRound, battle()->getRound());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*griffin));
}
TEST_F(NewHorizonsAerialGazeTest, DefiantConsumesItsBudgetThenAllowsThePhysicalAerialCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	installGaze();
	ASSERT_TRUE(shoot());
	ASSERT_EQ(server.attacks.size(), 2);
	EXPECT_TRUE(server.attacks.back().aerialExcursion);
	EXPECT_TRUE(server.attacks.back().counter());
	EXPECT_FALSE(server.attacks.back().shot());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*griffin));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
}
