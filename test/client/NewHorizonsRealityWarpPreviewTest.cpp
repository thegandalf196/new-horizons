/*
 * NewHorizonsRealityWarpPreviewTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "../../client/battle/NewHorizonsRealityWarpPreview.h"

namespace
{
newHorizonsRealityWarp::PreparedExchange prepared()
{
	newHorizonsRealityWarp::PreparedExchange result;
	result.exchange.emplace();
	result.exchange->endpoints[0].id = 10;
	result.exchange->endpoints[1].id = 20;
	Bonus effect;
	effect.type = BonusType::STACKS_SPEED;
	effect.val = 3;
	effect.turnsRemain = 2;
	effect.duration = BonusDuration::N_TURNS;
	effect.source = BonusSource::SPELL_EFFECT;
	effect.spellCasterOwner = PlayerColor(0);
	result.exchange->endpoints[0].expected.effects.push_back(effect);
	result.exchange->endpoints[1].replacement.effects.push_back(effect);
	newHorizonsRealityWarp::BundlePreview row;
	row.source = 10;
	row.destination = 20;
	row.bundle.spell = SpellID(SpellID::HASTE);
	row.bundle.bonuses.push_back(effect);
	row.reason = newHorizonsRealityWarp::StayReason::MOVED;
	result.previews.push_back(row);
	return result;
}

std::string text(const newHorizonsRealityWarp::PreparedExchange & exchange)
{
	return newHorizonsRealityWarpPreview::text(exchange,
		[](uint32_t id) { return "Stack " + std::to_string(id); },
		[](SpellID) { return "Haste"; });
}
}

TEST(NewHorizonsRealityWarpPreviewTest, CompletePreviewShowsMovingAndEveryStayingReason)
{
	auto exchange = prepared();
	using Reason = newHorizonsRealityWarp::StayReason;
	for(const auto reason : {Reason::EXCLUDED_EFFECT, Reason::UNKNOWN_SOURCE, Reason::MISSING_CAPTURE,
		Reason::UNSUPPORTED_CONDITION, Reason::ILLEGAL_RECIPIENT, Reason::INVALID_SIDECAR,
		Reason::SIDECAR_CONFLICT, Reason::INVALID_LINK})
	{
		auto row = exchange.previews.front();
		row.reason = reason;
		exchange.previews.push_back(row);
	}
	const auto preview = text(exchange);
	EXPECT_NE(preview.find("Stack 10 -> Stack 20: Haste [MOVES]"), std::string::npos);
	EXPECT_NE(preview.find("strength 3; remaining turns 2"), std::string::npos);
	EXPECT_NE(preview.find("original caster 0"), std::string::npos);
	for(size_t index = 1; index < exchange.previews.size(); ++index)
		EXPECT_NE(preview.find(newHorizonsRealityWarpPreview::stayReason(exchange.previews[index].reason)), std::string::npos);
}

TEST(NewHorizonsRealityWarpPreviewTest, ConfirmationComparesFullExpectedAndReplacementState)
{
	const auto original = prepared();
	ASSERT_TRUE(newHorizonsRealityWarpPreview::sameExchange(original, original));
	auto changed = original;
	changed.exchange->endpoints[0].expected.effects.front().turnsRemain = 1;
	EXPECT_FALSE(newHorizonsRealityWarpPreview::sameExchange(original, changed));
	changed = original;
	changed.exchange->endpoints[1].replacement.effects.front().val = 4;
	EXPECT_FALSE(newHorizonsRealityWarpPreview::sameExchange(original, changed));
	changed = original;
	changed.exchange->endpoints[0].expected.sidecars.regenerationPendingMicroHealth = 1;
	EXPECT_FALSE(newHorizonsRealityWarpPreview::sameExchange(original, changed));
	changed = original;
	changed.exchange->endpoints[0].expected.recipientHealth["health"].Integer() = 1;
	EXPECT_FALSE(newHorizonsRealityWarpPreview::sameExchange(original, changed));
	changed = original;
	changed.exchange->endpoints[1].id = 21;
	EXPECT_FALSE(newHorizonsRealityWarpPreview::sameExchange(original, changed));
	changed.exchange.reset();
	EXPECT_FALSE(newHorizonsRealityWarpPreview::sameExchange(original, changed));
}

TEST(NewHorizonsRealityWarpPreviewTest, CapturedSidecarStrengthAndProvenanceRemainVisible)
{
	auto exchange = prepared();
	auto & bundle = exchange.previews.front().bundle;
	bundle.guardianSpirit = newHorizonsRealityWarp::GuardianSpiritPayload{123, 2};
	bundle.regeneration = newHorizonsRealityWarp::RegenerationPayload{50000, 77};
	bundle.capacityRegeneration = newHorizonsRealityWarp::CapacityRegenerationPayload{4};
	bundle.confusion = newHorizonsRealityWarp::ConfusionPayload{PlayerColor(1), true};
	const auto preview = text(exchange);
	EXPECT_NE(preview.find("Guardian Spirit 123 HP; 2 rounds"), std::string::npos);
	EXPECT_NE(preview.find("Regeneration rate 50000; pending healing 77"), std::string::npos);
	EXPECT_NE(preview.find("Regeneration remainder 4"), std::string::npos);
	EXPECT_NE(preview.find("original caster 1; Confounder captured"), std::string::npos);
}

TEST(NewHorizonsRealityWarpPreviewTest, EmptyExchangeIsExplicitAndFormattingDoesNotMutateIt)
{
	auto exchange = prepared();
	exchange.previews.clear();
	const auto before = exchange;
	EXPECT_NE(text(exchange).find("no effect"), std::string::npos);
	EXPECT_TRUE(newHorizonsRealityWarpPreview::sameExchange(before, exchange));
	EXPECT_TRUE(exchange.previews.empty());
}
