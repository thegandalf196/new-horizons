/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/PeerBonusDependencyClosure.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Updaters.h"
namespace
{
class TypeChangingUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & value, const BonusEvaluationContext &) const override
	{
		auto result = std::make_shared<Bonus>(*value); result->type = BonusType::SPELL_RESISTANCE_AURA; return result;
	}
};
std::shared_ptr<Bonus> raw(BonusType type, int value = 1)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}
BonusList list(std::initializer_list<std::shared_ptr<Bonus>> values)
{
	BonusList result; for(const auto & value : values) result.push_back(value); return result;
}
}
TEST(PeerBonusDependencyClosureTest, KeepsRecursiveDependenciesPerNodeWithoutEvaluatingUpdaterOrFlatteningAliases)
{
	auto aura = raw(BonusType::SPELL_RESISTANCE_AURA, 15);
	aura->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::STACKS_SPEED);
	auto speed = raw(BonusType::STACKS_SPEED, 1); speed->limiter = std::make_shared<HasChargesLimiter>(1);
	speed->updater = std::make_shared<TimesHeroLevelUpdater>(1);
	auto charge = raw(BonusType::ARTIFACT_CHARGE, 1), unused = raw(BonusType::DEFENSIVE_STANCE);
	const std::vector<BonusList> input{list({aura}), list({unused, speed, charge, speed})};
	const auto captured = ownedRecipientPeer::dependencyClosure(input);
	ASSERT_EQ(captured.size(), 2u); ASSERT_EQ(captured[1].size(), 3u);
	EXPECT_EQ(captured[1][0], speed); EXPECT_EQ(captured[1][1], charge); EXPECT_EQ(captured[1][2], speed);
	EXPECT_EQ(speed->val, 1); // Capture never runs a non-idempotent source updater.
	OwnedRecipientBonusGraph::NodeInput root, hero;
	root.facts.nodeType = BonusNodeType::STACK_BATTLE; root.facts.stackCount = 1; root.facts.stableStackLevel = 1; root.facts.acceptance.emplace();
	root.parents = {1}; root.local.assign(captured[0].begin(), captured[0].end());
	hero.facts.nodeType = BonusNodeType::HERO; hero.facts.heroLevel = 4; hero.facts.armySlots.emplace();
	hero.local.assign(captured[1].begin(), captured[1].end());
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({root, hero}, 0);
	EXPECT_EQ(graph->query(Selector::type()(BonusType::SPELL_RESISTANCE_AURA)).totalValue(), 15);
	EXPECT_EQ(speed->val, 1);
}
TEST(PeerBonusDependencyClosureTest, NestedSourceSelectorsAndChargeSidDoNotImportUnrelatedRecords)
{
	auto hypnosis = raw(BonusType::HYPNOTIZED);
	auto need = std::make_shared<HasAnotherBonusLimiter>(BonusType::NONE, BonusSource::SPELL_EFFECT);
	need->isSourceIDRelevant = true; need->sid = BonusSourceID(SpellID(3));
	hypnosis->limiter = std::make_shared<NoneOfLimiter>(std::vector<TLimiterPtr>{need});
	auto required = raw(BonusType::STACKS_SPEED), other = raw(BonusType::STACKS_SPEED);
	required->source = other->source = BonusSource::SPELL_EFFECT;
	required->sid = BonusSourceID(SpellID(3)); other->sid = BonusSourceID(SpellID(4));
	const auto result = ownedRecipientPeer::dependencyClosure({list({hypnosis, other, required})});
	ASSERT_EQ(result[0].size(), 2u); EXPECT_EQ(result[0][0], hypnosis); EXPECT_EQ(result[0][1], required);
}
TEST(PeerBonusDependencyClosureTest, CyclicDependenciesTerminateWithoutDeduplicatingQueryOccurrences)
{
	auto aura = raw(BonusType::SPELL_RESISTANCE_AURA), speed = raw(BonusType::STACKS_SPEED);
	aura->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::STACKS_SPEED);
	speed->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::SPELL_RESISTANCE_AURA);
	const auto result = ownedRecipientPeer::dependencyClosure({list({aura, aura}), list({speed, speed})});
	EXPECT_EQ(result[0].size(), 2u); EXPECT_EQ(result[1].size(), 2u);
}

TEST(PeerBonusDependencyClosureTest, ArtifactChargeClosurePreservesExactTypedSid)
{
	auto aura = raw(BonusType::SPELL_RESISTANCE_AURA); aura->sid = BonusSourceID(ArtifactID(3));
	aura->limiter = std::make_shared<HasChargesLimiter>();
	auto required = raw(BonusType::ARTIFACT_CHARGE), wrongVariant = raw(BonusType::ARTIFACT_CHARGE), wrongID = raw(BonusType::ARTIFACT_CHARGE);
	required->sid = BonusSourceID(ArtifactID(3)); wrongVariant->sid = BonusSourceID(SpellID(3)); wrongID->sid = BonusSourceID(ArtifactID(4));
	const auto result = ownedRecipientPeer::dependencyClosure({list({aura, wrongVariant, required, wrongID})});
	ASSERT_EQ(result[0].size(), 2u); EXPECT_EQ(result[0][0], aura); EXPECT_EQ(result[0][1], required);
}

TEST(PeerBonusDependencyClosureTest, RejectsUnknownTypeChangingModifierBeforeDroppingAnUnselectedRecord)
{
	auto unselected = raw(BonusType::DEFENSIVE_STANCE);
	unselected->updater = std::make_shared<TypeChangingUpdater>();
	EXPECT_THROW(ownedRecipientPeer::dependencyClosure({list({unselected})}), std::invalid_argument);
	EXPECT_EQ(unselected->type, BonusType::DEFENSIVE_STANCE);
	auto composite = std::make_shared<CompositeUpdater>();
	composite->updaters.push_back(std::make_shared<TypeChangingUpdater>()); unselected->updater = composite;
	EXPECT_THROW(ownedRecipientPeer::dependencyClosure({list({unselected})}), std::invalid_argument);
}
