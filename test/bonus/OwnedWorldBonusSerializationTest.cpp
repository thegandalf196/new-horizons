/*
 * OwnedWorldBonusSerializationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/rewardable/Configuration.h"
#include "../../lib/networkPacks/SetRewardableConfiguration.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto previous = static_cast<ESerializationVersion>(1144);
std::shared_ptr<Bonus> marker()
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION, BonusSource::OTHER, 20, BonusSourceID());
}
template<typename T> void expectOldWritable(T & source)
{
	CMemorySerializer wire;
	wire.oser.version = previous;
	EXPECT_NO_THROW(wire.oser & source);
}
template<typename T> void expectPrefixRejectedWithCurrentRoundtrip(T & source)
{
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & source);
	T restored;
	ASSERT_NO_THROW(current.iser & restored);
	CMemorySerializer old;
	old.oser.version = previous;
	EXPECT_THROW(old.oser & source, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(OwnedWorldBonusSerialization, ThreeRewardBonusListsGuardBeforeResources)
{
	for(size_t which = 0; which < 3; ++which)
	{
		Rewardable::Reward source;
		expectOldWritable(source);
		const std::array lists{&source.heroBonuses, &source.playerBonuses, &source.commanderBonuses};
		lists[which]->push_back(marker());
		expectPrefixRejectedWithCurrentRoundtrip(source);
	}
}

TEST(OwnedWorldBonusSerialization, VisitInfoGuardsBeforeLimiter)
{
	Rewardable::VisitInfo source{};
	expectOldWritable(source);
	source.reward.heroBonuses.push_back(marker());
	expectPrefixRejectedWithCurrentRoundtrip(source);
}

TEST(OwnedWorldBonusSerialization, ConfigurationGuardsBeforeTextsAndVisitCount)
{
	Rewardable::Configuration source;
	source.info.emplace_back();
	expectOldWritable(source);
	source.info.back().reward.commanderBonuses.push_back(marker());
	expectPrefixRejectedWithCurrentRoundtrip(source);
}

TEST(OwnedWorldBonusSerialization, RewardConfigurationPacketGuardsBeforeObjectId)
{
	SetRewardableConfiguration source;
	source.configuration.info.emplace_back();
	expectOldWritable(source);
	source.configuration.info.back().reward.playerBonuses.push_back(marker());
	expectPrefixRejectedWithCurrentRoundtrip(source);
}

TEST(OwnedWorldBonusSerialization, PlayerStoredAndBattleBonusPayloadsGuardBeforeColor)
{
	for(const bool battleList : {false, true})
	{
		PlayerState source(nullptr);
		expectOldWritable(source);
		if(battleList) source.battleBonuses.push_back(*marker());
		else source.addNewBonus(marker());
		CMemorySerializer current;
		ASSERT_NO_THROW(current.oser & source);
		PlayerState restored(nullptr);
		ASSERT_NO_THROW(current.iser & restored);
		if(battleList)
		{
			ASSERT_EQ(restored.battleBonuses.size(), 1u);
			EXPECT_EQ(restored.battleBonuses.front().type, marker()->type);
		}
		else
		{
			ASSERT_EQ(restored.getExportedBonusList().size(), 1u);
			EXPECT_EQ(restored.getExportedBonusList().front()->type, marker()->type);
		}
		CMemorySerializer old;
		old.oser.version = previous;
		EXPECT_THROW(old.oser & source, std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
	}
}

TEST(OwnedWorldBonusSerialization, TeamGuardsBeforeIdAndFog)
{
	TeamState source;
	expectOldWritable(source);
	source.addNewBonus(marker());
	expectPrefixRejectedWithCurrentRoundtrip(source);
}
TEST(OwnedWorldBonusSerialization, GiveBonusRetainsItsExistingBonusFirstProtection)
{
	GiveBonus source(GiveBonus::ETarget::OBJECT, ObjectInstanceID(1),
		Bonus(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()));
	expectOldWritable(source);
	source.bonus = *marker();
	expectPrefixRejectedWithCurrentRoundtrip(source);
}

TEST(OwnedWorldBonusSerialization, RemoveBonusLocalCopyIsNotSerializedOrRejected)
{
	RemoveBonus source;
	source.source = BonusSource::OTHER;
	source.whoID = ObjectInstanceID(1);
	CMemorySerializer first;
	first.oser.version = previous;
	ASSERT_NO_THROW(first.oser & source);
	source.bonus = *marker();
	CMemorySerializer second;
	second.oser.version = previous;
	ASSERT_NO_THROW(second.oser & source);
	EXPECT_EQ(first.extractBuffer(), second.extractBuffer());
}

}
