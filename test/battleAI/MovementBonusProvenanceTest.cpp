/*
 * MovementBonusProvenanceTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../AI/BattleAI/MovementBonusProvenance.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Updaters.h"

namespace
{
class ForeignQuerySource final : public CBonusSystemNode
{
public:
	ForeignQuerySource() : CBonusSystemNode(BonusNodeType::STACK_BATTLE, true) {}
	TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override
	{
		ADD_FAILURE() << "A provenance check must not query this foreign effective provider";
		return std::make_shared<BonusList>();
	}
};
}

TEST(MovementBonusProvenanceTest, SharedParentIsNotACycleAndScanDoesNotReadEffectiveBonuses)
{
	CBonusSystemNode root(BonusNodeType::STACK_BATTLE, true), first(BonusNodeType::HERO, true),
		second(BonusNodeType::HERO, true), shared(BonusNodeType::CREATURE, true);
	first.attachToSource(shared);
	second.attachToSource(shared);
	root.attachToSource(first);
	root.attachToSource(second);
	const auto version = root.getTreeVersion();
	EXPECT_TRUE(movementBonusProvenance::invariantSources(root));
	EXPECT_EQ(root.getTreeVersion(), version);
	root.detachFromSource(first);
	root.detachFromSource(second);
	first.detachFromSource(shared);
	second.detachFromSource(shared);
}

TEST(MovementBonusProvenanceTest, SourceCycleRefusesRatherThanRecursingIntoAnEffectiveQuery)
{
	CBonusSystemNode first(BonusNodeType::STACK_BATTLE, true), second(BonusNodeType::CREATURE, true);
	first.attachToSource(second);
	second.attachToSource(first);
	EXPECT_FALSE(movementBonusProvenance::invariantSources(first));
	second.detachFromSource(first);
	first.detachFromSource(second);
}

TEST(MovementBonusProvenanceTest, RawInactiveLimiterOnInheritedSourceIsNotLostBySelection)
{
	CBonusSystemNode root(BonusNodeType::STACK_BATTLE, true), parent(BonusNodeType::CREATURE, true);
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK,
		BonusSource::OTHER, -1, BonusSourceID());
	bonus->limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(92)});
	parent.addNewBonus(bonus);
	root.attachToSource(parent);
	EXPECT_FALSE(movementBonusProvenance::invariantSources(root));
	root.detachFromSource(parent);
}

TEST(MovementBonusProvenanceTest, UpdaterProvenanceIsUnavailableWithoutExecutingTheUpdater)
{
	CBonusSystemNode root(BonusNodeType::STACK_BATTLE, true);
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK,
		BonusSource::OTHER, -1, BonusSourceID());
	bonus->updater = std::make_shared<TimesStackSizeUpdater>();
	root.addNewBonus(bonus);
	EXPECT_FALSE(movementBonusProvenance::invariantSources(root));
}

TEST(MovementBonusProvenanceTest, DeepSourceClosureIsBoundedAndDoesNotUseRecursiveBonusCollection)
{
	std::vector<std::unique_ptr<CBonusSystemNode>> nodes;
	for(size_t index = 0; index < 1025; ++index)
		nodes.push_back(std::make_unique<CBonusSystemNode>(BonusNodeType::CREATURE, true));
	for(size_t index = 1; index < nodes.size(); ++index)
		nodes[index - 1]->attachToSource(*nodes[index]);
	EXPECT_FALSE(movementBonusProvenance::invariantSources(*nodes.front()));
	for(size_t index = 1; index < nodes.size(); ++index)
		nodes[index - 1]->detachFromSource(*nodes[index]);
}

TEST(MovementBonusProvenanceTest, ForeignNativeQueryOverrideCannotClaimRawCollectorProvenance)
{
	ForeignQuerySource source;
	EXPECT_FALSE(movementBonusProvenance::invariantSources(source));
}
