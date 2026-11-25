/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSoulHarvestFixture.h"
#include "../../../lib/spells/CSpell.h"

namespace
{
class SoulHarvestPresentationTest : public NewHorizonsSoulHarvestTest
{
protected:
	void expectHarvestVisual()
	{
		const auto * resurrection = SpellID(SpellID::RESURRECTION).toSpell();
		ASSERT_NE(resurrection, nullptr);
		ASSERT_EQ(resurrection->animationInfo.affect.size(), 1u);
		ASSERT_EQ(server.battleAnimations.size(), 1u);
		const auto & visual = server.battleAnimations.front();
		EXPECT_EQ(visual.battleID, BattleID(0));
		EXPECT_EQ(visual.animation, resurrection->animationInfo.affect.front().resourceName);
		EXPECT_EQ(visual.sound, AudioPath::builtin("DRAINLIF"));
		EXPECT_FLOAT_EQ(visual.transparency, resurrection->animationInfo.affect.front().transparency);
		EXPECT_FALSE(visual.deferred);
		ASSERT_EQ(visual.targets.size(), 1u);
		EXPECT_EQ(visual.targets.front().unitID, static_cast<int32_t>(ghost->unitId()));
		EXPECT_EQ(visual.targets.front().tile, ghost->getPosition());
		EXPECT_TRUE(server.casts.empty());
	}
};
}

TEST_F(SoulHarvestPresentationTest, AcceptedKillPresentsTheGrowingGhost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	server.battleAnimations.clear();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 11);
	expectHarvestVisual();
}

TEST_F(SoulHarvestPresentationTest, SeveralCasualtiesInSurvivingStackPresentOneGrowthEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 1000));
	blockRetaliation(ghost);
	server.battleAnimations.clear();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_TRUE(target->alive());
	ASSERT_GT(ghost->getCount(), 11);
	expectHarvestVisual();
}

TEST_F(SoulHarvestPresentationTest, MultipleVictimsInOneStrikePresentOneGrowthEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ghost->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	server.battleAnimations.clear();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 12);
	expectHarvestVisual();
}

TEST_F(SoulHarvestPresentationTest, RetaliationGrowthUsesTheSameEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000, 3));
	const auto blocks = target->getBonuses(Selector::type()(BonusType::BLOCKS_RETALIATION));
	ASSERT_EQ(blocks->size(), 1u);
	target->removeBonus((*blocks)[0]);
	server.battleAnimations.clear();
	ASSERT_TRUE(attack(target, ghost->getPosition()));
	ASSERT_EQ(ghost->getHealth().getResurrected(), 3);
	expectHarvestVisual();
}

TEST_F(SoulHarvestPresentationTest, NonlethalHitDoesNotPresentGrowth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1, 1000));
	blockRetaliation(ghost);
	server.battleAnimations.clear();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 1);
	EXPECT_TRUE(server.battleAnimations.empty());
}

TEST_F(SoulHarvestPresentationTest, CloneVictimDoesNotPresentGrowth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->cloned = true;
	server.battleAnimations.clear();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 10);
	EXPECT_TRUE(server.battleAnimations.empty());
}
