/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/mapObjects/EstatesRecruitmentQuote.h"

#include <limits>

namespace
{
using namespace newHorizonsEstates;
ResourceSet price(int gold, int gems = 0)
{
	ResourceSet result;
	result[EGameResID::GOLD] = gold;
	result[EGameResID::GEMS] = gems;
	return result;
}
}

TEST(EstatesRecruitmentQuote, SeventyFiveGoldCostsSixtyEightThenSixtySeven)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto first = quoteRecruitment(price(75), 1, {}, true);
	EXPECT_EQ(first.cost[EGameResID::GOLD], 68);
	const auto second = quoteRecruitment(price(75), 1, first.after, true);
	EXPECT_EQ(second.cost[EGameResID::GOLD], 67);
	EXPECT_EQ(second.after[6], 150u);
	EXPECT_EQ(first.before, ConscriptionLedger{});
}

TEST(EstatesRecruitmentQuote, EveryResourceHasPurchasePartitionInvariance)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	ResourceSet unit;
	for(size_t resource = 0; resource < 7; ++resource)
		unit[resource] = static_cast<int>(resource * 17 + 1);
	const auto combined = quoteRecruitment(unit, 137, {}, true);
	RecruitmentQuoteCursor cursor(ConscriptionLedger{});
	ResourceSet separately;
	for(int i = 0; i < 137; ++i)
	{
		const auto next = quoteRecruitment(unit, 1, cursor.current(), true);
		separately += next.cost;
		cursor.accept(next);
	}
	EXPECT_EQ(combined.cost, separately);
	EXPECT_EQ(combined.after, cursor.current());
}

TEST(EstatesRecruitmentQuote, TenRareResourcesCostNineAndOnlyEligiblePurchasesAccumulate)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	ConscriptionLedger ledger{};
	ResourceSet total;
	for(int i = 0; i < 10; ++i)
	{
		const auto quote = quoteRecruitment(price(75, 1), 1, ledger, true);
		total += quote.cost;
		ledger = quote.after;
	}
	EXPECT_EQ(total, price(675, 9));
	const auto ordinary = quoteRecruitment(price(75, 1), 9, ledger, false);
	EXPECT_EQ(ordinary.cost, price(675, 9));
	EXPECT_EQ(ordinary.before, ordinary.after);
}

TEST(EstatesRecruitmentQuote, AffordabilityUsesActualRemainderIncludingFreeTenthResource)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	ConscriptionLedger ledger{};
	EXPECT_EQ(affordableRecruitment(price(75), price(67), 10, ledger, true), 0);
	ledger[6] = 75;
	EXPECT_EQ(affordableRecruitment(price(75), price(67), 10, ledger, true), 1);
	EXPECT_EQ(affordableRecruitment(price(75), price(66), 10, ledger, true), 0);
	ledger[5] = 9;
	EXPECT_EQ(affordableRecruitment(price(0, 1), {}, 20, ledger, true), 1);
	EXPECT_EQ(affordableRecruitment({}, {}, 123, ledger, true), 123);
}

TEST(EstatesRecruitmentQuote, InvalidAndUnrepresentableQuotesDoNotMutateLedger)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	ConscriptionLedger ledger{};
	ledger[6] = std::numeric_limits<int64_t>::max();
	const auto original = ledger;
	EXPECT_THROW(quoteRecruitment(price(1), 1, ledger, true), std::overflow_error);
	EXPECT_EQ(ledger, original);
	EXPECT_THROW(quoteRecruitment(price(-1), 1, {}, true), std::runtime_error);
	EXPECT_THROW(quoteRecruitment(price(1), -1, {}, true), std::runtime_error);
	EXPECT_THROW(quoteRecruitment(price(std::numeric_limits<int32_t>::max()), 2, {}, false), std::overflow_error);
	EXPECT_EQ(affordableRecruitment(price(1), price(10), 10, ledger, true), 0);
}

TEST(EstatesRecruitmentQuote, CancelledAndStaleProjectionDoesNotAdvanceCursor)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	RecruitmentQuoteCursor cursor(ConscriptionLedger{});
	const auto quoted = quoteRecruitment(price(75), 1, cursor.current(), true);
	EXPECT_EQ(cursor.current(), ConscriptionLedger{}); // Inspection/cancellation is read-only.
	cursor.accept(quoted);
	const auto committed = cursor.current();
	EXPECT_THROW(cursor.accept(quoted), std::runtime_error);
	EXPECT_EQ(cursor.current(), committed);
}

TEST(EstatesKingdomState, AssignmentsAreOneToOneAndHistoricalWritesFailBeforePayload)
{
	KingdomState state;
	EXPECT_NO_THROW(state.validateSerialization(false));
	state.stewards.emplace(ObjectInstanceID(1), ObjectInstanceID(2));
	EXPECT_NO_THROW(state.validate());
	EXPECT_THROW(state.validateSerialization(false), std::runtime_error);
	state.stewards.emplace(ObjectInstanceID(3), ObjectInstanceID(2));
	EXPECT_THROW(state.validate(), std::runtime_error);
	state.stewards.clear();
	state.recruitmentBaseCost[0] = 1;
	EXPECT_THROW(state.validateSerialization(false), std::runtime_error);
}
