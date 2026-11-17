/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/CreatureAbility.h"
#include "../../lib/battle/StackEffectRefresh.h"
#include "../../lib/battle/StackEffectRemoval.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using namespace creatureAbility;
using Version = ESerializationVersion;
struct Features
{
	using Version = ESerializationVersion;
	Version version;
	bool hasFeature(Version feature) const { return version >= feature; }
};
Bonus marker()
{
	Bonus result(BonusDuration::N_TURNS, BonusType::MORALE, BonusSource::OTHER, -1, BonusSourceID());
	result.turnsRemain = 2;
	result.abilityDescriptor = nativeAffliction(Native::CONCUSSIVE_STUN, Family::CREATURE_ABILITY, PlayerColor(0));
	return result;
}
template<typename T> void rejectsBeforePrefix(T & object)
{
	CMemorySerializer stream;
	stream.oser.version = Version::NEW_HORIZONS_SHARED_CAPTURED_RULES;
	EXPECT_THROW(stream.oser & object, std::runtime_error);
	EXPECT_TRUE(stream.extractBuffer().empty());
}
}

TEST(CreatureAbilitySerialization, MetadataRoundTripRetainsSharedLogicalGroup)
{
	std::vector<Bonus> source{marker(), marker(), marker()};
	for(auto & bonus : source) bonus.abilityDescriptor = source.front().abilityDescriptor;
	CMemorySerializer stream;
	stream.oser & source;
	std::vector<Bonus> decoded;
	stream.iser & decoded;
	ASSERT_EQ(decoded.size(), 3);
	ASSERT_TRUE(decoded[0].abilityDescriptor);
	EXPECT_EQ(*decoded[0].abilityDescriptor, *source[0].abilityDescriptor);
	EXPECT_EQ(decoded[0].abilityDescriptor, decoded[1].abilityDescriptor);
	EXPECT_EQ(decoded[1].abilityDescriptor, decoded[2].abilityDescriptor);
}

TEST(CreatureAbilitySerialization, JsonRoundTripRejectsUnknownOrAmbiguousMetadata)
{
	const auto source = marker().abilityDescriptor;
	const auto json = source->toJson();
	EXPECT_EQ(*CreatureAbilityDescriptor::fromJson(json), *source);
	auto malformed = json;
	malformed["native"].Integer() = 255;
	EXPECT_THROW(CreatureAbilityDescriptor::fromJson(malformed), std::runtime_error);
	malformed = json;
	malformed["arbitraryIdentity"].String() = "stun";
	EXPECT_THROW(CreatureAbilityDescriptor::fromJson(malformed), std::runtime_error);
}

TEST(CreatureAbilitySerialization, DirectAndNestedOldWritersRejectBeforeAnyPrefix)
{
	auto bonus = marker();
	rejectsBeforePrefix(bonus);
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(0, std::vector<Bonus>{bonus});
	rejectsBeforePrefix(packet);
	BonusParametersOnCombatEvent nested;
	nested.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{std::make_shared<Bonus>(bonus), true});
	Bonus outer;
	outer.parameters = std::make_shared<BonusParameters>(nested);
	rejectsBeforePrefix(outer);
}

TEST(CreatureAbilitySerialization, CurrentCloneProfileIsDistinctAndAllPriorProfilesRemain)
{
	using Context = newHorizonsHeroes::SpecialtySerializationContext;
	const auto current = Context::fromHandler(Features{Version::NEW_HORIZONS_CONSECRATED_ARMS});
	const auto previous = Context::fromHandler(Features{Version::NEW_HORIZONS_SHARED_CAPTURED_RULES});
	EXPECT_NE(current, previous);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_CONSECRATED_ARMS, current).version(),
		Version::NEW_HORIZONS_CONSECRATED_ARMS);
	EXPECT_EQ(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_SHARED_CAPTURED_RULES, previous).version(),
		Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_CONSECRATED_ARMS, previous), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_SHARED_CAPTURED_RULES, current), std::invalid_argument);
}

TEST(CreatureAbilitySerialization, SameBonusMachineryFromDifferentOriginsNeverMergesOrRemovesEachOther)
{
	auto ability = marker();
	auto spell = ability;
	spell.abilityDescriptor = nativeAffliction(Native::CONCUSSIVE_STUN, Family::HERO_SPELL, PlayerColor(0));
	EXPECT_FALSE(stackEffectRefresh::matchesIdentity(ability, &spell));
	EXPECT_FALSE(battle::matchesStackEffectRemoval(ability, spell));
	EXPECT_TRUE(stackEffectRefresh::matchesIdentity(ability, &ability));
}
