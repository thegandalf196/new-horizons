/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "../../lib/mapObjects/EstatesArtifactQuote.h"
#include "../../lib/mapObjects/IMarket.h"
#include "../../lib/entities/artifact/CArtHandler.h"
#include "../../lib/GameLibrary.h"

namespace
{
class QuoteMarket : public IMarket
{
	int count;
public:
	explicit QuoteMarket(int count) : IMarket(nullptr), count(count) {}
	ObjectInstanceID getObjInstanceID() const override { return ObjectInstanceID::NONE; }
	int getMarketEfficiency() const override { return count; }
	std::set<EMarketMode> availableModes() const override
	{
		return {EMarketMode::RESOURCE_ARTIFACT, EMarketMode::ARTIFACT_RESOURCE};
	}
};
}

TEST(EstatesHoarderQuote, HalfGoldRoundsUpAndUnusedAllowanceSurvivesZeroPrice)
{
	using newHorizonsEstates::hoarderGoldCost;
	EXPECT_EQ(hoarderGoldCost(1, true, -1, 0), 1);
	EXPECT_EQ(hoarderGoldCost(3, true, -1, 0), 2);
	EXPECT_EQ(hoarderGoldCost(500, true, -1, 0), 250);
	EXPECT_EQ(hoarderGoldCost(0, true, -1, 0), 0);
	EXPECT_EQ(hoarderGoldCost(500, true, 0, 0), 500);
	EXPECT_EQ(hoarderGoldCost(500, true, 0, 1), 250);
	EXPECT_EQ(hoarderGoldCost(500, false, -1, 0), 500);
	EXPECT_EQ(hoarderGoldCost(500, true, 4, 3), 500);
	EXPECT_THROW(hoarderGoldCost(-1, true, -1, 0), std::runtime_error);
	EXPECT_THROW(hoarderGoldCost(1, true, -2, 0), std::runtime_error);
	EXPECT_THROW(newHorizonsEstates::validateHoarderReceipt(0, false), std::runtime_error);
}

TEST(EstatesHoarderQuote, StandardMerchantBuybackCannotProfitAfterHoarderDiscount)
{
	std::vector<ArtifactID> artifactIds;
	LIBRARY->artifacts()->forEach([&](const Artifact * artifact, bool &)
	{
		artifactIds.push_back(artifact->getId());
	});
	size_t checked = 0;
	for(int marketplaces = 0; marketplaces <= 20; ++marketplaces)
	{
		QuoteMarket market(marketplaces);
		for(const auto id : artifactIds)
		{
			const auto * artifact = id.toArtifact();
			if(!artifact || !artifact->isTradable() || !artifact->getConstituents().empty())
				continue;
			int buy, quantity, sold, resale;
			ASSERT_TRUE(market.getOffer(GameResID::GOLD, id, buy, quantity, EMarketMode::RESOURCE_ARTIFACT));
			ASSERT_TRUE(market.getOffer(id, GameResID::GOLD, sold, resale, EMarketMode::ARTIFACT_RESOURCE));
			EXPECT_GE(newHorizonsEstates::hoarderGoldCost(buy, true, -1, 0), resale)
				<< artifact->getJsonKey() << " at " << marketplaces << " Marketplaces";
			++checked;
		}
	}
	EXPECT_GT(checked, 0u);
}
