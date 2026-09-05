/*
 * AutocombatPreferencesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../mock/mock_battle_Unit.h"

#include "../../lib/battle/AutocombatPreferences.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/CCreatureHandler.h"

namespace test
{
namespace
{
using ::testing::NiceMock;
using ::testing::Return;
using Mode = AutocombatPreferences::Mode;

class AutocombatPreferencesTest : public ::testing::Test
{
protected:
	AutocombatPreferences preferences;
	NiceMock<UnitMock> creature;
	NiceMock<UnitMock> catapult;
	NiceMock<UnitMock> ballista;
	NiceMock<UnitMock> tent;
	NiceMock<UnitMock> tower;

	std::array<UnitMock *, 5> units()
	{
		return {&creature, &catapult, &ballista, &tent, &tower};
	}

	void SetUp() override
	{
		const std::array<CreatureID, 5> types = {
			CreatureID::IMP, CreatureID::CATAPULT, CreatureID::BALLISTA,
			CreatureID::FIRST_AID_TENT, CreatureID::ARROW_TOWERS
		};
		const auto subjects = units();
		for(size_t i = 0; i < subjects.size(); ++i)
		{
			const auto * type = types[i].toCreature();
			ASSERT_NE(type, nullptr);
			ON_CALL(*subjects[i], unitType()).WillByDefault(Return(type));
			ON_CALL(*subjects[i], creatureIndex()).WillByDefault(Return(types[i].getNum()));
			ON_CALL(*subjects[i], creatureId()).WillByDefault(Return(types[i]));
		}
	}

	void disableAllCategories()
	{
		preferences.enableUnitsUsage = false;
		preferences.enableCatapultUsage = false;
		preferences.enableBallistaUsage = false;
		preferences.enableFirstAidTentUsage = false;
	}
};

TEST_F(AutocombatPreferencesTest, DefaultsPreserveAutomaticDelegation)
{
	EXPECT_TRUE(preferences.enableSpellsUsage);
	EXPECT_TRUE(preferences.enableTacticsUsage);
	EXPECT_TRUE(preferences.enableUnitsUsage);
	EXPECT_TRUE(preferences.enableCatapultUsage);
	EXPECT_TRUE(preferences.enableBallistaUsage);
	EXPECT_TRUE(preferences.enableFirstAidTentUsage);
	for(const auto * unit : units())
	{
		EXPECT_TRUE(preferences.controlsUnit(*unit, Mode::SELECTIVE));
		EXPECT_TRUE(preferences.controlsUnit(*unit, Mode::FULL_BATTLE));
	}
}

// These four exclusions must fail against the initial all-delegated hook.
TEST_F(AutocombatPreferencesTest, SelectiveCreaturesExcluded)
{
	preferences.enableUnitsUsage = false;
	EXPECT_FALSE(preferences.controlsUnit(creature, Mode::SELECTIVE));
}

TEST_F(AutocombatPreferencesTest, SelectiveCatapultExcluded)
{
	preferences.enableCatapultUsage = false;
	EXPECT_FALSE(preferences.controlsUnit(catapult, Mode::SELECTIVE));
}

TEST_F(AutocombatPreferencesTest, SelectiveBallistaExcluded)
{
	preferences.enableBallistaUsage = false;
	EXPECT_FALSE(preferences.controlsUnit(ballista, Mode::SELECTIVE));
}

TEST_F(AutocombatPreferencesTest, SelectiveFirstAidTentExcluded)
{
	preferences.enableFirstAidTentUsage = false;
	EXPECT_FALSE(preferences.controlsUnit(tent, Mode::SELECTIVE));
}

TEST_F(AutocombatPreferencesTest, SelectiveExclusionDoesNotDisableOtherCategories)
{
	const std::array<bool AutocombatPreferences::*, 4> flags = {
		&AutocombatPreferences::enableUnitsUsage,
		&AutocombatPreferences::enableCatapultUsage,
		&AutocombatPreferences::enableBallistaUsage,
		&AutocombatPreferences::enableFirstAidTentUsage
	};
	const auto subjects = units();
	for(size_t disabled = 0; disabled < flags.size(); ++disabled)
	{
		SCOPED_TRACE(disabled);
		preferences = AutocombatPreferences();
		preferences.*flags[disabled] = false;
		for(size_t other = 0; other < subjects.size(); ++other)
		{
			if(other != disabled)
			{
				EXPECT_TRUE(preferences.controlsUnit(*subjects[other], Mode::SELECTIVE));
			}
		}
	}
}

TEST_F(AutocombatPreferencesTest, FullBattleOverridesExclusionsWithoutMutatingPreferences)
{
	disableAllCategories();
	preferences.enableSpellsUsage = false;
	preferences.enableTacticsUsage = false;
	for(const auto * unit : units())
		EXPECT_TRUE(preferences.controlsUnit(*unit, Mode::FULL_BATTLE));

	EXPECT_FALSE(preferences.enableUnitsUsage);
	EXPECT_FALSE(preferences.enableCatapultUsage);
	EXPECT_FALSE(preferences.enableBallistaUsage);
	EXPECT_FALSE(preferences.enableFirstAidTentUsage);
	EXPECT_FALSE(preferences.enableSpellsUsage);
	EXPECT_FALSE(preferences.enableTacticsUsage);
}

TEST_F(AutocombatPreferencesTest, TowersRemainAutomaticWithAllCategoriesDisabled)
{
	disableAllCategories();
	// Artillery eligibility does not establish which original checkbox controlled towers.
	EXPECT_TRUE(preferences.controlsUnit(tower, Mode::SELECTIVE));
}
}
}
