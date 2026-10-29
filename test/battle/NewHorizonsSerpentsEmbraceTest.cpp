/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
#include "../../lib/battle/StackEffectRemoval.h"
#include "../../lib/battle/NewHorizonsDebuffStatuses.h"
#include "../../lib/entities/hero/NewHorizonsSerpentsEmbrace.h"
#include "../../lib/GameLibrary.h"
#include <utility>
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace
{
class NewHorizonsSerpentsEmbraceTest : public testing::Test
{
protected:
	UnitInfoMock info;
	UnitEnvironmentMock environment;
	BonusBearerMock bonuses;
	std::shared_ptr<battle::CUnitStateDetached> unit;
	void SetUp() override
	{
		using namespace testing;
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(3));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(7));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 10, BonusSourceID(CreatureID(0))));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, 6, BonusSourceID()));
		unit = std::make_shared<battle::CUnitStateDetached>(&info, &bonuses);
		unit->localInit(&environment);
		unit->setPosition(BattleHex(40));
	}
};
}
TEST_F(NewHorizonsSerpentsEmbraceTest, MovementRestrictionPreservesCreatureActionPermission)
{
	ASSERT_GT(unit->getMovementRange(), 0);
	bonuses.addNewBonus(std::make_shared<Bonus>(newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")))));
	EXPECT_EQ(unit->getMovementRange(), 0);
	EXPECT_TRUE(unit->canMove()); // Existing action permission, not permission to change hex.
	EXPECT_TRUE(unit->ableToRetaliate());
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*unit));
}
TEST_F(NewHorizonsSerpentsEmbraceTest, IndependentSourcesHaveSelectiveReleasePlans)
{
	const auto first = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")));
	const auto second = newHorizonsSerpentsEmbrace::marker(12, CreatureID(CreatureID::decode("core:nagaQueen")));
	bonuses.addNewBonus(std::make_shared<Bonus>(first));
	bonuses.addNewBonus(std::make_shared<Bonus>(second));
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BIND_EFFECT, BonusSource::OTHER, 0, BonusSourceID()));
	ASSERT_NE(first.statusIdentity, second.statusIdentity);
	EXPECT_TRUE(battle::matchesStackEffectRemoval(first, first));
	EXPECT_FALSE(battle::matchesStackEffectRemoval(first, second));
	auto legacy = first;
	legacy.statusIdentity.clear();
	legacy.statusTags.clear();
	legacy.parameters.reset();
	EXPECT_FALSE(battle::matchesStackEffectRemoval(first, legacy));
	EXPECT_FALSE(battle::matchesStackEffectRemoval(legacy, first));
	EXPECT_TRUE(battle::matchesStackEffectRemoval(legacy, legacy));
	for(const auto source : {11u, 12u})
	{
		const auto plan = newHorizonsSerpentsEmbrace::releasePlan(*unit, source);
		ASSERT_EQ(plan.size(), 1u);
		EXPECT_EQ(newHorizonsSerpentsEmbrace::sourceId(plan.front()), source);
	}
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::releasePlan(*unit, 13).empty());
	EXPECT_EQ(unit->getMovementRange(), 0); // Planning does not mutate recipient state.
}
TEST(NewHorizonsSerpentsEmbraceMarker, RoundtripRetainsPhysicalSourceAndIndependentIdentity)
{
	const auto source = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:nagaQueen")));
	CMemorySerializer wire;
	auto writable = source;
	writable.serialize(wire.oser);
	Bonus loaded;
	loaded.serialize(wire.iser);
	EXPECT_EQ(newHorizonsSerpentsEmbrace::sourceId(loaded), 11);
	EXPECT_EQ(loaded.source, BonusSource::HERO_SPECIAL);
	EXPECT_NE(loaded.source, BonusSource::SPELL_EFFECT); // No ordinary Dispel/Cure spell-source identity.
	EXPECT_EQ(loaded.statusIdentity, source.statusIdentity);
	EXPECT_TRUE(vstd::contains(loaded.statusTags, BonusStatusTag::NON_TRANSFERABLE));
}
TEST(NewHorizonsSerpentsEmbraceMarker, MalformedSourceProvenanceRejectsWithoutDereferencingUnit)
{
	auto bonus = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")));
	bonus.parameters = std::make_shared<BonusParameters>(12);
	EXPECT_THROW(newHorizonsSerpentsEmbrace::sourceId(bonus), std::runtime_error);
	bonus = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")));
	bonus.statusIdentity = "serpentsEmbrace:4294967295";
	EXPECT_THROW(newHorizonsSerpentsEmbrace::sourceId(bonus), std::runtime_error);
	bonus.statusIdentity = "serpentsEmbrace:011";
	EXPECT_THROW(newHorizonsSerpentsEmbrace::sourceId(bonus), std::runtime_error);
	EXPECT_THROW(newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:pikeman"))), std::invalid_argument);
	EXPECT_THROW(newHorizonsSerpentsEmbrace::marker(std::numeric_limits<uint32_t>::max(), CreatureID(CreatureID::decode("core:naga"))), std::invalid_argument);
}

TEST(NewHorizonsSerpentsEmbraceMarker, OlderDirectAndEnclosingWritersRejectBeforePrefix)
{
	auto bonus = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")));
	CMemorySerializer direct, outer;
	direct.oser.version = outer.oser.version = ESerializationVersion::NEW_HORIZONS_MANA_LEECHES;
	EXPECT_THROW(bonus.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	SetStackEffect packet;
	packet.toUpdate.emplace_back(7, std::vector<Bonus>{bonus});
	EXPECT_THROW(packet.serialize(outer.oser), std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
}
TEST(NewHorizonsSerpentsEmbraceMarker, OldReaderRejectsSourceActivationSemantics)
{
	auto bonus = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")));
	CMemorySerializer current;
	bonus.serialize(current.oser);
	CMemorySerializer old(current.extractBuffer());
	old.iser.version = ESerializationVersion::NEW_HORIZONS_MANA_LEECHES;
	Bonus loaded;
	EXPECT_THROW(loaded.serialize(old.iser), std::runtime_error);
}
TEST(NewHorizonsSerpentsEmbraceMarker, MalformedTypedMarkerWriterRejectsBeforePrefix)
{
	auto bonus = newHorizonsSerpentsEmbrace::marker(11, CreatureID(CreatureID::decode("core:naga")));
	bonus.statusIdentity = "serpentsEmbrace";
	CMemorySerializer wire;
	EXPECT_THROW(bonus.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}

TEST_F(NewHorizonsSerpentsEmbraceTest, IndependentSourceLifetimesCountAsOneLogicalDebuffKind)
{
	bonuses.addNewBonus(std::make_shared<Bonus>(newHorizonsSerpentsEmbrace::marker(11,
		CreatureID(CreatureID::decode("core:naga")))));
	bonuses.addNewBonus(std::make_shared<Bonus>(newHorizonsSerpentsEmbrace::marker(12,
		CreatureID(CreatureID::decode("core:nagaQueen")))));
	const auto statuses = newHorizonsDebuffStatuses::snapshot(*unit);
	EXPECT_EQ(statuses.identities, std::vector<std::string>{"serpentsEmbrace"});
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*unit, 11).size(), 1u);
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*unit, 12).size(), 1u);
}

