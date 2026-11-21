/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/battle/ShieldPresentation.h"
#include <limits>

namespace
{
battle::ShieldSnapshot mixedShield(int64_t guardian, int64_t redemption)
{
	battle::ShieldSnapshot result;
	result.pools[0] = {battle::ShieldSource::GUARDIAN_SPIRIT, guardian, 100, true};
	result.pools[1] = {battle::ShieldSource::VICARIOUS_REDEMPTION, redemption, 200, false};
	return result;
}
}

TEST(ShieldPresentation, TwoPixelWhiteReservoirLeavesTwoClearRowsAboveExistingHealthBar)
{
	const auto bar = shieldPresentation::bar(mixedShield(100, 200), Point(20, 40), 30);
	EXPECT_EQ(bar.bounds.x, 20);
	EXPECT_EQ(bar.bounds.y, 33);
	EXPECT_EQ(bar.bounds.w, 30);
	EXPECT_EQ(bar.bounds.h, 2);
	EXPECT_EQ(bar.fillWidth, 30);
	// Last shield pixel = y-6; the first existing HP pixel = y-3.
	EXPECT_EQ((40 - 3) - (bar.bounds.y + bar.bounds.h), 2);
}

TEST(ShieldPresentation, DamageAndPoolBreakDoNotRefillOtherReservoir)
{
	EXPECT_EQ(shieldPresentation::bar(mixedShield(100, 200), {}, 30).fillWidth, 30);
	EXPECT_EQ(shieldPresentation::bar(mixedShield(50, 200), {}, 30).fillWidth, 25);
	EXPECT_EQ(shieldPresentation::bar(mixedShield(0, 200), {}, 30).fillWidth, 20);
	EXPECT_EQ(shieldPresentation::bar(mixedShield(0, 100), {}, 30).fillWidth, 10);
	EXPECT_EQ(shieldPresentation::bar(mixedShield(0, 0), {}, 30).fillWidth, 0);
}

TEST(ShieldPresentation, AbsentExpiredAndZeroWidthShieldHasNoPixels)
{
	EXPECT_EQ(shieldPresentation::bar({}, {}, 30).fillWidth, 0);
	EXPECT_TRUE(shieldPresentation::tooltip({}).empty());
	EXPECT_EQ(shieldPresentation::bar(mixedShield(100, 200), {}, 0).fillWidth, 0);
	EXPECT_EQ(shieldPresentation::bar(mixedShield(100, 200), {}, -1).fillWidth, 0);
	EXPECT_EQ(shieldPresentation::bar(mixedShield(1, 0), {}, 30).fillWidth, 1);
}

TEST(ShieldPresentation, SummingLargePoolsDoesNotOverflow)
{
	auto snapshot = mixedShield(0, 0);
	for(auto & pool : snapshot.pools)
		pool.remaining = pool.capacity = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(shieldPresentation::bar(snapshot, {}, 30).fillWidth, 30);
}

TEST(ShieldPresentation, TooltipSeparatesExactSourceValuesAndCoverage)
{
	const auto text = shieldPresentation::tooltip(mixedShield(37, 91));
	EXPECT_NE(text.find("Guardian Spirit\nShield HP remaining: 37 / 100"), std::string::npos);
	EXPECT_NE(text.find("physical creature damage only"), std::string::npos);
	EXPECT_NE(text.find("Vicarious Redemption\nShield HP remaining: 91 / 200"), std::string::npos);
	EXPECT_NE(text.find("Absorbs physical and magical damage"), std::string::npos);
	EXPECT_NE(text.find("not universal protection"), std::string::npos);
}

TEST(ShieldPresentation, CopiedPreHitSnapshotRemainsStableAcrossDamageAndExpiry)
{
	auto live = mixedShield(100, 200);
	const auto locked = live;
	live.pools[0].remaining = 0;
	live.pools[1].remaining = 50;
	EXPECT_FALSE(shieldPresentation::equal(locked, live));
	EXPECT_EQ(shieldPresentation::bar(locked, {}, 30).fillWidth, 30);
	EXPECT_EQ(shieldPresentation::bar(live, {}, 30).fillWidth, 5);
	live = {};
	EXPECT_EQ(shieldPresentation::bar(locked, {}, 30).fillWidth, 30);
	EXPECT_EQ(shieldPresentation::bar(live, {}, 30).fillWidth, 0);
}

TEST(ShieldPresentation, SingleSourceDoesNotAdvertiseOtherSourcesProtection)
{
	auto guardian = mixedShield(37, 0);
	guardian.pools[1] = {};
	EXPECT_EQ(shieldPresentation::bar(guardian, {}, 30).fillWidth, 11);
	const auto guardianText = shieldPresentation::tooltip(guardian);
	EXPECT_EQ(guardianText.find("Vicarious Redemption"), std::string::npos);
	EXPECT_NE(guardianText.find("physical creature damage only"), std::string::npos);
	auto redemption = mixedShield(0, 100);
	redemption.pools[0] = {};
	EXPECT_EQ(shieldPresentation::bar(redemption, {}, 30).fillWidth, 15);
	const auto redemptionText = shieldPresentation::tooltip(redemption);
	EXPECT_EQ(redemptionText.find("Guardian Spirit"), std::string::npos);
	EXPECT_NE(redemptionText.find("Absorbs physical and magical damage"), std::string::npos);
}

TEST(ShieldPresentation, ArcaneWardNamesItsSourceCoverageAndExpiry)
{
	battle::ShieldSnapshot snapshot;
	snapshot.pools[0] = {battle::ShieldSource::ARCANE_WARD, 12, 30, false};
	const auto text = shieldPresentation::tooltip(snapshot);
	EXPECT_NE(text.find("Arcane Ward\nShield HP remaining: 12 / 30"), std::string::npos);
	EXPECT_NE(text.find("Absorbs physical and magical damage"), std::string::npos);
	EXPECT_NE(text.find("start of the next round"), std::string::npos);
	EXPECT_EQ(shieldPresentation::tooltip(mixedShield(37, 91)).find("next round"), std::string::npos);
}
