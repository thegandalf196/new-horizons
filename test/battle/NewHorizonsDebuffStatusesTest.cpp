/*
 * NewHorizonsDebuffStatusesTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/BattleFake.h"
#include "../../lib/battle/NewHorizonsDebuffStatuses.h"
#include "../../lib/battle/NewHorizonsOffense.h"

class NewHorizonsDebuffStatusesTest : public testing::Test
{
protected:
	testing::NiceMock<test::battle::UnitFake> unit;
	void SetUp() override { unit.redirectBonusesToFake(); }
	std::shared_ptr<Bonus> component(const std::string & identity, BonusType type = BonusType::MORALE)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::OTHER, -2, BonusSourceID());
		bonus->turnsRemain = 3;
		bonus->statusTags = {BonusStatusTag::DEBUFF};
		bonus->statusIdentity = identity;
		unit.addNewBonus(bonus);
		return bonus;
	}
};

TEST_F(NewHorizonsDebuffStatusesTest, ComponentsRepeatedApplicationsAndRefreshShareIdentity)
{
	component("disease");
	component("disease", BonusType::LUCK);
	component("disease", BonusType::STACKS_SPEED);
	component("poison");
	component("poison", BonusType::LUCK);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).identities, (std::vector<std::string>{"disease", "poison"}));
}

TEST_F(NewHorizonsDebuffStatusesTest, NegativeSignAndDurationDoNotInferStatus)
{
	unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::CREATURE_ABILITY, -10, BonusSourceID()));
	unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::LUCK,
		BonusSource::OTHER, -10, BonusSourceID()));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).count(), 0u);
	auto ongoing = component("ongoing");
	ongoing->duration = BonusDuration::PERMANENT;
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).count(), 1u);
}

TEST_F(NewHorizonsDebuffStatusesTest, SnapshotRemainsImmutableWhenLaterApplicationsChange)
{
	component("slow");
	const auto captured = newHorizonsDebuffStatuses::snapshot(unit);
	component("curse");
	EXPECT_EQ(captured.identities, (std::vector<std::string>{"slow"}));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).count(), 2u);
}

TEST_F(NewHorizonsDebuffStatusesTest, NoQuarterActualComponentsCountOneLogicalStatus)
{
	unit.addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::noQuarterRetaliationBonus()));
	unit.addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::noQuarterMoralePenalty()));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).count(), 1u);
}

TEST_F(NewHorizonsDebuffStatusesTest, LegalHistoricalTagOnlyMetadataDoesNotThrow)
{
	auto legacy = component("");
	ASSERT_TRUE(legacy->hasValidStatusMetadata());
	EXPECT_NO_THROW(newHorizonsDebuffStatuses::snapshot(unit));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).count(), 1u);
}

TEST_F(NewHorizonsDebuffStatusesTest, StrippedOldAndTaggedNewNoQuarterComponentsDeduplicate)
{
	for(const auto bonus : {newHorizonsOffense::noQuarterRetaliationBonus(), newHorizonsOffense::noQuarterMoralePenalty()})
	{
		unit.addNewBonus(std::make_shared<Bonus>(bonus));
		auto legacy = bonus;
		legacy.statusTags.clear();
		legacy.statusIdentity.clear();
		unit.addNewBonus(std::make_shared<Bonus>(legacy));
	}
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(unit).identities,
		(std::vector<std::string>{newHorizonsOffense::NO_QUARTER}));
}