TEST(NewHorizonsSerpentsEmbracePolicy, CapturedDisabledAndAbsentRowsRemainDistinctAndRequireCurrentSupport)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_EQ(newHorizonsHeroes::serpentsEmbracePolicy(rules), std::optional<bool>(true));
	EXPECT_NO_THROW(newHorizonsHeroes::validateSerpentsEmbraceSerialization(rules, true));
	EXPECT_THROW(newHorizonsHeroes::validateSerpentsEmbraceSerialization(rules, false), std::runtime_error);
	rules["additionalSpecialties"]["serpentsEmbrace"]["enabled"].Bool() = false;
	EXPECT_EQ(newHorizonsHeroes::serpentsEmbracePolicy(rules), std::optional<bool>(false));
	EXPECT_THROW(newHorizonsHeroes::validateSerpentsEmbraceSerialization(rules, false), std::runtime_error);
	rules["additionalSpecialties"].Struct().erase("serpentsEmbrace");
	EXPECT_EQ(newHorizonsHeroes::serpentsEmbracePolicy(rules), std::nullopt);
	EXPECT_NO_THROW(newHorizonsHeroes::validateSerpentsEmbraceSerialization(rules, false));
}

namespace
{
struct WithoutLibrary
{
	GameLibrary * previous = std::exchange(LIBRARY, nullptr);
	~WithoutLibrary() { LIBRARY = previous; }
};
Bonus rawRestraint()
{
	// Synthetic numeric hero SID: structural transport never resolves its identity.
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT,
		BonusSource::HERO_SPECIAL, 0, BonusSourceID(HeroTypeID(0)));
	result.statusIdentity = "serpentsEmbrace:11";
	result.parameters = std::make_shared<BonusParameters>(11);
	result.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	return result;
}
}
TEST(NewHorizonsSerpentsEmbraceMarker, LibraryFreeStructuralPreflightAndRegistryBackedCurrentRoundtrip)
{
	Bonus raw;
	{
		WithoutLibrary isolated;
		ASSERT_EQ(LIBRARY, nullptr);
		raw = rawRestraint();
		EXPECT_EQ(newHorizonsSerpentsEmbrace::sourceId(raw), 11u);
		CMemorySerializer currentPreflight;
		EXPECT_NO_THROW(raw.validateSerpentsEmbraceSerialization(currentPreflight.oser));
		EXPECT_TRUE(currentPreflight.extractBuffer().empty());
		CMemorySerializer direct, enclosing;
		direct.oser.version = enclosing.oser.version = ESerializationVersion::NEW_HORIZONS_MANA_LEECHES;
		EXPECT_THROW(raw.serialize(direct.oser), std::runtime_error);
		EXPECT_TRUE(direct.extractBuffer().empty());
		SetStackEffect packet;
		packet.toAdd.emplace_back(7, std::vector<Bonus>{raw});
		EXPECT_THROW(packet.serialize(enclosing.oser), std::runtime_error);
		EXPECT_TRUE(enclosing.extractBuffer().empty());
	}
	// Complete symbolic HeroTypeID wire encode/decode requires its loaded registry.
	ASSERT_NE(LIBRARY, nullptr);
	CMemorySerializer current;
	ASSERT_NO_THROW(raw.serialize(current.oser));
	auto bytes = current.extractBuffer();
	CMemorySerializer restored(bytes);
	Bonus loaded;
	ASSERT_NO_THROW(loaded.serialize(restored.iser));
	EXPECT_EQ(newHorizonsSerpentsEmbrace::sourceId(loaded), 11u);
	{
		WithoutLibrary isolated;
		CMemorySerializer decodedBoundary;
		EXPECT_NO_THROW(loaded.validateSerpentsEmbraceSerialization(decodedBoundary.iser));
		decodedBoundary.iser.version = ESerializationVersion::NEW_HORIZONS_MANA_LEECHES;
		EXPECT_THROW(loaded.validateSerpentsEmbraceSerialization(decodedBoundary.iser), std::runtime_error);
	}
	CMemorySerializer unsupported(bytes);
	unsupported.iser.version = ESerializationVersion::NEW_HORIZONS_MANA_LEECHES;
	EXPECT_THROW(loaded.serialize(unsupported.iser), std::runtime_error);
}
TEST(NewHorizonsSerpentsEmbraceMarker, LibraryFreeMalformedCurrentAndOldBoundariesRejectWithoutPrefix)
{
	{
		WithoutLibrary isolated;
		for(const auto version : {ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_MANA_LEECHES})
		{
			auto raw = rawRestraint();
			raw.parameters = std::make_shared<BonusParameters>(12);
			CMemorySerializer direct, enclosing;
			direct.oser.version = enclosing.oser.version = version;
			EXPECT_THROW(raw.serialize(direct.oser), std::runtime_error);
			EXPECT_TRUE(direct.extractBuffer().empty());
			SetStackEffect packet;
			packet.toAdd.emplace_back(7, std::vector<Bonus>{raw});
			EXPECT_THROW(packet.serialize(enclosing.oser), std::runtime_error);
			EXPECT_TRUE(enclosing.extractBuffer().empty());
		}
		auto wrongType = rawRestraint();
		wrongType.sid = BonusSourceID(CreatureID(0));
		EXPECT_THROW(newHorizonsSerpentsEmbrace::sourceId(wrongType), std::runtime_error);
	}
	// The tampered-reader fixture retains the normal symbolic-ID registry prerequisite.
	ASSERT_NE(LIBRARY, nullptr);
	auto raw = rawRestraint();
	CMemorySerializer current;
	raw.serialize(current.oser);
	auto bytes = current.extractBuffer();
	const std::string wire(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	const auto position = wire.find("serpentsEmbrace:11");
	ASSERT_NE(position, std::string::npos);
	bytes[position + std::string("serpentsEmbrace:11").size() - 1] = std::byte{'2'};
	CMemorySerializer malformed(bytes);
	Bonus loaded;
	EXPECT_THROW(loaded.serialize(malformed.iser), std::runtime_error);
}
