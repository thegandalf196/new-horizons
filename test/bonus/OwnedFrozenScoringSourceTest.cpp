/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/bonuses/OwnedFrozenScoringSource.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
namespace
{
bonusDependencies::QueryFrameBinding profile()
{
	battle::OwnedUnitCreatureResolver::Creature body; body.id=CreatureID(7); body.key="frozen-scoring-fixture";
	bonusDependencies::QueryFrameBinding result;
	result.recipientId=31; result.creature=body.id; result.count=21;
	result.profileOwner=std::make_shared<const battle::OwnedUnitCreatureResolver>(std::vector{body});
	return result;
}
std::shared_ptr<Bonus> value(int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PHYSICAL_DEFENSE,BonusSource::OTHER,amount,BonusSourceID());
}
}
using OwnedFrozenScoringSourceTest = vcmiTestData::OriginalDataTest;

TEST_F(OwnedFrozenScoringSourceTest, SelectorRunsBeforeStackAndUnstackedAliasesStayDistinctFromValueCopies)
{
	auto low=value(7),high=value(15); low->stacking=high->stacking="same-source";
	const std::vector<Bonus> stacked{*low,*high};
	const std::optional<frozenScoring::Records> raw = frozenScoring::Records{low,low,high,nullptr};
	const auto source=frozenScoring::OwnedSource::capture(profile(),stacked,raw,false); ASSERT_TRUE(source);
	EXPECT_EQ(source->valOfBonuses(BonusType::PHYSICAL_DEFENSE),15);
	const CSelector onlyLow([](const Bonus * b){return b->val==7;});
	EXPECT_EQ(source->getAllBonuses(onlyLow)->totalValue(),7); // No eager all-list restacking.
	const auto unstacked=source->getUnstackedBonuses(onlyLow); ASSERT_EQ(unstacked->size(),size_t(2));
	EXPECT_EQ((*unstacked)[0],(*unstacked)[1]); EXPECT_EQ(unstacked->totalValue(),14);
	low->stacking.clear(); const std::vector<Bonus> duplicateValues{*low,*low};
	const auto distinct=frozenScoring::OwnedSource::capture(profile(),duplicateValues,std::nullopt,false); ASSERT_TRUE(distinct);
	const auto copies=distinct->getAllBonuses(Selector::all); ASSERT_EQ(copies->size(),size_t(2));
	EXPECT_NE((*copies)[0],(*copies)[1]); EXPECT_EQ(copies->totalValue(),14);
	EXPECT_TRUE(distinct->getUnstackedBonuses(Selector::all)->empty());
}
TEST_F(OwnedFrozenScoringSourceTest, FrozenRulesNeverRunAndLoansOwnNestedPayloadsAfterOriginalsRetire)
{
	auto record=value(7); auto updater=std::make_shared<CompositeUpdater>();
	updater->updaters.push_back(std::make_shared<TimesStackSizeUpdater>(0,100,9,1));
	record->updater=updater; record->limiter=std::make_shared<CreatureLevelLimiter>(99,100);
	const std::vector<Bonus> stacked{*record}; const std::optional<frozenScoring::Records> raw = frozenScoring::Records{record,record};
	auto source=frozenScoring::OwnedSource::capture(profile(),stacked,raw,false); ASSERT_TRUE(source);
	const auto loan=source->getUnstackedBonuses(Selector::all); ASSERT_EQ(loan->size(),size_t(2));
	updater->updaters.clear(); record->val=999; source.reset();
	EXPECT_EQ(loan->totalValue(),14); EXPECT_EQ((*loan)[0],(*loan)[1]);
	const auto * copied=dynamic_cast<const CompositeUpdater *>((*loan)[0]->updater.get()); ASSERT_NE(copied,nullptr);
	EXPECT_EQ(copied->updaters.size(),size_t(1)); EXPECT_TRUE((*loan)[0]->limiter);
}
TEST_F(OwnedFrozenScoringSourceTest, UnknownRuleAndCyclicMetadataRefuseWithoutCanonicalRawRights)
{
	class Unknown final:public TimesStackSizeUpdater {};
	auto record=value(7); record->updater=std::make_shared<Unknown>();
	EXPECT_FALSE(frozenScoring::OwnedSource::capture(profile(),std::vector<Bonus>{*record},std::nullopt,false));
	auto cycle=std::make_shared<CompositeUpdater>(); cycle->updaters.push_back(cycle); record->updater=cycle;
	EXPECT_FALSE(frozenScoring::OwnedSource::capture(profile(),std::vector<Bonus>{*record},std::nullopt,false));
	cycle->updaters.clear();
	const auto source=frozenScoring::OwnedSource::capture(profile(),{},std::nullopt,false); ASSERT_TRUE(source);
	EXPECT_TRUE(source->mayHaveBonusType(BonusType::HATE)); EXPECT_FALSE(source->isBonusCacheable());
	EXPECT_TRUE(source->matchesProfile(31,CreatureID(7))); EXPECT_FALSE(source->matchesProfile(32,CreatureID(7)));
}
