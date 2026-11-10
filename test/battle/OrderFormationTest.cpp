/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OrderFormation.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/json/JsonUtils.h"
#include <limits>

namespace
{
JsonNode rules()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsCombat"))["combat"]["heroCommands"];
}
}

TEST(OrderFormation, CapturedPrototypeUsesClampedAttackDefenseAndRealCommand)
{
	const auto captured = rules();
	ASSERT_NO_THROW(heroCommands::validateRules(captured));
	EXPECT_EQ(orderFormation::rating(captured, 101, 100, 6), 160);
	EXPECT_EQ(orderFormation::rating(captured, -100, 100, 6), 110);
	EXPECT_EQ(orderFormation::rating(captured, -100, -100, -6), 0);
	EXPECT_THROW(orderFormation::rating(captured, 1, 1, std::numeric_limits<int64_t>::max()), std::overflow_error);
}

TEST(OrderFormation, SecondWindEfficiencyNeverMultipliesItsFixedFiftyPercent)
{
	const auto captured = rules();
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 0, 0, 0, 200), 50);
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 100, 100, 0, 100), 60);
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 200, 200, 0, 150), 80);
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 10000, 10000, 100, 200), 100);
}

TEST(OrderFormation, CrownAndAltarPreservesOnlyItsDerivedSecondWindMultiplier)
{
	const auto captured = rules();
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 0, 0, 0, 200, true), 50);
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 100, 100, 0, 100), 60);
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 100, 100, 0, 100, true), 62);
	EXPECT_EQ(orderFormation::secondWindPercent(captured, 10000, 10000, 100, 200, true), 100);
}

TEST(OrderFormation, VanguardDurationHasOneRoundBaseAndEfficiencyOnDerivedRatingOnly)
{
	const auto captured = rules();
	for(const auto [rating, expected] : {std::pair{149, 1}, {150, 2}, {299, 2}, {300, 3}, {900, 3}})
	{
		const auto snapshot = orderFormation::snapshot(captured, HeroCommand::VANGUARD, 4,
			rating, rating, 0, 100, 100, 100);
		EXPECT_EQ(snapshot.expiresAfterRound, 4 + expected - 1);
	}
	const auto ordinary = orderFormation::snapshot(captured, HeroCommand::VANGUARD, 4, 100, 100, 0, 100, 100, 100);
	const auto efficient = orderFormation::snapshot(captured, HeroCommand::VANGUARD, 4, 100, 100, 0, 100, 100, 150);
	EXPECT_EQ(ordinary.expiresAfterRound, 4);
	EXPECT_EQ(efficient.expiresAfterRound, 5);
	EXPECT_EQ(ordinary.vanguardStatBonus, efficient.vanguardStatBonus);
	EXPECT_EQ(ordinary.vanguardInitiativePercent, efficient.vanguardInitiativePercent);
}

TEST(OrderFormation, FeintUsesRatingAndExistingFinalCapAfterCombinedArms)
{
	const auto captured = rules();
	auto snapshot = orderFormation::snapshot(captured, HeroCommand::FEINT, 1, 101, 100, 6, 100, 100, 150);
	EXPECT_EQ(snapshot.feintReductionPercent, 39);
	HeroOrderState order;
	order.command = HeroCommand::FEINT;
	order.issuedRound = 1;
	order.revision = snapshot;
	order.revision->feintCompanionPercent = 5;
	heroCommands::finalizeRevisedOrderSnapshot(order, JsonNode());
	EXPECT_EQ(heroCommands::revisedFeintReductionFor(order, 0), 44);
	order.revision = orderFormation::snapshot(captured, HeroCommand::FEINT, 1, 1000, 1000, 6, 100, 100, 200);
	order.revision->feintCompanionPercent = 5;
	heroCommands::finalizeRevisedOrderSnapshot(order, JsonNode());
	EXPECT_EQ(heroCommands::revisedFeintReductionFor(order, 0), 60);
}

TEST(OrderFormation, MalformedOrMixedCapturedDependenciesAreRejected)
{
	const auto valid = rules();
	auto malformed = valid;
	malformed["formationRating"]["attributeDivisor"].Integer() = 0;
	EXPECT_THROW(heroCommands::validateRules(malformed), std::runtime_error);
	malformed = valid;
	malformed["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"]["leadership"].Float() = .015;
	EXPECT_THROW(heroCommands::validateRules(malformed), std::runtime_error);
	malformed = valid;
	malformed["commandResourcePolicyVersion"].Integer() = 1;
	EXPECT_THROW(heroCommands::validateRules(malformed), std::runtime_error);
	malformed = valid;
	malformed["formationRating"]["version"].Integer() = 2;
	EXPECT_THROW(heroCommands::validateRules(malformed), std::runtime_error);
}

TEST(OrderFormation, NativeSchemaValidatesCoreScopedAndModScopedCurrentCaptures)
{
	for(const char * scope : {"core", "new-horizons"})
	{
		auto captured = rules();
		captured.setModScope(scope);
		ASSERT_TRUE(JsonUtils::validate(captured, "vcmi:newHorizonsCombatV4", "Captured Warfare profile"));
		JsonNode settings;
		settings["combat"]["heroCommands"] = captured;
		EXPECT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "Captured Warfare settings"));
		captured["commandResourcePolicyVersion"].Integer() = 1;
		EXPECT_FALSE(JsonUtils::validate(captured, "vcmi:newHorizonsCombatV4", "Mixed Warfare/resource profile"));
	}
}

TEST(OrderFormation, FeintRetainsExistingWholePercentRoundingAtHalfBoundary)
{
	const auto captured = rules();
	const auto below = orderFormation::snapshot(captured, HeroCommand::FEINT, 1, 28, 0, 0, 100, 100, 100);
	const auto half = orderFormation::snapshot(captured, HeroCommand::FEINT, 1, 30, 0, 0, 100, 100, 100);
	EXPECT_EQ(below.feintReductionPercent, 16); // lround(15 + .10 * 14).
	EXPECT_EQ(half.feintReductionPercent, 17); // lround(15 + .10 * 15).
	const auto efficient = orderFormation::snapshot(captured, HeroCommand::FEINT, 1, 20, 0, 0, 100, 100, 150);
	EXPECT_EQ(efficient.feintReductionPercent, 17); // Efficiency scales the derived term only.
}

TEST(OrderFormation, VanguardPotencyKeepsMinimumEfficientAttributesWhenCommandChanges)
{
	const auto captured = rules();
	const auto low = orderFormation::snapshot(captured, HeroCommand::VANGUARD, 1, 150, 0, 1, 100, 100, 100);
	const auto high = orderFormation::snapshot(captured, HeroCommand::VANGUARD, 1, 150, 0, 30, 100, 100, 100);
	EXPECT_EQ(low.vanguardStatBonus, 1);
	EXPECT_EQ(low.vanguardInitiativePercent, 5);
	EXPECT_EQ(high.vanguardStatBonus, low.vanguardStatBonus);
	EXPECT_EQ(high.vanguardInitiativePercent, low.vanguardInitiativePercent);
	EXPECT_GT(high.expiresAfterRound, low.expiresAfterRound);
}
