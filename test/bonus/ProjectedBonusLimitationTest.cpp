/*
 * ProjectedBonusLimitationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/bonuses/BonusLimitation.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitAdjacency.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include <stdexcept>
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/json/JsonBonus.h"

namespace test
{
using namespace ::testing;
using Decision = ILimiter::EDecision;

class ProjectedBonusLimitationTest : public Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 8};
	CStack original{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	UnitInfoMock info, neighborInfo;
	BonusBearerMock bonuses, neighborBonuses;
	UnitEnvironmentMock environment;
	battle::CUnitStateDetached projected{&info, &bonuses};
	battle::CUnitStateDetached neighbor{&neighborInfo, &neighborBonuses};
	BonusList accepted, pending;
	Bonus input{BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};

	void SetUp() override
	{
		for(auto * data : {&info, &neighborInfo})
		{
			EXPECT_CALL(*data, unitBaseAmount()).WillRepeatedly(Return(8));
			EXPECT_CALL(*data, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
			EXPECT_CALL(*data, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
			EXPECT_CALL(*data, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		}
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(17));
		EXPECT_CALL(neighborInfo, unitId()).WillRepeatedly(Return(18));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(2)));
		EXPECT_CALL(neighborInfo, unitOwner()).WillRepeatedly(Return(PlayerColor(3)));
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		for(auto * bearer : {&bonuses, &neighborBonuses})
			bearer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 20, BonusSourceID(CreatureID(0))));
		projected.localInit(&environment);
		neighbor.localInit(&environment);
		original.position = BattleHex(2, 2);
		projected.position = BattleHex(5, 5);
		neighbor.position = BattleHex(6, 5);
	}

	Decision decision(const ILimiter & limiter, const BonusEvaluationContext & evaluation)
	{
		return limiter.limit({input, original, accepted, pending, evaluation});
	}
};

TEST_F(ProjectedBonusLimitationTest, LiveFixedPointMatchesExistingQueryAndKeepsPointers)
{
	auto dependent = std::make_shared<Bonus>(input);
	dependent->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::SHOOTER);
	auto provider = std::make_shared<Bonus>(input);
	provider->type = BonusType::SHOOTER;
	original.addNewBonus(dependent);
	original.addNewBonus(provider);
	BonusList all;
	all.push_back(dependent);
	all.push_back(provider);
	limitBonuses(all, accepted, original);
	ASSERT_EQ(accepted.size(), 2);
	EXPECT_EQ(accepted[0], provider);
	EXPECT_EQ(accepted[1], dependent);
	const auto live = original.getUnstackedBonuses(Selector::all);
	ASSERT_EQ(live->size(), accepted.size());
	for(size_t i = 0; i < accepted.size(); ++i)
		EXPECT_EQ((*live)[i], accepted[i]);
}

TEST_F(ProjectedBonusLimitationTest, LiveAndExplicitContextKeepLimiterDecisions)
{
	CCreatureTypeLimiter creature(*original.unitType(), false);
	CreatureLevelLimiter level(0, 100);
	CreatureAlignmentLimiter alignment(original.unitType()->getAlignment());
	FactionLimiter faction(original.unitType()->getFactionID());
	UnitOnHexLimiter hex(original.getHexes());
	OppositeSideLimiter owner;
	HasAnotherBonusLimiter dependency(BonusType::SHOOTER);
	HasChargesLimiter charges;
	const BonusLimitationContext legacy{input, original, accepted, pending};
	for(const ILimiter * limiter : std::vector<const ILimiter *>{&creature, &level, &alignment, &faction, &hex, &owner, &dependency, &charges})
		EXPECT_EQ(limiter->limit(legacy), decision(*limiter, BonusEvaluationContext(original)));
	EXPECT_EQ(BonusEvaluationContext(original).acceptsBonus(input), original.acceptsBonus(input));
}

TEST_F(ProjectedBonusLimitationTest, FormCreatureLevelFactionAndAlignmentUseProjectedSpecies)
{
	projected.beginBattleForm(CreatureID(14), 3);
	const BonusEvaluationContext context(original, projected);
	CCreatureTypeLimiter creature(*projected.unitType(), false);
	FactionLimiter faction(projected.unitType()->getFactionID());
	CreatureAlignmentLimiter alignment(projected.unitType()->getAlignment());
	ASSERT_NE(projected.unitType()->getFactionID(), original.unitType()->getFactionID());
	EXPECT_EQ(decision(creature, context), Decision::ACCEPT);
	EXPECT_EQ(decision(creature, original), Decision::DISCARD);
	EXPECT_EQ(decision(faction, context), Decision::ACCEPT);
	EXPECT_EQ(decision(faction, original), Decision::DISCARD);
	EXPECT_EQ(decision(alignment, context), Decision::ACCEPT);
	projected.endBattleForm();
	projected.beginBattleForm(CreatureID(2), 3);
	const auto level = projected.unitType()->getLevel();
	ASSERT_NE(level, original.unitType()->getLevel());
	CreatureLevelLimiter exactLevel(level, level + 1);
	EXPECT_EQ(decision(exactLevel, context), Decision::ACCEPT);
	EXPECT_EQ(decision(exactLevel, original), Decision::DISCARD);
}

TEST_F(ProjectedBonusLimitationTest, ChangedPositionAndDoubleWideFootprintReachHexLimiter)
{
	projected.beginBattleForm(CreatureID(10), 3);
	ASSERT_TRUE(projected.doubleWide());
	ASSERT_EQ(projected.getHexes().size(), 2);
	const auto rear = battle::Unit::occupiedHex(projected.position, true, projected.unitSide());
	UnitOnHexLimiter hex(BattleHexArray{rear});
	const BonusEvaluationContext context(original, projected);
	EXPECT_EQ(decision(hex, context), Decision::ACCEPT);
	EXPECT_EQ(decision(hex, original), Decision::DISCARD);
	projected.position = BattleHex(10, 5);
	EXPECT_EQ(decision(hex, context), Decision::DISCARD);
}

TEST_F(ProjectedBonusLimitationTest, RosterReadsSequentialNeighborFormMovementAndDeath)
{
	ProjectedBonusRoster roster({&projected, &neighbor});
	const BonusEvaluationContext context(original, projected, roster);
	UnitAdjacentLimiter adjacent;
	adjacent.targetUnit = CreatureID(0);
	EXPECT_EQ(decision(adjacent, context), Decision::ACCEPT);
	neighbor.beginBattleForm(CreatureID(14), 3);
	EXPECT_EQ(decision(adjacent, context), Decision::DISCARD);
	adjacent.targetUnit = CreatureID(14);
	EXPECT_EQ(decision(adjacent, context), Decision::ACCEPT);
	neighbor.position = BattleHex(12, 5);
	EXPECT_EQ(decision(adjacent, context), Decision::DISCARD);
	neighbor.position = BattleHex(6, 5);
	int64_t damage = neighbor.getAvailableHealth();
	neighbor.damage(damage);
	ASSERT_FALSE(neighbor.alive());
	EXPECT_EQ(decision(adjacent, context), Decision::DISCARD);
}

TEST_F(ProjectedBonusLimitationTest, RosterMatchesActualLiveCallbackOrderAndSharedPredicate)
{
	BattleInfo battle(nullptr);
	for(int id = 0; id < 3; ++id)
	{
		auto unit = std::make_unique<CStack>(&descriptor, PlayerColor(id), id, BattleSide::ATTACKER);
		unit->position = BattleHex(5 + id, 5);
		battle.stacks.push_back(std::move(unit));
	}
	const auto * subject = battle.stacks[0].get();
	ProjectedBonusRoster roster({subject, battle.stacks[1].get(), battle.stacks[2].get()});
	const auto projectedUnits = roster.adjacentUnits(*subject);
	const auto liveUnits = battle.battleAdjacentUnits(subject);
	ASSERT_EQ(projectedUnits.size(), liveUnits.size());
	for(size_t i = 0; i < liveUnits.size(); ++i)
		EXPECT_EQ(projectedUnits[i], liveUnits[i]);
	ASSERT_EQ(projectedUnits.size(), 1);
	EXPECT_EQ(projectedUnits.front(), battle.stacks[1].get());
}

TEST_F(ProjectedBonusLimitationTest, ProjectedOwnerFeedsOppositeSideAndOriginalRemainsUnchanged)
{
	input.bonusOwner = PlayerColor(0);
	OppositeSideLimiter opposite;
	const BonusEvaluationContext context(original, projected);
	EXPECT_EQ(decision(opposite, original), Decision::DISCARD);
	EXPECT_EQ(decision(opposite, context), Decision::ACCEPT);
	EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor::UNFLAGGABLE));
	input.bonusOwner = PlayerColor::NEUTRAL;
	EXPECT_EQ(decision(opposite, context), Decision::DISCARD);
	EXPECT_EQ(original.unitOwner(), PlayerColor(0));
}

TEST_F(ProjectedBonusLimitationTest, AcceptanceFiltersOriginalFormSourcesButNotUnrelatedBonus)
{
	projected.beginBattleForm(CreatureID(2), 3);
	const BonusEvaluationContext context(original, projected);
	input.source = BonusSource::CREATURE_ABILITY;
	input.sid = BonusSourceID(CreatureID(0));
	EXPECT_FALSE(context.acceptsBonus(input));
	EXPECT_TRUE(original.acceptsBonus(input));
	input.source = BonusSource::STACK_EXPERIENCE;
	EXPECT_FALSE(context.acceptsBonus(input));
	input.source = BonusSource::OTHER;
	EXPECT_TRUE(context.acceptsBonus(input));
	input.source = BonusSource::CREATURE_ABILITY;
	input.sid = BonusSourceID(CreatureID(2));
	EXPECT_TRUE(context.acceptsBonus(input));
}

TEST_F(ProjectedBonusLimitationTest, SummonProvenanceUsesOriginalHeroAbsenceWithoutAffectingNativeSources)
{
	projected.summoned = true;
	projected.natureSummoned = false;
	input.source = BonusSource::SECONDARY_SKILL;
	input.sid = BonusSourceID(SecondarySkill(SecondarySkill::decode("new-horizons:sylvanLuck")));
	const BonusEvaluationContext context(original, projected);
	EXPECT_FALSE(context.acceptsBonus(input));
	projected.natureSummoned = true;
	ASSERT_EQ(original.getMyHero(), nullptr);
	EXPECT_FALSE(context.acceptsBonus(input));
	input.source = BonusSource::CREATURE_ABILITY;
	EXPECT_TRUE(context.acceptsBonus(input));
}

TEST_F(ProjectedBonusLimitationTest, SameContextDrivesUpdaterAndIterativeDependentLimiters)
{
	ProjectedBonusRoster roster({&projected, &neighbor});
	const BonusEvaluationContext context(original, projected, roster);
	auto provider = std::make_shared<Bonus>(input);
	provider->type = BonusType::SHOOTER;
	auto adjacent = std::make_shared<UnitAdjacentLimiter>();
	adjacent->targetUnit = CreatureID(0);
	provider->limiter = adjacent;
	auto dependent = std::make_shared<Bonus>(input);
	dependent->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::SHOOTER);
	TimesStackSizeUpdater updater;
	BonusList all;
	all.push_back(updater.createUpdatedBonus(dependent, context));
	all.push_back(provider);
	limitBonuses(all, accepted, context);
	ASSERT_EQ(accepted.size(), 2);
	EXPECT_EQ(accepted[0], provider);
	EXPECT_EQ(accepted[1]->val, projected.getCount());
	neighbor.position = BattleHex(12, 5);
	accepted.clear();
	limitBonuses(all, accepted, context);
	EXPECT_TRUE(accepted.empty());
	EXPECT_EQ(all.size(), 2);
	EXPECT_EQ(dependent->val, 7);
}

TEST_F(ProjectedBonusLimitationTest, OriginalFormAcceptancePrecedesLimiterFixedPoint)
{
	projected.beginBattleForm(CreatureID(2), 3);
	auto provider = std::make_shared<Bonus>(input);
	provider->type = BonusType::SHOOTER;
	provider->source = BonusSource::CREATURE_ABILITY;
	provider->sid = BonusSourceID(CreatureID(0));
	auto dependent = std::make_shared<Bonus>(input);
	dependent->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::SHOOTER);
	BonusList all;
	all.push_back(dependent);
	all.push_back(provider);
	limitBonuses(all, accepted, BonusEvaluationContext(original, projected));
	EXPECT_TRUE(accepted.empty());
	EXPECT_EQ(all[1], provider);
}

TEST_F(ProjectedBonusLimitationTest, ExplicitTerrainKeepsDefinedOriginalBaseTerrainDuringProjection)
{
	CStackInstance base(nullptr, CreatureID(0), 8, true);
	original.base = &base;
	ASSERT_EQ(base.getCurrentTerrain(), TerrainId::NONE);
	projected.beginBattleForm(CreatureID(14), 3);
	const BonusEvaluationContext context(original, projected);
	TerrainLimiter none(TerrainId::NONE);
	TerrainLimiter grass(ETerrainId::GRASS);
	EXPECT_EQ(decision(none, original), Decision::ACCEPT);
	EXPECT_EQ(decision(none, context), Decision::ACCEPT);
	EXPECT_EQ(decision(grass, context), Decision::DISCARD);
	EXPECT_EQ(decision(grass, original), Decision::DISCARD);
}

TEST_F(ProjectedBonusLimitationTest, NativeTerrainUsesDefinedBaseAndExplicitAcceptedPendingInputs)
{
	CStackInstance base(nullptr, CreatureID(0), 8, true);
	original.base = &base;
	ASSERT_EQ(base.getCurrentTerrain(), TerrainId::NONE);
	const BonusEvaluationContext context(original, projected);
	TerrainLimiter native;
	EXPECT_EQ(decision(native, context), decision(native, original));
	auto provider = std::make_shared<Bonus>(input);
	provider->type = BonusType::TERRAIN_NATIVE;
	pending.push_back(provider);
	EXPECT_EQ(decision(native, context), Decision::NOT_SURE);
	projected.beginBattleForm(CreatureID(14), 3);
	EXPECT_EQ(decision(native, context), Decision::NOT_SURE);
	accepted.push_back(provider);
	EXPECT_EQ(decision(native, context), Decision::ACCEPT);
	EXPECT_EQ(decision(native, original), Decision::ACCEPT);
}

TEST_F(ProjectedBonusLimitationTest, InvalidRosterIdentityAndMissingRosterNeverFallBackToLive)
{
	EXPECT_THROW((ProjectedBonusRoster({nullptr})), std::invalid_argument);
	EXPECT_THROW((ProjectedBonusRoster({&projected, &projected})), std::invalid_argument);
	ProjectedBonusRoster other({&neighbor});
	EXPECT_THROW((BonusEvaluationContext(original, projected, other)), std::invalid_argument);
	UnitAdjacentLimiter adjacent;
	adjacent.targetUnit = CreatureID(0);
	EXPECT_THROW(decision(adjacent, BonusEvaluationContext(original, projected)), std::logic_error);
	projected.position = BattleHex::INVALID;
	EXPECT_EQ(decision(adjacent, BonusEvaluationContext(original, projected)), Decision::DISCARD);
	EXPECT_THROW((BonusLimitationContext(input, original, accepted, pending, BonusEvaluationContext(*original.unitType()))), std::invalid_argument);
}
TEST_F(ProjectedBonusLimitationTest, GenericFilteredAdjacencyKeepsNativeArmyWhenBothUnitsAreControlled)
{
	ProjectedBonusRoster roster({&projected, &neighbor});
	const BonusEvaluationContext context(original, projected, roster);
	UnitAdjacentLimiter limiter;
	limiter.targetUnit = CreatureID(0);
	limiter.filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	EXPECT_EQ(decision(limiter, context), Decision::DISCARD);
	EXPECT_CALL(neighborInfo, unitOwner()).WillRepeatedly(Return(PlayerColor(2)));
	EXPECT_EQ(decision(limiter, context), Decision::ACCEPT);
	for(auto * bearer : {&bonuses, &neighborBonuses})
		bearer->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
			BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_CALL(environment, unitEffectiveOwner(_)).Times(0);
	EXPECT_EQ(decision(limiter, context), Decision::ACCEPT);
	limiter.filter.relationship = AdjacentUnitFilter::Relationship::OPPOSING_ARMY;
	EXPECT_EQ(decision(limiter, context), Decision::DISCARD);
	neighbor.position = BattleHex(12, 5);
	limiter.filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	EXPECT_EQ(decision(limiter, context), Decision::DISCARD);
}
TEST_F(ProjectedBonusLimitationTest, GenericAnyCreatureFilterExcludesSelfDeadAndWrongNativeTrait)
{
	ProjectedBonusRoster roster({&projected, &neighbor});
	const BonusEvaluationContext context(original, projected, roster);
	UnitAdjacentLimiter limiter;
	limiter.filter.anyCreature = true;
	EXPECT_EQ(decision(limiter, context), Decision::ACCEPT);
	limiter.filter.nativeTrait = AdjacentUnitFilter::NativeTrait::UNDEAD;
	EXPECT_EQ(context.matchesAdjacentUnit(projected, CreatureID(), limiter.filter), false);
	EXPECT_EQ(decision(limiter, context), neighbor.unitType()->hasBonusOfType(BonusType::UNDEAD)
		? Decision::ACCEPT : Decision::DISCARD);
	limiter.filter.nativeTrait = AdjacentUnitFilter::NativeTrait::ANY;
	int64_t all = neighbor.getAvailableHealth();
	neighbor.damage(all);
	EXPECT_EQ(decision(limiter, context), Decision::DISCARD);
}
TEST_F(ProjectedBonusLimitationTest, GenericAdjacencyOldLayoutResetsReusedMetadataAndRejectsNewOldWriter)
{
	UnitAdjacentLimiter legacy;
	legacy.targetUnit = CreatureID(0);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	memory.oser & legacy;
	CMemorySerializer expected;
	expected.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	expected.oser & legacy.targetUnit;
	const auto original1144Bytes = memory.extractBuffer();
	EXPECT_EQ(original1144Bytes, expected.extractBuffer())
		<< "Format1144 must remain exactly the original targetUnit-only layout";
	CMemorySerializer originalReader(original1144Bytes);
	UnitAdjacentLimiter restored;
	restored.filter.anyCreature = true;
	restored.filter.includeUpgrades = true;
	restored.filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	restored.filter.nativeTrait = AdjacentUnitFilter::NativeTrait::UNDEAD;
	originalReader.iser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	originalReader.iser & restored;
	EXPECT_TRUE(restored.filter.isDefault());
	EXPECT_EQ(restored.targetUnit, legacy.targetUnit);
	CMemorySerializer second;
	second.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	restored.filter.anyCreature = true;
	restored.targetUnit = CreatureID();
	CMemorySerializer accepted;
	EXPECT_NO_THROW(accepted.oser & restored);
	EXPECT_FALSE(accepted.extractBuffer().empty());
	EXPECT_THROW(second.oser & restored, std::runtime_error);
	EXPECT_TRUE(second.extractBuffer().empty());
}
TEST_F(ProjectedBonusLimitationTest, GenericAdjacencyCurrentMetadataRoundtripsAndRejectsInvalidEnums)
{
	UnitAdjacentLimiter limiter;
	limiter.filter.anyCreature = true;
	limiter.filter.includeUpgrades = true;
	limiter.filter.relationship = AdjacentUnitFilter::Relationship::OPPOSING_ARMY;
	limiter.filter.nativeTrait = AdjacentUnitFilter::NativeTrait::LIVING;
	CMemorySerializer memory;
	memory.oser & limiter;
	UnitAdjacentLimiter restored;
	memory.iser & restored;
	EXPECT_EQ(restored.filter.relationship, limiter.filter.relationship);
	EXPECT_EQ(restored.filter.nativeTrait, limiter.filter.nativeTrait);
	EXPECT_EQ(restored.filter.includeUpgrades, limiter.filter.includeUpgrades);
	EXPECT_EQ(restored.filter.anyCreature, limiter.filter.anyCreature);
	CMemorySerializer invalid;
	limiter.filter.relationship = static_cast<AdjacentUnitFilter::Relationship>(255);
	EXPECT_THROW(invalid.oser & limiter, std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty());
}
TEST_F(ProjectedBonusLimitationTest, GenericAdjacencyJsonFactoryRetainsDefaultsAndRejectsMalformedMetadata)
{
	JsonNode node;
	node["type"].String() = "UNIT_ADJACENT";
	node["anyCreature"].Bool() = true;
	node["relationship"].String() = "sameArmy";
	node["nativeTrait"].String() = "undead";
	node["includeUpgrades"].Bool() = true;
	const auto parsed = UnitAdjacentLimiter::fromJson(node);
	EXPECT_EQ(parsed->toJsonNode(), node);
	for(const char * key : {"relationship", "nativeTrait"})
	{
		auto bad = node;
		bad[key].String() = "not-a-filter-value";
		EXPECT_THROW(UnitAdjacentLimiter::fromJson(bad), std::runtime_error);
	}
	for(const char * key : {"anyCreature", "includeUpgrades"})
	{
		auto bad = node;
		bad[key].Integer() = 1;
		EXPECT_THROW(UnitAdjacentLimiter::fromJson(bad), std::runtime_error);
	}
	auto contradictory = node;
	contradictory["creature"].String() = "core:pikeman";
	EXPECT_THROW(UnitAdjacentLimiter::fromJson(contradictory), std::runtime_error);
}

TEST_F(ProjectedBonusLimitationTest, NestedFilteredBonusRejectsOldWriterBeforeAnyBonusPrefix)
{
	auto filtered = std::make_shared<UnitAdjacentLimiter>();
	filtered->filter.anyCreature = true;
	auto nested = std::make_shared<AllOfLimiter>();
	nested->add(filtered);
	CMemorySerializer aggregateOld;
	aggregateOld.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(aggregateOld.oser & *nested, std::runtime_error);
	EXPECT_TRUE(aggregateOld.extractBuffer().empty());
	CMemorySerializer aggregateCurrent;
	EXPECT_NO_THROW(aggregateCurrent.oser & *nested);
	EXPECT_FALSE(aggregateCurrent.extractBuffer().empty());
	Bonus bonus = input;
	bonus.limiter = nested;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(memory.oser & bonus, std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
	CMemorySerializer current;
	EXPECT_NO_THROW(current.oser & bonus);
	Bonus restored;
	EXPECT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.toJsonNode(), bonus.toJsonNode());
	filtered->filter = {};
	filtered->targetUnit = CreatureID(0);
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_NO_THROW(legacy.oser & bonus);
}

}
