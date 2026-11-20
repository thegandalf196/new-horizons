/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/battle/MetamagicDoubleCast.h"
#include "../../lib/battle/MetamagicDoubleCastState.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using Ledger = HeroActionAllowanceState;
constexpr auto OLDER = ESerializationVersion::NEW_HORIZONS_CONSECRATED_ARMS;

MetamagicDoubleCastState openOffer()
{
	MetamagicDoubleCastState state;
	state.usesSpent = 2;
	state.usesRegained = 1;
	state.completedDoubleCasts = 2;
	state.offerGrantId = 5;
	state.offerRound = 3;
	state.offerActivationSerial = 9;
	state.firstSpell = SpellID::HASTE;
	state.firstSpellAffectedUnits = {1, 4};
	return state;
}

Ledger ledgerWithOffer()
{
	Ledger ledger;
	ledger.resetForRound(3);
	ledger.grantAllowance(Ledger::AllowanceKind::SPELL, Ledger::GrantSource::METAMAGIC_DOUBLE_CAST, 3);
	return ledger;
}
}

TEST(MetamagicDoubleCastState, CurrentFormatRoundTripsCountersAndOpenOffer)
{
	const auto state = openOffer();
	CMemorySerializer bytes;
	bytes.oser & state;
	MetamagicDoubleCastState decoded;
	bytes.iser & decoded;
	EXPECT_EQ(decoded, state);
}

TEST(MetamagicDoubleCastState, OlderFormatRefusesLiveStateAndLoadsDefaults)
{
	CMemorySerializer refused;
	refused.oser.version = OLDER;
	EXPECT_THROW(refused.oser & openOffer(), std::runtime_error);

	CMemorySerializer older;
	older.oser.version = OLDER;
	older.iser.version = OLDER;
	older.oser & MetamagicDoubleCastState{};
	older.oser & int32_t{1234};
	auto loaded = openOffer();
	older.iser & loaded;
	int32_t sentinel = 0;
	older.iser & sentinel;
	EXPECT_TRUE(loaded.defaultState());
	EXPECT_EQ(sentinel, 1234) << "An older record carries no Double Cast bytes";
}

TEST(MetamagicDoubleCastState, ValidationRejectsInconsistentRecords)
{
	auto regained = MetamagicDoubleCastState{};
	regained.usesRegained = 1;
	EXPECT_THROW(regained.validate(), std::runtime_error);
	auto completed = MetamagicDoubleCastState{};
	completed.completedDoubleCasts = 1;
	EXPECT_THROW(completed.validate(), std::runtime_error);
	auto closed = MetamagicDoubleCastState{};
	closed.firstSpell = SpellID::HASTE;
	EXPECT_THROW(closed.validate(), std::runtime_error);
	auto unsorted = openOffer();
	unsorted.firstSpellAffectedUnits = {4, 1};
	EXPECT_THROW(unsorted.validate(), std::runtime_error);
	EXPECT_NO_THROW(openOffer().validate());
}

TEST(MetamagicDoubleCastState, FollowUpGrantIsARoundSpellAllowanceThatCannotPayOrders)
{
	auto ledger = ledgerWithOffer();
	EXPECT_TRUE(metamagicDoubleCast::offerGranted(ledger));
	ledger.grants.erase(std::remove_if(ledger.grants.begin(), ledger.grants.end(), [](const Ledger::Grant & grant)
	{
		return grant.source == Ledger::GrantSource::ROUND;
	}), ledger.grants.end());
	const auto spell = ledger.eligibleAllowance(Ledger::ActionKind::SPELL, 3);
	ASSERT_TRUE(spell);
	EXPECT_EQ(spell->source, Ledger::GrantSource::METAMAGIC_DOUBLE_CAST);
	EXPECT_FALSE(ledger.eligibleAllowance(Ledger::ActionKind::ORDER, 3));
	ledger.resetForRound(4);
	EXPECT_FALSE(metamagicDoubleCast::offerGranted(ledger));
	auto twice = ledgerWithOffer();
	EXPECT_THROW(twice.grantAllowance(Ledger::AllowanceKind::SPELL, Ledger::GrantSource::METAMAGIC_DOUBLE_CAST, 3),
		std::runtime_error);
	EXPECT_THROW(twice.grantAllowance(Ledger::AllowanceKind::SPELL, Ledger::GrantSource::METAMAGIC_DOUBLE_CAST, 4),
		std::runtime_error);
}

TEST(MetamagicDoubleCastState, FollowUpGrantSerializesOnlyInItsFormat)
{
	const auto ledger = ledgerWithOffer();
	CMemorySerializer current;
	current.oser & ledger;
	Ledger decoded;
	current.iser & decoded;
	EXPECT_EQ(decoded, ledger);

	CMemorySerializer older;
	older.oser.version = OLDER;
	EXPECT_THROW(older.oser & ledger, std::runtime_error);
}
