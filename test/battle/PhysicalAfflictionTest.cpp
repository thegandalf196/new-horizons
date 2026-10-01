/*
 * PhysicalAfflictionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/json/JsonBonus.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using physicalAfflictions::Affliction;

class PhysicalAfflictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PhysicalAfflictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

Bonus makeAfflictionMarker(const BonusSource source, const SpellID spell, const std::string & kind,
	const int64_t applicationOrder = 0)
{
	const BonusSourceID sourceID(spell);
	Bonus result(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION, source, 0, sourceID);
	JsonNode parameters;
	parameters["kind"].String() = kind;
	parameters["applicationOrder"].Integer() = applicationOrder;
	result.parameters = std::make_shared<BonusParameters>(parameters);
	return result;
}

Bonus makeEffect(const BonusSource source, const SpellID spell, const BonusType type, const int32_t value)
{
	return Bonus(BonusDuration::N_TURNS, type, source, value, BonusSourceID(spell));
}

size_t markerCount(const std::vector<Bonus> & bonuses)
{
	return static_cast<size_t>(std::count_if(bonuses.begin(), bonuses.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}));
}

std::vector<std::string> markerKinds(const std::vector<Bonus> & bonuses)
{
	std::vector<std::string> result;
	for(const auto & bonus : bonuses)
	{
		if(bonus.type != BonusType::PHYSICAL_AFFLICTION)
			continue;
		const auto metadata = physicalAfflictions::markerMetadata(bonus);
		if(metadata)
			result.push_back(metadata->kind);
	}
	return result;
}

std::vector<Bonus> makeGroup(const std::string & kind, const SpellID spell, const int64_t applicationOrder,
	const BonusType effectType = BonusType::STACKS_SPEED, const int32_t effectValue = -1)
{
	return {
		makeEffect(BonusSource::SPELL_EFFECT, spell, effectType, effectValue),
		makeAfflictionMarker(BonusSource::SPELL_EFFECT, spell, kind, applicationOrder)
	};
}

void appendGroup(BonusBearerMock & bearer, const std::vector<Bonus> & group)
{
	for(const auto & bonus : group)
		bearer.addNewBonus(std::make_shared<Bonus>(bonus));
}

class PhysicalAfflictionTest : public testing::Test
{
protected:
	void addDefaultHealthBonus(BonusBearerMock & bearer)
	{
		bearer.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
			BonusSource::CREATURE_ABILITY, 10, BonusSourceID(CreatureID(0))));
	}

	std::unique_ptr<battle::CUnitStateDetached> detachedView(BonusBearerMock & bearer)
	{
		auto result = std::make_unique<battle::CUnitStateDetached>(&unitInfo, &bearer);
		result->localInit(&environment);
		return result;
	}

	void SetUp() override
	{
		using namespace testing;
		EXPECT_CALL(unitInfo, unitBaseAmount()).WillRepeatedly(Return(1));
		EXPECT_CALL(unitInfo, unitId()).WillRepeatedly(Return(7));
		EXPECT_CALL(unitInfo, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(unitInfo, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
		EXPECT_CALL(unitInfo, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		unitCreature = CreatureID(0).toCreature();
		ASSERT_NE(unitCreature, nullptr);
		EXPECT_CALL(unitInfo, unitType()).WillRepeatedly(Return(unitCreature));
		addDefaultHealthBonus(bonuses);
		unit = std::make_unique<battle::CUnitStateDetached>(&unitInfo, &bonuses);
		unit->localInit(&environment);
	}

	void append(const std::vector<Bonus> & group)
	{
		appendGroup(bonuses, group);
	}

	UnitInfoMock unitInfo;
	UnitEnvironmentMock environment;
	BonusBearerMock bonuses;
	const CCreature * unitCreature = nullptr;
	std::unique_ptr<battle::CUnitStateDetached> unit;
};

class PhysicalAfflictionHypotheticTest : public BattleTestFixture
{
};

JsonNode markerConfig(const std::string & kind)
{
	JsonNode result;
	result["type"].String() = "PHYSICAL_AFFLICTION";
	result["val"].Integer() = 0;
	result["addInfo"]["kind"].String() = kind;
	result.setModScope("core", false);
	return result;
}

class CountingLegacySaveHandler
{
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;

	bool hasFeature(Version) const
	{
		return false;
	}

	template<typename T>
	CountingLegacySaveHandler & operator&(T &)
	{
		++fieldsWritten;
		return *this;
	}

	size_t fieldsWritten = 0;
};
}

TEST_F(PhysicalAfflictionTest, EnumeratesByAfflictionPriorityAndIgnoresUnmarkedNegativeMagic)
{
	append(makeGroup("other", SpellID(44), 8));
	append(makeGroup("bleeding", SpellID(43), 2));
	append(makeGroup("other", SpellID(45), 3));
	append(makeGroup("disease", SpellID(42), 20));
	append(makeGroup("poison", SpellID(41), 50));
	// A negative magic bonus alone does not make its source an eligible affliction.
	bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, -9, BonusSourceID(SpellID(999))));

	const auto afflictions = physicalAfflictions::enumerate(*unit);
	std::vector<std::string> kinds;
	for(const auto & affliction : afflictions)
	{
		kinds.push_back(affliction.kind);
		EXPECT_FALSE(affliction.storedPoison);
		EXPECT_TRUE(std::all_of(affliction.effects.begin(), affliction.effects.end(), [](const Bonus & bonus)
		{
			return bonus.type != BonusType::PHYSICAL_AFFLICTION;
		}));
	}
	std::sort(kinds.begin(), kinds.end());
	EXPECT_EQ(kinds, (std::vector<std::string>{"bleeding", "disease", "other", "other", "poison"}));
	ASSERT_FALSE(afflictions.empty());
	const auto first = physicalAfflictions::first(*unit);
	ASSERT_TRUE(first.has_value());
	EXPECT_EQ(first->kind, "poison");
}

TEST_F(PhysicalAfflictionTest, FirstUsesDiseaseThenBleedingThenOldestOtherPriority)
{
	BonusBearerMock diseaseBonuses;
	addDefaultHealthBonus(diseaseBonuses);
	appendGroup(diseaseBonuses, makeGroup("disease", SpellID(90), 40));
	appendGroup(diseaseBonuses, makeGroup("bleeding", SpellID(91), 1));
	appendGroup(diseaseBonuses, makeGroup("other", SpellID(92), 0));
	const auto diseaseUnit = detachedView(diseaseBonuses);
	const auto diseaseFirst = physicalAfflictions::first(*diseaseUnit);
	ASSERT_TRUE(diseaseFirst.has_value());
	EXPECT_EQ(diseaseFirst->kind, "disease");

	BonusBearerMock bleedingBonuses;
	addDefaultHealthBonus(bleedingBonuses);
	appendGroup(bleedingBonuses, makeGroup("bleeding", SpellID(93), 40));
	appendGroup(bleedingBonuses, makeGroup("other", SpellID(94), 0));
	const auto bleedingUnit = detachedView(bleedingBonuses);
	const auto bleedingFirst = physicalAfflictions::first(*bleedingUnit);
	ASSERT_TRUE(bleedingFirst.has_value());
	EXPECT_EQ(bleedingFirst->kind, "bleeding");

	BonusBearerMock otherBonuses;
	addDefaultHealthBonus(otherBonuses);
	appendGroup(otherBonuses, makeGroup("other", SpellID(95), 8));
	appendGroup(otherBonuses, makeGroup("other", SpellID(96), 2));
	const auto otherUnit = detachedView(otherBonuses);
	const auto otherFirst = physicalAfflictions::first(*otherUnit);
	ASSERT_TRUE(otherFirst.has_value());
	EXPECT_EQ(otherFirst->kind, "other");
	EXPECT_EQ(otherFirst->applicationOrder, 2);
}

TEST_F(PhysicalAfflictionTest, RefreshKeepsItsOrderAndNewDefaultOrExplicitOrdersAreStable)
{
	append(makeGroup("disease", SpellID(51), 4));
	append(makeGroup("other", SpellID(52), 9));

	const auto refreshed = physicalAfflictions::stampApplicationOrder(*unit,
		makeGroup("disease", SpellID(51), 0));
	ASSERT_EQ(markerCount(refreshed), 1u);
	ASSERT_EQ(markerKinds(refreshed), (std::vector<std::string>{"disease"}));
	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(refreshed.begin(), refreshed.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 4);

	const auto defaultOrder = physicalAfflictions::stampApplicationOrder(*unit,
		makeGroup("other", SpellID(53), 0));
	const auto explicitOrder = physicalAfflictions::stampApplicationOrder(*unit,
		makeGroup("other", SpellID(54), 27));
	ASSERT_EQ(markerCount(defaultOrder), 1u);
	ASSERT_EQ(markerCount(explicitOrder), 1u);
	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(defaultOrder.begin(), defaultOrder.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 10);
	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(explicitOrder.begin(), explicitOrder.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 27);
}

TEST_F(PhysicalAfflictionTest, BatchRemoveAndReapplyTreatsTheGroupAsNewAndAllocatesOrdersInApplyOrder)
{
	append(makeGroup("other", SpellID(61), 2));
	append(makeGroup("bleeding", SpellID(62), 8));
	auto afflictions = physicalAfflictions::enumerate(*unit);
	const auto existing = std::find_if(afflictions.begin(), afflictions.end(), [](const Affliction & affliction)
	{
		return affliction.kind == "bleeding";
	});
	ASSERT_NE(existing, afflictions.end());
	const auto removed = physicalAfflictions::removalPlan(*unit, *existing);
	ASSERT_EQ(removed.size(), 2u);

	auto reapplied = makeGroup("bleeding", SpellID(62), 0);
	auto firstNew = makeGroup("other", SpellID(63), 0);
	auto secondNew = makeGroup("other", SpellID(64), 0);
	physicalAfflictions::stampEffectChanges(*unit, removed, {&reapplied, &firstNew, &secondNew});

	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(reapplied.begin(), reapplied.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 3);
	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(firstNew.begin(), firstNew.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 4);
	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(secondNew.begin(), secondNew.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 5);
}

TEST_F(PhysicalAfflictionTest, DuplicateMarkersCollapseAndConflictingKindsFailBeforeAnyIncomingMutation)
{
	auto duplicate = makeGroup("poison", SpellID(71), 0);
	duplicate.push_back(makeAfflictionMarker(BonusSource::SPELL_EFFECT, SpellID(71), "poison", 0));
	const auto secondGroup = makeGroup("disease", SpellID(74), 0);
	duplicate.insert(duplicate.end(), secondGroup.begin(), secondGroup.end());
	const auto stamped = physicalAfflictions::stampApplicationOrder(*unit, duplicate);
	EXPECT_EQ(markerCount(stamped), 2u);
	EXPECT_EQ(markerCount(duplicate), 3u);
	for(const auto & marker : stamped)
	{
		if(marker.type != BonusType::PHYSICAL_AFFLICTION)
			continue;
		const auto metadata = physicalAfflictions::markerMetadata(marker);
		ASSERT_TRUE(metadata.has_value());
		EXPECT_EQ(metadata->applicationOrder, marker.sid == BonusSourceID(SpellID(71)) ? 1 : 2);
	}

	const auto firstDefault = physicalAfflictions::stampApplicationOrder(*unit, makeGroup("other", SpellID(75), 0));
	const auto firstDefaultMarker = std::find_if(firstDefault.begin(), firstDefault.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	});
	ASSERT_NE(firstDefaultMarker, firstDefault.end());
	EXPECT_EQ(physicalAfflictions::markerMetadata(*firstDefaultMarker)->applicationOrder, 1);

	auto validEarlierBatchItem = makeGroup("other", SpellID(72), 0);
	auto conflicting = makeGroup("poison", SpellID(73), 0);
	conflicting.push_back(makeAfflictionMarker(BonusSource::SPELL_EFFECT, SpellID(73), "disease", 0));
	std::vector<std::vector<Bonus> *> incoming{&validEarlierBatchItem, &conflicting};
	EXPECT_THROW(physicalAfflictions::stampEffectChanges(*unit, {}, incoming), std::invalid_argument);
	EXPECT_EQ(markerCount(validEarlierBatchItem), 1u);
	EXPECT_EQ(physicalAfflictions::markerMetadata(*std::find_if(validEarlierBatchItem.begin(), validEarlierBatchItem.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	}))->applicationOrder, 0);
	EXPECT_EQ(markerCount(conflicting), 2u);
	EXPECT_EQ(markerKinds(conflicting), (std::vector<std::string>{"poison", "disease"}));
}

TEST_F(PhysicalAfflictionTest, PartialEffectRemovalKeepsTheActiveMarkerAndItsApplicationOrder)
{
	auto currentGroup = makeGroup("disease", SpellID(76), 7);
	currentGroup.push_back(makeEffect(BonusSource::SPELL_EFFECT, SpellID(76), BonusType::MAGIC_RESISTANCE, -2));
	append(currentGroup);
	const auto removal = std::find_if(currentGroup.begin(), currentGroup.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::STACKS_SPEED;
	});
	ASSERT_NE(removal, currentGroup.end());
	std::vector<Bonus> removed{*removal};
	auto refreshed = makeGroup("disease", SpellID(76), 0);

	physicalAfflictions::stampEffectChanges(*unit, removed, {&refreshed});
	const auto marker = std::find_if(refreshed.begin(), refreshed.end(), [](const Bonus & bonus)
	{
		return bonus.type == BonusType::PHYSICAL_AFFLICTION;
	});
	ASSERT_NE(marker, refreshed.end());
	EXPECT_EQ(physicalAfflictions::markerMetadata(*marker)->applicationOrder, 7);
}

TEST_F(PhysicalAfflictionTest, RemovalPlanIncludesOnlyTheExactSourceAndIdentifierGroup)
{
	append(makeGroup("disease", SpellID(81), 1));
	append(makeGroup("poison", SpellID(82), 2));
	auto sameIdentifierOtherSource = makeGroup("other", SpellID(81), 3);
	for(auto & bonus : sameIdentifierOtherSource)
	{
		bonus.source = BonusSource::CREATURE_ABILITY;
		appendGroup(bonuses, {bonus});
	}

	const auto afflictions = physicalAfflictions::enumerate(*unit);
	const auto disease = std::find_if(afflictions.begin(), afflictions.end(), [](const Affliction & affliction)
	{
		return affliction.kind == "disease";
	});
	ASSERT_NE(disease, afflictions.end());
	const auto plan = physicalAfflictions::removalPlan(*unit, *disease);
	ASSERT_EQ(plan.size(), 2u);
	EXPECT_EQ(markerCount(plan), 1u);
	for(const auto & bonus : plan)
	{
		EXPECT_EQ(bonus.source, disease->source);
		EXPECT_EQ(bonus.sid, disease->sourceID);
	}
}

TEST_F(PhysicalAfflictionTest, StoredPhysicalPoisonSurvivesAcquiringDetachedState)
{
	unit->physicalPoisonBaseDamage = 37;
	unit->physicalPoisonActivationsRemaining = 3;
	unit->physicalPoisonSourceStackId = 29;

	const auto acquired = unit->acquireState();
	ASSERT_NE(acquired, nullptr);
	EXPECT_EQ(acquired->physicalPoisonBaseDamage, 37);
	EXPECT_EQ(acquired->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(acquired->physicalPoisonSourceStackId, 29);

	const auto afflictions = physicalAfflictions::enumerate(*acquired);
	ASSERT_EQ(afflictions.size(), 1u);
	EXPECT_TRUE(afflictions.front().storedPoison);
	EXPECT_EQ(afflictions.front().kind, "poison");
	EXPECT_TRUE(afflictions.front().effects.empty());
	EXPECT_TRUE(physicalAfflictions::removalPlan(*acquired, afflictions.front()).empty());

	auto restored = detachedView(bonuses);
	restored->load(acquired->save());
	EXPECT_EQ(restored->physicalPoisonBaseDamage, 37);
	EXPECT_EQ(restored->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(restored->physicalPoisonSourceStackId, 29);
}

TEST(PhysicalAfflictionParserTest, MarkerSchemaAndParserAcceptDefaultAndMaximumOrderAndRejectOutOfRangeFloats)
{
	auto defaultOrder = markerConfig("poison");
	ASSERT_TRUE(JsonUtils::validate(defaultOrder, "vcmi:bonusInstance", "physical-affliction marker schema"));
	const auto parsedDefault = JsonUtils::parseBonus(defaultOrder);
	ASSERT_NE(parsedDefault, nullptr);
	ASSERT_EQ(parsedDefault->type, BonusType::PHYSICAL_AFFLICTION);
	ASSERT_NE(parsedDefault->parameters, nullptr);
	const auto defaultMetadata = physicalAfflictions::markerMetadata(*parsedDefault);
	ASSERT_TRUE(defaultMetadata.has_value());
	EXPECT_EQ(defaultMetadata->kind, "poison");
	EXPECT_EQ(defaultMetadata->applicationOrder, 0);
	EXPECT_EQ(parsedDefault->val, 0);

	auto maximumOrder = markerConfig("disease");
	maximumOrder["addInfo"]["applicationOrder"].Integer() = std::numeric_limits<int64_t>::max();
	const auto parsedMaximum = JsonUtils::parseBonus(maximumOrder);
	ASSERT_NE(parsedMaximum, nullptr);
	ASSERT_NE(parsedMaximum->parameters, nullptr);
	const auto maximumMetadata = physicalAfflictions::markerMetadata(*parsedMaximum);
	ASSERT_TRUE(maximumMetadata.has_value());
	EXPECT_EQ(maximumMetadata->applicationOrder, std::numeric_limits<int64_t>::max());

	auto floatingOverflow = markerConfig("poison");
	floatingOverflow["addInfo"]["applicationOrder"].Float() = 0x1p63;
	const auto parsedOverflow = JsonUtils::parseBonus(floatingOverflow);
	ASSERT_NE(parsedOverflow, nullptr);
	EXPECT_EQ(parsedOverflow->parameters, nullptr);
	EXPECT_THROW(physicalAfflictions::markerMetadata(*parsedOverflow), std::invalid_argument);

	auto negativeOverflow = markerConfig("poison");
	negativeOverflow["addInfo"]["applicationOrder"].Float() = -1e100;
	EXPECT_FALSE(JsonUtils::validate(negativeOverflow, "vcmi:bonusInstance", "negative physical-affliction marker schema"));
	const auto parsedNegativeOverflow = JsonUtils::parseBonus(negativeOverflow);
	ASSERT_NE(parsedNegativeOverflow, nullptr);
	EXPECT_EQ(parsedNegativeOverflow->parameters, nullptr);
}

TEST(PhysicalAfflictionPersistenceTest, JsonParametersAndMarkerRoundTripAtCurrentVersion)
{
	JsonNode metadata;
	metadata["kind"].String() = "bleeding";
	metadata["applicationOrder"].Integer() = 23;

	BonusParameters parameters(metadata);
	CMemorySerializer parameterSerializer;
	parameterSerializer.oser.version = ESerializationVersion::CURRENT;
	parameterSerializer.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(parameterSerializer.oser & parameters);
	BonusParameters restoredParameters;
	ASSERT_NO_THROW(parameterSerializer.iser & restoredParameters);
	EXPECT_EQ(restoredParameters.toJsonNode(), metadata);

	const auto marker = makeAfflictionMarker(BonusSource::SPELL_EFFECT, SpellID(101), "bleeding", 23);
	CMemorySerializer markerSerializer;
	markerSerializer.oser.version = ESerializationVersion::CURRENT;
	markerSerializer.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(markerSerializer.oser & marker);
	Bonus restoredMarker;
	ASSERT_NO_THROW(markerSerializer.iser & restoredMarker);
	EXPECT_EQ(restoredMarker.type, BonusType::PHYSICAL_AFFLICTION);
	EXPECT_EQ(restoredMarker.val, 0);
	const auto restoredMetadata = physicalAfflictions::markerMetadata(restoredMarker);
	ASSERT_TRUE(restoredMetadata.has_value());
	EXPECT_EQ(restoredMetadata->kind, "bleeding");
	EXPECT_EQ(restoredMetadata->applicationOrder, 23);
}

TEST(PhysicalAfflictionPersistenceTest, OlderSaveHandlerRejectsMarkerBeforeWritingFields)
{
	auto marker = makeAfflictionMarker(BonusSource::SPELL_EFFECT, SpellID(102), "poison", 1);
	CountingLegacySaveHandler legacy;
	EXPECT_THROW(marker.serialize(legacy), std::runtime_error);
	EXPECT_EQ(legacy.fieldsWritten, 0u);
}

TEST_F(PhysicalAfflictionHypotheticTest, DetachedChildRemovesAndRefreshesGenericOtherGroupWithoutMutatingParent)
{
	startGame();
	startBattle();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(target, nullptr);
	beginCombat();

	auto environment = std::make_shared<PhysicalAfflictionEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);

	const BonusSourceID sourceID(BonusCustomSource(1707));
	Bonus marker(BonusDuration::N_TURNS, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::OTHER, 0, sourceID);
	JsonNode parameters;
	parameters["kind"].String() = "other";
	marker.parameters = std::make_shared<BonusParameters>(parameters);
	marker.turnsRemain = 2;
	Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::OTHER, -2, sourceID);
	effect.turnsRemain = 2;
	parent->addUnitBonus(target->unitId(), {marker, effect});

	const auto parentAfflictions = physicalAfflictions::enumerate(*parent->battleGetUnitByID(target->unitId()));
	ASSERT_EQ(parentAfflictions.size(), 1u);
	ASSERT_EQ(parentAfflictions.front().kind, "other");
	const auto originalOrder = parentAfflictions.front().applicationOrder;
	ASSERT_EQ(originalOrder, 1);
	ASSERT_EQ(parentAfflictions.front().effects.size(), 1u);

	HypotheticBattle removalChild(environment.get(), parent);
	const auto * childUnit = removalChild.battleGetUnitByID(target->unitId());
	ASSERT_NE(childUnit, nullptr);
	const auto selected = physicalAfflictions::first(*childUnit);
	ASSERT_TRUE(selected);
	const auto removal = physicalAfflictions::removalPlan(*childUnit, *selected);
	ASSERT_EQ(markerCount(removal), 1u);
	removalChild.getForUpdate(target->unitId())->removeUnitBonus(removal);
	EXPECT_TRUE(physicalAfflictions::enumerate(*removalChild.battleGetUnitByID(target->unitId())).empty());
	const auto afterChildRemovalParent = physicalAfflictions::enumerate(*parent->battleGetUnitByID(target->unitId()));
	ASSERT_EQ(afterChildRemovalParent.size(), 1u);
	EXPECT_EQ(afterChildRemovalParent.front().applicationOrder, originalOrder);
	EXPECT_EQ(afterChildRemovalParent.front().effects.size(), 1u);
	EXPECT_TRUE(physicalAfflictions::enumerate(*target).empty());

	HypotheticBattle refreshChild(environment.get(), parent);
	auto refreshedMarker = marker;
	refreshedMarker.turnsRemain = 5;
	auto refreshedEffect = effect;
	refreshedEffect.turnsRemain = 5;
	refreshChild.getForUpdate(target->unitId())->updateUnitBonus({refreshedMarker, refreshedEffect});
	const auto * refreshedUnit = refreshChild.battleGetUnitByID(target->unitId());
	const auto refreshedAfflictions = physicalAfflictions::enumerate(*refreshedUnit);
	ASSERT_EQ(refreshedAfflictions.size(), 1u);
	EXPECT_EQ(refreshedAfflictions.front().applicationOrder, originalOrder);
	ASSERT_EQ(refreshedAfflictions.front().effects.size(), 1u);
	EXPECT_EQ(refreshedAfflictions.front().effects.front().turnsRemain, 5);
	const auto projectedMarkers = refreshedUnit->getAllBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus->type == BonusType::PHYSICAL_AFFLICTION;
	}));
	ASSERT_EQ(projectedMarkers->size(), 1u);
	const auto afterRefreshParent = physicalAfflictions::enumerate(*parent->battleGetUnitByID(target->unitId()));
	ASSERT_EQ(afterRefreshParent.size(), 1u);
	EXPECT_EQ(afterRefreshParent.front().applicationOrder, originalOrder);
	EXPECT_EQ(afterRefreshParent.front().effects.size(), 1u);
	EXPECT_TRUE(physicalAfflictions::enumerate(*target).empty());
}

TEST_F(PhysicalAfflictionHypotheticTest, MarkerOnlyRemovalPreservesTimedEffectAcrossChildUpdatesAndGrandchildren)
{
	startGame();
	startBattle();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(target, nullptr);
	beginCombat();

	auto environment = std::make_shared<PhysicalAfflictionEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);

	const BonusSourceID groupID(BonusCustomSource(1710));
	Bonus marker(BonusDuration::N_TURNS, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::OTHER, 0, groupID);
	JsonNode parameters;
	parameters["kind"].String() = "other";
	marker.parameters = std::make_shared<BonusParameters>(parameters);
	marker.turnsRemain = 5;
	Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::OTHER, -2, groupID);
	effect.turnsRemain = 5;
	parent->addUnitBonus(target->unitId(), {marker, effect});

	auto markerOnlyChild = std::make_shared<HypotheticBattle>(environment.get(), parent);
	markerOnlyChild->getForUpdate(target->unitId())->removeUnitBonus(std::vector<Bonus>{marker});
	EXPECT_TRUE(physicalAfflictions::enumerate(*markerOnlyChild->battleGetUnitByID(target->unitId())).empty());
	const CSelector groupEffect([&](const Bonus * bonus)
	{
		return bonus->type == BonusType::STACKS_SPEED
			&& bonus->source == BonusSource::OTHER
			&& bonus->sid == groupID;
	});
	auto childEffects = markerOnlyChild->battleGetUnitByID(target->unitId())->getAllBonuses(groupEffect);
	ASSERT_EQ(childEffects->size(), 1u);
	EXPECT_EQ(childEffects->front()->turnsRemain, 5);

	const BonusSourceID unrelatedID(BonusCustomSource(1711));
	Bonus unrelated(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::OTHER, 1, unrelatedID);
	unrelated.turnsRemain = 5;
	markerOnlyChild->updateUnitBonus(target->unitId(), {unrelated});
	markerOnlyChild->nextRound();
	childEffects = markerOnlyChild->battleGetUnitByID(target->unitId())->getAllBonuses(groupEffect);
	ASSERT_EQ(childEffects->size(), 1u);
	EXPECT_EQ(childEffects->front()->turnsRemain, 4);

	auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), markerOnlyChild);
	EXPECT_TRUE(physicalAfflictions::enumerate(*grandchild->battleGetUnitByID(target->unitId())).empty());
	const auto grandchildEffects = grandchild->battleGetUnitByID(target->unitId())->getAllBonuses(groupEffect);
	ASSERT_EQ(grandchildEffects->size(), 1u);
	EXPECT_EQ(grandchildEffects->front()->val, -2);
	EXPECT_EQ(grandchildEffects->front()->turnsRemain, 4);

	const auto parentAfflictions = physicalAfflictions::enumerate(*parent->battleGetUnitByID(target->unitId()));
	ASSERT_EQ(parentAfflictions.size(), 1u);
	EXPECT_EQ(parentAfflictions.front().applicationOrder, 1);
	ASSERT_EQ(parentAfflictions.front().effects.size(), 1u);
	EXPECT_EQ(parentAfflictions.front().effects.front().turnsRemain, 5);
	EXPECT_TRUE(physicalAfflictions::enumerate(*target).empty());
}
