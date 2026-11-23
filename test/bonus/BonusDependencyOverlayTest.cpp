/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/Updaters.h"
namespace
{
using namespace bonusDependencies;
recipientBonusOverlay::Layer emptyLayer()
{
	recipientBonusOverlay::Records added, updated;
	std::set<std::shared_ptr<Bonus>> removed;
	std::optional<recipientBonusOverlay::Values> stacked;
	std::optional<recipientBonusOverlay::Records> unstacked;
	recipientBonusOverlay::Groups groups;
	bool veteran = false;
	return recipientBonusOverlay::Layer({added, updated, removed, stacked, unstacked, groups, veteran});
}
std::shared_ptr<Bonus> damage(int amount, BonusSource source = BonusSource::CREATURE_ABILITY)
{
	auto record = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE,
		source, amount, source == BonusSource::SPELL_EFFECT
			? BonusSourceID(SpellID(SpellID::HASTE)) : BonusSourceID(CreatureID(0)));
	record->subtype = BonusCustomSubtype::creatureDamageBoth;
	return record;
}
OwnedRecipientBonusGraph::NodeInput baseInput()
{
	OwnedRecipientBonusGraph::NodeInput input;
	input.facts.nodeType = BonusNodeType::STACK_BATTLE;
	input.facts.stackCount = 21; input.facts.stableStackLevel = 1;
	input.facts.acceptance.emplace();
	input.local = {damage(4)};
	return input;
}
}
using BonusDependencyOverlayTest = vcmiTestData::OriginalDataTest;

TEST_F(BonusDependencyOverlayTest, RecipientValuesDoNotRunLimitersUpdatersOrExtraStacking)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({baseInput()}, 0);
	auto layer = emptyLayer();
	auto effect = damage(6, BonusSource::SPELL_EFFECT);
	effect->sid = BonusSourceID(SpellID(SpellID::HASTE));
	effect->limiter = std::make_shared<CreatureLevelLimiter>(99, 100);
	effect->updater = std::make_shared<TimesStackSizeUpdater>();
	effect->stacking = "same-recipient-value";
	layer.added = {effect, effect}; // Native SWB does not restack local additions.
	const std::array layers{layer};
	const auto source = captureNativeOverlayModifiers(*graph, graph->root(), layers);
	std::array<nativeDamage::PreparedUnit, 2> base;
	const auto first = source.refresh(21, Change::None, base, 0);
	ASSERT_TRUE(first); EXPECT_EQ(first->prepared[0].unit.minDamage, 16);
	const auto next = source.refresh(8, Change::Count, base, 0, &*first);
	ASSERT_TRUE(next); EXPECT_EQ(next->prepared[0].unit.minDamage, 16);
}
TEST_F(BonusDependencyOverlayTest, NativeRawRemovalAliasesAndEarlierRecipientAdditionAliasesAreOwned)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({baseInput()}, 0);
	auto first = emptyLayer(), second = emptyLayer();
	first.removed.insert(graph->local(graph->root())[0]);
	auto local = damage(7, BonusSource::SPELL_EFFECT);
	first.added.push_back(local);
	second.removed.insert(local);
	second.added.push_back(damage(9, BonusSource::SPELL_EFFECT));
	const std::array layers{first, second};
	const auto source = captureNativeOverlayModifiers(*graph, graph->root(), layers);
	graph.reset(); local.reset(); first.added.clear(); second.removed.clear();
	std::array<nativeDamage::PreparedUnit, 2> base;
	const auto values = source.refresh(8, Change::Count, base, 0);
	ASSERT_TRUE(values); EXPECT_EQ(values->prepared[0].unit.minDamage, 9);
}
TEST_F(BonusDependencyOverlayTest, SnapshotExpiryDoesNotResurrectNativeSpellRowsOnLaterCasualty)
{
	auto input = baseInput();
	auto spell = damage(6, BonusSource::SPELL_EFFECT);
	spell->sid = BonusSourceID(SpellID(SpellID::HASTE));
	input.local.push_back(spell);
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
	auto applied = emptyLayer();
	applied.stackedSnapshot = recipientBonusOverlay::Values{*spell};
	applied.unstackedSnapshot = recipientBonusOverlay::Records{spell};
	const std::array before{applied};
	const auto old = captureNativeOverlayModifiers(*graph, graph->root(), before);
	auto expired = emptyLayer();
	expired.stackedSnapshot.emplace(); expired.unstackedSnapshot.emplace();
	const std::array after{expired};
	const auto fresh = captureNativeOverlayModifiers(*graph, graph->root(), after);
	std::array<nativeDamage::PreparedUnit, 2> base;
	const auto oldValues = old.refresh(21, Change::None, base, 0);
	const auto newValues = fresh.refresh(21, Change::None, base, 0);
	ASSERT_TRUE(oldValues); ASSERT_TRUE(newValues);
	EXPECT_EQ(oldValues->prepared[0].unit.minDamage, 10);
	EXPECT_EQ(newValues->prepared[0].unit.minDamage, 4);
	EXPECT_FALSE(fresh.refresh(8, Change::Count, base, 0, &*oldValues));
	const auto casualty = fresh.refresh(8, Change::Count, base, 0, &*newValues);
	ASSERT_TRUE(casualty); EXPECT_EQ(casualty->prepared[0].unit.minDamage, 4);
}
TEST_F(BonusDependencyOverlayTest, UnresolvedNonProjectionRemovalAndRecipientPropagationFailClosed)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({baseInput()}, 0);
	auto unknown = emptyLayer();
	unknown.removed.insert(damage(4));
	const std::array unresolved{unknown};
	EXPECT_THROW(captureNativeOverlayModifiers(*graph, graph->root(), unresolved), std::invalid_argument);
	unknown.removed.clear();
	auto aura = damage(4); aura->propagationUpdater = std::make_shared<OwnerUpdater>();
	unknown.added.push_back(aura);
	const std::array propagated{unknown};
	EXPECT_THROW(captureNativeOverlayModifiers(*graph, graph->root(), propagated), std::invalid_argument);
}
