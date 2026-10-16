/*
 * HeroQueueBonusSnapshotTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/battle/BattleHexArray.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace heroQueueTest
{
namespace
{
Bonus withNested(const std::shared_ptr<const Bonus> & nested)
{
	BonusParametersOnCombatEvent event;
	event.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{nested, true});
	event.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectSpell{SpellID::CURE, 2, false});
	Bonus result;
	result.parameters = std::make_shared<BonusParameters>(event);
	return result;
}

class UnknownUpdater final : public IUpdater {};
class UnknownLimiter final : public ILimiter {};
class UnknownPropagator final : public IPropagator {};
struct UnknownBonus final : public Bonus {};

std::vector<std::byte> ordinaryBonusWire(const Bonus & bonus)
{
	CMemorySerializer memory;
	memory.oser & bonus;
	return memory.extractBuffer();
}

void expectRefusal(const Bonus & bonus, const char * reason)
{
	try
	{
		bonusSnapshotBytes(bonus);
		FAIL() << "Expected snapshot refusal: " << reason;
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), reason);
	}
}
}

TEST(HeroQueueBonusSnapshotTest, NativeTerrainSentinelUsesNumericRuntimeWire)
{
	Bonus original;
	original.limiter = std::make_shared<TerrainLimiter>();
	CMemorySerializer memory(bonusSnapshotBytes(original));
	Bonus restored;
	std::vector<PlayerColor> owners;
	memory.iser & restored;
	memory.iser & owners;
	const auto * terrain = dynamic_cast<const TerrainLimiter *>(restored.limiter.get());
	ASSERT_NE(terrain, nullptr);
	EXPECT_EQ(terrain->terrainType.getNum(), -4);
	EXPECT_EQ(owners, (std::vector<PlayerColor>{original.bonusOwner}));
}

TEST(HeroQueueBonusSnapshotTest, UnitOnHexMembershipDistinguishesEqualStorageWireInTraversalOrder)
{
	BattleHexArray changed{BattleHex(10)};
	changed.set(0, BattleHex(11));
	const BattleHexArray fresh{BattleHex(11)};
	ASSERT_EQ(changed.toVector(), fresh.toVector());
	ASSERT_TRUE(changed.contains(BattleHex(10)));
	ASSERT_FALSE(fresh.contains(BattleHex(10)));
	Bonus first, second;
	first.limiter = std::make_shared<UnitOnHexLimiter>(changed);
	second.limiter = std::make_shared<UnitOnHexLimiter>(fresh);
	EXPECT_EQ(ordinaryBonusWire(first), ordinaryBonusWire(second));
	EXPECT_NE(bonusSnapshotBytes(first), bonusSnapshotBytes(second));
	CMemorySerializer memory(bonusSnapshotBytes(first));
	Bonus restored;
	std::vector<PlayerColor> owners;
	std::vector<std::vector<uint8_t>> membership;
	memory.iser & restored;
	memory.iser & owners;
	memory.iser & membership;
	ASSERT_EQ(membership.size(), 1);
	ASSERT_EQ(membership.front().size(), GameConstants::BFIELD_SIZE);
	for(int cell = 0; cell < GameConstants::BFIELD_SIZE; ++cell)
		EXPECT_EQ(membership.front()[cell], cell == 10 || cell == 11 ? 1 : 0);
	const auto * restoredLimiter = dynamic_cast<const UnitOnHexLimiter *>(restored.limiter.get());
	ASSERT_NE(restoredLimiter, nullptr);
	EXPECT_EQ(restoredLimiter->applicableHexes.toVector(), fresh.toVector());
	Bonus root = withNested(std::make_shared<Bonus>(second));
	root.limiter = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{first.limiter, second.limiter});
	CMemorySerializer nestedMemory(bonusSnapshotBytes(root));
	Bonus nestedRestored;
	nestedMemory.iser & nestedRestored;
	nestedMemory.iser & owners;
	nestedMemory.iser & membership;
	ASSERT_EQ(membership.size(), 3);
	for(size_t index = 0; index < membership.size(); ++index)
	{
		ASSERT_EQ(membership[index].size(), GameConstants::BFIELD_SIZE);
		for(int cell = 0; cell < GameConstants::BFIELD_SIZE; ++cell)
			EXPECT_EQ(membership[index][cell], cell == 11 || (index == 0 && cell == 10) ? 1 : 0);
	}
}

TEST(HeroQueueBonusSnapshotTest, TopAndRecursiveCombatEffectOwnersRemainObservable)
{
	auto leaf = std::make_shared<Bonus>();
	auto middle = std::make_shared<Bonus>(withNested(leaf));
	Bonus root = withNested(middle);
	const auto initial = bonusSnapshotBytes(root);
	root.bonusOwner = PlayerColor(0);
	EXPECT_NE(initial, bonusSnapshotBytes(root));
	const auto topChanged = bonusSnapshotBytes(root);
	middle->bonusOwner = PlayerColor(1);
	EXPECT_NE(topChanged, bonusSnapshotBytes(root));
	const auto middleChanged = bonusSnapshotBytes(root);
	leaf->bonusOwner = PlayerColor(0);
	EXPECT_NE(middleChanged, bonusSnapshotBytes(root));
	CMemorySerializer memory(bonusSnapshotBytes(root));
	Bonus restored;
	std::vector<PlayerColor> owners;
	memory.iser & restored;
	memory.iser & owners;
	EXPECT_EQ(owners, (std::vector<PlayerColor>{PlayerColor(0), PlayerColor(1), PlayerColor(0)}));
}

TEST(HeroQueueBonusSnapshotTest, ReturnedOrderAndDistinctEqualEntriesAreNotNormalized)
{
	auto first = std::make_shared<Bonus>();
	auto equal = std::make_shared<Bonus>();
	auto different = std::make_shared<Bonus>();
	different->val = 17;
	BonusList list;
	for(const auto & bonus : {first, equal, different})
		list.push_back(bonus);
	const auto snapshots = bonusSnapshots(list);
	ASSERT_EQ(snapshots.size(), 3);
	EXPECT_EQ(snapshots[0], snapshots[1]);
	EXPECT_NE(snapshots[1], snapshots[2]);
	BonusList reordered;
	for(const auto & bonus : {different, equal, first})
		reordered.push_back(bonus);
	const auto reverse = bonusSnapshots(reordered);
	EXPECT_EQ(reverse, (std::vector<std::string>{snapshots[2], snapshots[1], snapshots[0]}));
	EXPECT_NE(snapshots, reverse);
}

TEST(HeroQueueBonusSnapshotTest, RuntimeProvenanceStatusAndParameterValuesAreRetained)
{
	Bonus value;
	const auto initial = bonusSnapshotBytes(value);
	value.appliedByEnemy = true;
	value.spellCasterOwner = PlayerColor(1);
	value.statusTags = {BonusStatusTag::DEBUFF};
	value.statusIdentity = "queue-oracle-status";
	value.hidden = true;
	value.turnsRemain = 3;
	value.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{3, 1, 3});
	EXPECT_NE(initial, bonusSnapshotBytes(value));
	CMemorySerializer memory(bonusSnapshotBytes(value));
	Bonus restored;
	memory.iser & restored;
	EXPECT_EQ(restored.appliedByEnemy, value.appliedByEnemy);
	EXPECT_EQ(restored.spellCasterOwner, value.spellCasterOwner);
	EXPECT_EQ(restored.statusTags, value.statusTags);
	EXPECT_EQ(restored.statusIdentity, value.statusIdentity);
	EXPECT_EQ(restored.hidden, value.hidden);
	EXPECT_EQ(restored.turnsRemain, value.turnsRemain);
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toVector(), value.parameters->toVector());
}

TEST(HeroQueueBonusSnapshotTest, AllCurrentParameterAlternativesAndKnownGraphsAreSupported)
{
	for(const auto & parameters : std::vector<TBonusParametersPtr>{
		std::make_shared<BonusParameters>(int32_t(7)), std::make_shared<BonusParameters>(CreatureID(0)),
		std::make_shared<BonusParameters>(SpellID(SpellID::CURE)),
		std::make_shared<BonusParameters>(std::vector<int32_t>{2, 1}),
		withNested(std::make_shared<Bonus>()).parameters, std::make_shared<BonusParameters>(JsonNode())})
	{
		Bonus value;
		value.parameters = parameters;
		value.propagator = std::make_shared<CPropagatorNodeType>();
		value.updater = std::make_shared<TimesHeroLevelDivideStackLevelUpdater>();
		value.propagationUpdater = std::make_shared<OwnerUpdater>();
		value.limiter = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{
			std::make_shared<TerrainLimiter>(), std::make_shared<HasChargesLimiter>()});
		EXPECT_NO_THROW(bonusSnapshotBytes(value));
	}
}

TEST(HeroQueueBonusSnapshotTest, MissingStepValueIsRefusedAtEveryUpdaterPosition)
{
	for(const int step : {1, 2})
	{
		Bonus value;
		value.updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 1, step);
		expectRefusal(value, "Unsupported snapshot updater graph");
		auto composite = std::make_shared<CompositeUpdater>();
		composite->updaters.push_back(value.updater);
		value.updater = composite;
		expectRefusal(value, "Unsupported snapshot updater graph");
		value.propagationUpdater = value.updater;
		value.updater.reset();
		expectRefusal(value, "Unsupported snapshot updater graph");
		expectRefusal(withNested(std::make_shared<Bonus>(value)), "Unsupported snapshot updater graph");
	}
}

TEST(HeroQueueBonusSnapshotTest, CyclesDepthAndGraphSizeAreRefusedBeforeWriting)
{
	auto updater = std::make_shared<CompositeUpdater>();
	updater->updaters.push_back(updater);
	Bonus value;
	value.updater = updater;
	expectRefusal(value, "Unsupported snapshot graph depth or size");
	updater->updaters.clear();
	value.updater.reset();
	auto limiter = std::make_shared<AllOfLimiter>();
	limiter->limiters.push_back(limiter);
	value.limiter = limiter;
	expectRefusal(value, "Unsupported snapshot graph depth or size");
	limiter->limiters.clear();
	value.limiter.reset();
	auto cyclicBonus = std::make_shared<Bonus>();
	cyclicBonus->parameters = withNested(cyclicBonus).parameters;
	expectRefusal(*cyclicBonus, "Unsupported snapshot graph depth or size");
	cyclicBonus->parameters.reset();
	std::shared_ptr<const Bonus> deep = std::make_shared<Bonus>();
	for(unsigned index = 0; index < 16; ++index)
		deep = std::make_shared<Bonus>(withNested(deep));
	expectRefusal(*deep, "Unsupported snapshot graph depth or size");
	updater->updaters.resize(257);
	value.updater = updater;
	expectRefusal(value, "Unsupported snapshot graph depth or size");
	value.updater.reset();
	JsonNode deepJson;
	for(unsigned index = 0; index < 16; ++index)
	{
		JsonNode outer;
		outer.Vector().push_back(std::move(deepJson));
		deepJson = std::move(outer);
	}
	value.parameters = std::make_shared<BonusParameters>(deepJson);
	expectRefusal(value, "Unsupported snapshot graph depth or size");
	value.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>(257));
	expectRefusal(value, "Unsupported snapshot graph depth or size");
}

TEST(HeroQueueBonusSnapshotTest, UnknownDynamicTypesCannotSilentlyFallBackToBaseWire)
{
	Bonus value;
	value.updater = std::make_shared<UnknownUpdater>();
	expectRefusal(value, "Unsupported snapshot updater graph");
	value.updater.reset();
	value.limiter = std::make_shared<UnknownLimiter>();
	expectRefusal(value, "Unsupported snapshot limiter graph");
	value.limiter.reset();
	value.propagator = std::make_shared<UnknownPropagator>();
	expectRefusal(value, "Unsupported snapshot propagator graph");
	expectRefusal(UnknownBonus(), "Unsupported snapshot bonus type");
	expectRefusal(withNested(std::make_shared<UnknownBonus>()), "Unsupported snapshot bonus type");
}
}
