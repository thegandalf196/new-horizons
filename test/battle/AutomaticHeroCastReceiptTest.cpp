/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/AutomaticHeroCast.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <gtest/gtest.h>
#include <stdexcept>

TEST(AutomaticHeroCastReceipt, DefaultProvenancePreservesOrdinaryEnvelope)
{
	const spells::AutomaticHeroCastReceipt receipt;
	EXPECT_FALSE(receipt.automatic());
	EXPECT_NO_THROW(receipt.validateEnvelope(false, false, 7, BattleSide::NONE, 0));
}

TEST(AutomaticHeroCastReceipt, MandateOrdinalIsBoundedAndRequiresGenuineFreeHeroCast)
{
	for(uint8_t ordinal = 1; ordinal <= 5; ++ordinal)
	{
		const spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, ordinal,
			HeroCommand::CHARGE, 1, 0};
		EXPECT_TRUE(receipt.automatic());
		EXPECT_NO_THROW(receipt.validateEnvelope(true, true, -1, BattleSide::ATTACKER, 0));
		EXPECT_THROW(receipt.validateEnvelope(false, true, -1, BattleSide::ATTACKER, 0), std::runtime_error);
		EXPECT_THROW(receipt.validateEnvelope(true, false, -1, BattleSide::ATTACKER, 0), std::runtime_error);
		EXPECT_THROW(receipt.validateEnvelope(true, true, 3, BattleSide::ATTACKER, 0), std::runtime_error);
		EXPECT_THROW(receipt.validateEnvelope(true, true, -1, BattleSide::NONE, 0), std::runtime_error);
		EXPECT_THROW(receipt.validateEnvelope(true, true, -1, BattleSide::ATTACKER, 1), std::runtime_error);
	}
	EXPECT_THROW((spells::AutomaticHeroCastReceipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, 0}).validateShape(), std::runtime_error);
	EXPECT_THROW((spells::AutomaticHeroCastReceipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, 6}).validateShape(), std::runtime_error);
}

TEST(AutomaticHeroCastReceipt, FuryNeverConsumesMandateBudgetOrAcceptsUnknownKinds)
{
	EXPECT_NO_THROW((spells::AutomaticHeroCastReceipt{spells::AutomaticHeroCastKind::DIVINE_FURY, 0}).validateShape());
	EXPECT_THROW((spells::AutomaticHeroCastReceipt{spells::AutomaticHeroCastKind::DIVINE_FURY, 1}).validateShape(), std::runtime_error);
	EXPECT_THROW((spells::AutomaticHeroCastReceipt{spells::AutomaticHeroCastKind::NONE, 1}).validateShape(), std::runtime_error);
	EXPECT_THROW((spells::AutomaticHeroCastReceipt{static_cast<spells::AutomaticHeroCastKind>(255), 0}).validateShape(), std::runtime_error);
}

TEST(AutomaticHeroCastReceipt, InspirationRequiresMandateOrderAndBoundedScheduledDuration)
{
	spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, 1,
		HeroCommand::VANGUARD, 2, 3};
	EXPECT_NO_THROW(receipt.validateShape());
	receipt.inspiringDuration = 4;
	EXPECT_THROW(receipt.validateShape(), std::runtime_error);
	receipt.inspiringDuration = 1;
	receipt.kind = spells::AutomaticHeroCastKind::DIVINE_FURY;
	receipt.mandateUseOrdinal = 0;
	EXPECT_THROW(receipt.validateShape(), std::runtime_error);
}

TEST(AutomaticHeroCastReceipt, PassiveOrderSnapshotDoesNotReuseHistoricalPerkMagnitudeFields)
{
	HeroOrderState order;
	order.command = HeroCommand::CHARGE;
	order.issuedRound = 1;
	order.passiveDivineMandate = newHorizonsPassiveDivineMandate::Policy{3, 0};
	EXPECT_NO_THROW(order.validateShape());
	EXPECT_EQ(order.divineMandateEfficiencyBonusPercent(), 15);
	EXPECT_EQ(order.sacredCommandEfficiencyBonusPercent, 0);
	EXPECT_EQ(order.knightlySequenceEfficiencyBonusPercent, 0);
	order.sacredCommandEfficiencyBonusPercent = 10;
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order.passiveDivineMandate.reset();
	EXPECT_NO_THROW(order.validateShape());
	EXPECT_EQ(order.divineMandateEfficiencyBonusPercent(), 10);
}

namespace
{
struct AutomaticOldWriterProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	int fields = 0;
	bool hasFeature(Version feature) const { return feature != Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS; }
	template<typename T> AutomaticOldWriterProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached old ordinary payload");
	}
};
}

TEST(AutomaticHeroCastReceipt, OldWriterRejectsAutomaticKindBeforeOrdinaryPacketPrefix)
{
	BattleSpellCast packet;
	packet.battleID = BattleID(1);
	packet.side = BattleSide::ATTACKER;
	packet.spellID = SpellID::BLESS;
	packet.automaticHeroCast = {spells::AutomaticHeroCastKind::DIVINE_MANDATE, 1,
		HeroCommand::CHARGE, 1, 0};
	AutomaticOldWriterProbe old;
	EXPECT_THROW(packet.serialize(old), std::runtime_error);
	EXPECT_EQ(old.fields, 0);
}
