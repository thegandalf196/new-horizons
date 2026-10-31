/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/UnitAdditionRules.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/json/JsonNode.h"
#include <stdexcept>
namespace
{
using Resolver = battle::OwnedUnitCreatureResolver;
Resolver::Creature definition()
{
	Resolver::Creature value;
	value.id = CreatureID(7); value.key = "owned:spawn";
	value.faction = FactionID(2); value.baseHitPoints = 10; value.aiValue = 17;
	return value;
}
struct WithoutLibrary
{
	GameLibrary * saved = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = saved; }
};
class NegativeDecoder final : public battle::UnitCreatureResolver
{
public:
	const CCreature * creature(CreatureID) const override { return nullptr; }
	int32_t decode(const std::string &) const override { return -1; }
	std::string encode(int32_t) const override { throw std::logic_error("Unknown prototype"); }
};
}
TEST(OwnedUnitAdditionResolverTest, FullNatureSpawnMetadataRoundTripsWithoutNativeIdentifiers)
{
	Resolver resolver({definition()});
	WithoutLibrary absent;
	battle::UnitInfo original;
	original.id = 9; original.type = CreatureID(7); original.count = 4;
	original.side = BattleSide::DEFENDER; original.position = BattleHex(85);
	original.summoned = true; original.natureSummoned = true; original.summonRoundsRemaining = 2;
	JsonNode data; original.save(data, resolver);
	ASSERT_EQ(data["type"].String(), "owned:spawn");
	battle::UnitInfo decoded; decoded.load(9, data, resolver);
	EXPECT_EQ(decoded.id, 9u); EXPECT_EQ(decoded.type, CreatureID(7)); EXPECT_EQ(decoded.count, 4);
	EXPECT_EQ(decoded.side, BattleSide::DEFENDER); EXPECT_EQ(decoded.position, BattleHex(85));
	EXPECT_TRUE(decoded.summoned); EXPECT_TRUE(decoded.natureSummoned);
	EXPECT_EQ(decoded.summonRoundsRemaining, 2);
	JsonNode again; decoded.save(again, resolver); EXPECT_EQ(again, data);
}
TEST(OwnedUnitAdditionResolverTest, LegacyAbsentFieldsMatchNativeDefaultsAndUnknownKeysNeverBecomeNone)
{
	Resolver resolver({definition()});
	WithoutLibrary absent;
	JsonNode legacy; legacy["count"].Integer() = 2;
	battle::UnitInfo native, owned;
	native.load(3, legacy); // Empty type takes the exact native default without registry access.
	owned.load(3, legacy, resolver);
	EXPECT_EQ(owned.type, native.type); EXPECT_EQ(owned.type, CreatureID(CreatureID::NONE));
	EXPECT_EQ(owned.side, native.side); EXPECT_EQ(owned.position, native.position);
	EXPECT_EQ(owned.summoned, native.summoned); EXPECT_EQ(owned.natureSummoned, native.natureSummoned);
	EXPECT_EQ(owned.summonRoundsRemaining, native.summonRoundsRemaining);
	legacy["type"].String() = "owned:missing";
	EXPECT_THROW(owned.load(3, legacy, resolver), std::logic_error);
	NegativeDecoder missing;
	EXPECT_THROW(owned.load(3, legacy, missing), std::logic_error);
}
TEST(OwnedUnitAdditionResolverTest, ExistingRebirthAndCapacityGuardsUseOwnedDescriptorAtTheSameLazyPoint)
{
	Resolver resolver({definition()});
	WithoutLibrary absent;
	battle::UnitInfo original;
	original.type = CreatureID(7); original.count = 3; original.side = BattleSide::ATTACKER;
	original.position = BattleHex(80); original.summoned = true; original.rebirthOriginalAggregateHP = 29;
	JsonNode data; original.save(data, resolver); JsonNode legacyRules;
	const auto parsed = battle::decodeUnitAddition(5, data, legacyRules, 5, resolver);
	EXPECT_EQ(parsed.type, CreatureID(7)); EXPECT_EQ(parsed.rebirthOriginalAggregateHP, 29);
	EXPECT_NO_THROW(battle::validateRebirthAdditionSide(parsed));
	EXPECT_NO_THROW(battle::validateUnitAdditionCapacity(parsed, 10));
	auto excessive = parsed; excessive.rebirthOriginalAggregateHP = 31;
	EXPECT_THROW(battle::validateUnitAdditionCapacity(excessive, 10), std::runtime_error);
	data["natureSummoned"].Bool() = true;
	EXPECT_THROW(battle::decodeUnitAddition(5, data, legacyRules, 5, resolver), std::runtime_error);
	data["natureSummoned"].Bool() = false; data["count"].Integer() = 0;
	EXPECT_THROW(battle::decodeUnitAddition(5, data, legacyRules, 5, resolver), std::runtime_error);
}
