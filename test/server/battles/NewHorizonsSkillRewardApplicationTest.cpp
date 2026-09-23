/*
 * NewHorizonsSkillRewardApplicationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
class SkillRewardObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;
	using Rewardable::Interface::grantRewardBeforeLevelup;
	using Rewardable::Interface::getAvailableRewards;
};
}

class NewHorizonsSkillRewardApplicationTest : public HeroCommandFixture
{
protected:
	void startGameAsThane(bool newHorizons = true)
	{
		useCommands = newHorizons;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:thane")), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroTypeID(), HeroTypeID(HeroTypeID::decode("core:thane")));
		ASSERT_EQ(attackerSideHero->getHeroClass()->getJsonKey(), "core:alchemist");
	}
};

TEST_F(NewHorizonsSkillRewardApplicationTest, RawRewardAdjustsExistingEffectiveRankWithoutDowngradingOrIgnoringLoss)
{
	prepareCommands(true);
	const auto effective = newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), SecondarySkill::FIRE_MAGIC);
	attackerSideHero->setSecSkillLevel(effective, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	// Actual Rewardable::Interface calculation and authoritative game callbacks;
	// not an asserted graphical visit or bypass of player choice validation.
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(effective), MasteryLevel::EXPERT);
	if(effective != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);

	info.reward.secondary[SecondarySkill::FIRE_MAGIC] = -1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(effective), MasteryLevel::ADVANCED);
	if(effective != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, NewSkillTeacherRewardRespectsAlchemistOfferWeight)
{
	startGameAsThane();
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	const SecondarySkill lightMagic(SecondarySkill::decode("new-horizons:lightMagic"));

	ASSERT_TRUE(newHorizonsHeroes::usesSkillOfferWeights(attackerSideHero->getPrimaryGrowthRules()));
	const auto wisdomWeight = newHorizonsHeroes::skillOfferWeight(attackerSideHero->getPrimaryGrowthRules(), wisdom);
	ASSERT_TRUE(wisdomWeight);
	EXPECT_EQ(*wisdomWeight, 0);
	const auto lightMagicWeight = newHorizonsHeroes::skillOfferWeight(attackerSideHero->getPrimaryGrowthRules(), lightMagic);
	ASSERT_TRUE(lightMagicWeight);
	EXPECT_GT(*lightMagicWeight, 0);
	ASSERT_TRUE(attackerSideHero->canLearnSkill());
	EXPECT_FALSE(attackerSideHero->canLearnSkill(wisdom));
	ASSERT_TRUE(attackerSideHero->canLearnSkill(lightMagic));

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[wisdom] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::NONE);

	info.reward.secondary.clear();
	info.reward.secondary[lightMagic] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(lightMagic), MasteryLevel::BASIC);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, MixedTeacherRewardKeepsResourcesWhenSkillIsForbidden)
{
	startGameAsThane();
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	const auto goldBefore = gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD];

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.resources[GameResID::GOLD] = 100;
	info.reward.secondary[wisdom] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);

	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD], goldBefore + 100);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::NONE);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, LegacyDirectSkillRewardStillUsesFreeSlotEligibility)
{
	startGameAsThane(false);
	ASSERT_FALSE(newHorizonsHeroes::usesSkillOfferWeights(attackerSideHero->getPrimaryGrowthRules()));
	ASSERT_TRUE(attackerSideHero->canLearnSkill());

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[SecondarySkill::WISDOM] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);

	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::BASIC);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, ExistingSkillTeacherRewardCanUpgradeWithFullSkillBar)
{
	startGameAsThane();
	const SecondarySkill lightMagic(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	for(const auto & candidate : LIBRARY->skillh->objects)
	{
		if(!attackerSideHero->canLearnSkill())
			break;
		if(!candidate || attackerSideHero->getSecSkillLevel(candidate->getId()) != MasteryLevel::NONE)
			continue;
		attackerSideHero->setSecSkillLevel(candidate->getId(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}
	ASSERT_FALSE(attackerSideHero->canLearnSkill());

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	info.reward.secondary[lightMagic] = 1;
	object.configuration.info.push_back(info);
	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{0}));
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(lightMagic), MasteryLevel::ADVANCED);
}
