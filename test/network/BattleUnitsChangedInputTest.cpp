/*
 * BattleUnitsChangedInputTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../server/BattleUnitsChangedInput.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <bit>
#include <cstdint>
#include <functional>

namespace
{
BattleUnitsChanged input()
{
	BattleUnitsChanged p;
	p.battleID = BattleID(3);
	p.changedStacks.emplace_back(4, BattleChanges::EOperation::UPDATE);
	p.changedStacks[0].healthDelta = -7;
	p.changedStacks[0].data["vector"].Vector() = {JsonNode(int64_t{1}), JsonNode("value")};
	p.rebirthChainConsumption.emplace();
	p.rebirthChainConsumption->side = BattleSide::ATTACKER;
	p.rebirthChainConsumption->sourceUnitId = 10;
	p.rebirthChainConsumption->spawnUnitId = 11;
	p.phoenixSparkConsumption.emplace();
	p.phoenixSparkConsumption->side = BattleSide::DEFENDER;
	p.phoenixSparkConsumption->sourceUnitId = 12;
	return p; // Deliberately not a valid envelope: equality is not validation.
}
}

TEST(BattleUnitsChangedInputTest, AllOptionalMetadataFieldsAreComparedWithoutValidation)
{
	const auto original = input();
	EXPECT_TRUE(sameBattleUnitsChangedInput(original, original));
	using Change = std::function<void(BattleUnitsChanged &)>;
	const std::vector<Change> changes{
		[](auto & p) { p.rebirthChainConsumption.reset(); },
		[](auto & p) { p.rebirthChainConsumption->side = BattleSide::DEFENDER; },
		[](auto & p) { ++p.rebirthChainConsumption->sourceUnitId; },
		[](auto & p) { ++p.rebirthChainConsumption->spawnUnitId; },
		[](auto & p) { p.rebirthChainConsumption->rollback = true; },
		[](auto & p) { p.phoenixSparkConsumption.reset(); },
		[](auto & p) { p.phoenixSparkConsumption->side = BattleSide::ATTACKER; },
		[](auto & p) { ++p.phoenixSparkConsumption->sourceUnitId; }
	};
	for(const auto & change : changes)
	{
		auto altered = original;
		change(altered);
		EXPECT_FALSE(sameBattleUnitsChangedInput(original, altered));
		EXPECT_FALSE(sameBattleUnitsChangedInput(altered, original));
	}
}

TEST(BattleUnitsChangedInputTest, NestedJsonMetadataTypeAndMissingNullAreNotNormalized)
{
	const auto original = input();
	using Change = std::function<void(BattleUnitsChanged &)>;
	const std::vector<Change> changes{
		[](auto & p) { p.changedStacks[0].data["vector"][0].setModScope("changed", false); },
		[](auto & p) { p.changedStacks[0].data["vector"][0].setOverrideFlag(true); },
		[](auto & p) { p.changedStacks[0].data["vector"][0] = JsonNode(1.0); },
		[](auto & p) { p.changedStacks[0].data["missing"] = JsonNode(); },
		[](auto & p) { p.changedStacks[0].data["vector"].Vector().push_back(JsonNode()); },
		[](auto & p) { p.changedStacks[0].data["vector"][1] = JsonNode("changed"); }
	};
	for(const auto & change : changes)
	{
		auto altered = original;
		change(altered);
		EXPECT_FALSE(sameBattleUnitsChangedInput(original, altered));
	}
}

TEST(BattleUnitsChangedInputTest, FloatingPayloadBitsAreExactIncludingSignedZeroAndNan)
{
	auto first = input();
	first.changedStacks[0].data["value"] = JsonNode(0.0);
	auto second = first;
	second.changedStacks[0].data["value"] = JsonNode(-0.0);
	EXPECT_FALSE(sameBattleUnitsChangedInput(first, second));
	first.changedStacks[0].data["value"] = JsonNode(std::bit_cast<double>(uint64_t{0x7ff8000000000001}));
	second = first;
	EXPECT_TRUE(sameBattleUnitsChangedInput(first, second));
	second.changedStacks[0].data["value"] = JsonNode(std::bit_cast<double>(uint64_t{0x7ff8000000000002}));
	EXPECT_FALSE(sameBattleUnitsChangedInput(first, second));
}
