/*
 * NewHorizonsMarketSelectionAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include <gtest/gtest.h>
#include <utility>

#include "AI/Nullkiller2/Engine/ResourceTrader.h"

namespace
{

class SelectionTestMarket final : public IMarket
{
public:
	SelectionTestMarket(int efficiency, std::set<EMarketMode> modes)
		: IMarket(nullptr)
		, efficiency(efficiency)
		, modes(std::move(modes))
	{}

	ObjectInstanceID getObjInstanceID() const override
	{
		return ObjectInstanceID::NONE;
	}

	int getMarketEfficiency() const override
	{
		return efficiency;
	}

	std::set<EMarketMode> availableModes() const override
	{
		return modes;
	}

private:
	int efficiency;
	std::set<EMarketMode> modes;
};

const std::set<EMarketMode> resourceExchangeMode{EMarketMode::RESOURCE_RESOURCE};

} // namespace

TEST(Nullkiller2_ResourceTraderMarketSelection, returnsNoMarketForAnEmptyOrIneligibleList)
{
	SelectionTestMarket nonResourceMarket(10, {EMarketMode::CREATURE_RESOURCE});
	const std::vector<const IMarket *> markets{nullptr, &nonResourceMarket};

	EXPECT_EQ(NK2AI::ResourceTrader::selectBestResourceMarket({}), nullptr);
	EXPECT_EQ(NK2AI::ResourceTrader::selectBestResourceMarket(markets), nullptr);
}

TEST(Nullkiller2_ResourceTraderMarketSelection, selectsTheOnlyResourceExchangeMarket)
{
	SelectionTestMarket market(2, resourceExchangeMode);
	const std::vector<const IMarket *> markets{&market};

	EXPECT_EQ(NK2AI::ResourceTrader::selectBestResourceMarket(markets), &market);
}

TEST(Nullkiller2_ResourceTraderMarketSelection, selectsALaterMarketWithBetterEffectiveness)
{
	SelectionTestMarket first(1, resourceExchangeMode);
	SelectionTestMarket laterBest(4, resourceExchangeMode);
	const std::vector<const IMarket *> markets{&first, &laterBest};

	EXPECT_GT(laterBest.getMarketExchangeEffectiveness(), first.getMarketExchangeEffectiveness());
	EXPECT_EQ(NK2AI::ResourceTrader::selectBestResourceMarket(markets), &laterBest);
}

TEST(Nullkiller2_ResourceTraderMarketSelection, keepsTheFirstMarketWhenEffectivenessIsCapped)
{
	SelectionTestMarket firstBelowCap(8, resourceExchangeMode);
	SelectionTestMarket firstAtCap(9, resourceExchangeMode);
	SelectionTestMarket laterAtCap(20, resourceExchangeMode);
	const std::vector<const IMarket *> markets{&firstBelowCap, &firstAtCap, &laterAtCap};

	ASSERT_EQ(firstAtCap.getMarketExchangeEffectiveness(), 0.5);
	ASSERT_EQ(laterAtCap.getMarketExchangeEffectiveness(), firstAtCap.getMarketExchangeEffectiveness());
	EXPECT_EQ(NK2AI::ResourceTrader::selectBestResourceMarket(markets), &firstAtCap);
}
