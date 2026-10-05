/*
 * NewHorizonsWarMachineHitPointsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"

#include <algorithm>
#include <array>

namespace
{
CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

struct ExpectedWarMachine
{
	const char * identifier;
	CreatureID id;
	int hitPoints;
};

class NewHorizonsWarMachineHitPointsTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		const auto activeMods = LIBRARY->modh->getActiveMods();
		if(std::find(activeMods.begin(), activeMods.end(), GameConstants::NEW_HORIZONS_MOD_SCOPE) == activeMods.end())
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void startTestMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsWarMachineHitPoints")
			.playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};
}

TEST_F(NewHorizonsWarMachineHitPointsTest, LoadedMachineDefinitionsUseCanonicalHitPoints)
{
	ASSERT_NO_FATAL_FAILURE(startTestMap());

	const std::array<ExpectedWarMachine, 4> expected = {{
		{"core:ballista", creature("core:ballista"), 300},
		{"core:catapult", creature("core:catapult"), 500},
		{"core:firstAidTent", creature("core:firstAidTent"), 250},
		{"core:ammoCart", creature("core:ammoCart"), 250}
	}};

	for(const auto & row : expected)
	{
		ASSERT_NE(row.id, CreatureID::NONE) << row.identifier;
		const auto * definition = LIBRARY->creh->getById(row.id);
		ASSERT_NE(definition, nullptr) << row.identifier;
		EXPECT_EQ(definition->getBaseHitPoints(), row.hitPoints) << row.identifier;
	}
}
