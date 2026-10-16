/*
 * ProjectedUnitMagicResistanceTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_battle_IBattleState.h"
#include "../mock/mock_battle_Unit.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ProjectedUnitMagicResistance.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include <array>
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
std::shared_ptr<Bonus> resistanceValue(BonusType type, int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, amount, BonusSourceID());
}

class ResistanceBattle : public BattleStateMock
{
public:
	JsonNode magicRules;
	const JsonNode & getMagicRules() const override { return magicRules; }
};

// Test-only adapter exercises the REAL unit environment entry. It deliberately
// throws for required unrelated domains; it is not a production partial adapter.
class ResistanceOnlyEnvironment final : public battle::IUnitEnvironment
{
	const battle::ProjectedUnitMagicResistance & resistance;
public:
	explicit ResistanceOnlyEnvironment(const battle::ProjectedUnitMagicResistance & resistance) : resistance(resistance) {}
	bool unitHasAmmoCart(const battle::Unit *) const override { throw std::logic_error("unrequested ammo domain"); }
	PlayerColor unitEffectiveOwner(const battle::Unit *) const override { throw std::logic_error("unrequested owner domain"); }
	std::optional<int> unitMagicResistance(const battle::Unit * unit) const override { return resistance.query(unit); }
};
}

class ProjectedUnitMagicResistanceTest : public Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 8};
	CStackInstance base{nullptr, CreatureID(0), 8, true};
	CStack subject{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	CStack ally{&descriptor, PlayerColor(0), 18, BattleSide::ATTACKER};
	CStack enemy{&descriptor, PlayerColor(1), 19, BattleSide::DEFENDER};
	std::array<CStack *, 3> originals{&subject, &ally, &enemy};
	NiceMock<ResistanceBattle> battleState;
	CPlayerBattleCallback live{&battleState, PlayerColor(0)};
	ProjectedBonusGraph graph;
	std::unique_ptr<ProjectedBonusRoster> roster;
	std::unique_ptr<battle::ProjectedUnitMagicResistance> query;
	std::unique_ptr<ResistanceOnlyEnvironment> environment;
	std::array<std::unique_ptr<battle::CUnitStateDetached>, 3> units;
	std::shared_ptr<Bonus> aura;

	void SetUp() override
	{
		for(auto * original : originals)
		{
			auto health = resistanceValue(BonusType::STACK_HEALTH, 20);
			health->source = BonusSource::CREATURE_ABILITY;
			health->sid = BonusSourceID(CreatureID(0));
			original->addNewBonus(health);
			original->base = &base; // defined based terrain; never execute unbound terrain UB
			original->health.init();
		}
		subject.position = BattleHex(5, 5);
		ally.position = BattleHex(6, 5);
		enemy.position = BattleHex(5, 6);
		subject.addNewBonus(resistanceValue(BonusType::MAGIC_RESISTANCE, 20));
		aura = resistanceValue(BonusType::SPELL_RESISTANCE_AURA, 25);
		ally.addNewBonus(aura);
		enemy.addNewBonus(resistanceValue(BonusType::SPELL_RESISTANCE_AURA, 90));
		ON_CALL(battleState, getUnitsIf(_)).WillByDefault(Invoke([this](const battle::UnitFilter & predicate)
		{
			battle::Units result;
			for(const auto * original : originals)
				if(predicate(original))
					result.push_back(original);
			return result;
		}));
	}

	void initialize()
	{
		std::vector<const battle::Unit *> ordered;
		for(size_t index = 0; index < units.size(); ++index)
		{
			units[index] = std::make_unique<battle::CUnitStateDetached>(originals[index], &graph.view(*originals[index]));
			battle::UnitStateValueFields::copy(*units[index], *originals[index]);
			ordered.push_back(units[index].get());
		}
		roster = std::make_unique<ProjectedBonusRoster>(std::move(ordered));
		for(size_t index = 0; index < units.size(); ++index)
			graph.bind(*originals[index], *units[index], *roster);
		query = std::make_unique<battle::ProjectedUnitMagicResistance>(*roster, battleState.magicRules);
		environment = std::make_unique<ResistanceOnlyEnvironment>(*query);
		for(auto & unit : units)
			unit->bindStateEnvironment(environment.get());
	}
};

TEST_F(ProjectedUnitMagicResistanceTest, DefaultLiveParityThroughRealDetachedEnvironmentEntry)
{
	initialize();
	const auto before = subject.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 40);
	EXPECT_EQ(query->query(units[0].get()), 40);
	EXPECT_EQ(units[0]->magicResistance(), 40);
	EXPECT_EQ(subject.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedUnitMagicResistanceTest, NullSubjectRetainsLiveZeroWithoutRosterAccess)
{
	initialize();
	EXPECT_EQ(live.battleGetMagicResistance(nullptr), 0);
	EXPECT_EQ(query->query(nullptr), 0);
}

TEST_F(ProjectedUnitMagicResistanceTest, ForeignSubjectRefusesBeforeAnyBonusPayloadQuery)
{
	initialize();
	StrictMock<UnitMock> foreign;
	EXPECT_CALL(foreign, getAllBonuses(_, _)).Times(0);
	EXPECT_CALL(foreign, getUnstackedBonuses(_)).Times(0);
	EXPECT_THROW(query->query(&foreign), std::invalid_argument);
	EXPECT_THROW(query->query(&subject), std::invalid_argument); // no mutable live fallback
}

TEST_F(ProjectedUnitMagicResistanceTest, SequentialMovementChangesAdjacencyWithoutChangingLiveRoster)
{
	initialize();
	const auto before = ally.save();
	EXPECT_EQ(units[0]->magicResistance(), 40);
	units[1]->position = BattleHex(12, 8);
	EXPECT_EQ(units[0]->magicResistance(), 20);
	units[1]->position = BattleHex(6, 5);
	EXPECT_EQ(units[0]->magicResistance(), 40);
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 40);
	EXPECT_EQ(ally.save(), before);
}

TEST_F(ProjectedUnitMagicResistanceTest, SequentialDeathRemovesAuraButRetainsOriginalLivingState)
{
	initialize();
	const auto before = ally.save();
	units[1]->health.reset();
	ASSERT_FALSE(units[1]->alive());
	EXPECT_EQ(units[0]->magicResistance(), 20);
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 40);
	EXPECT_TRUE(ally.alive());
	EXPECT_EQ(ally.save(), before);
}

TEST_F(ProjectedUnitMagicResistanceTest, SequentialGraphAuraRemovalAndAdditionDoNotUseWarmLiveCache)
{
	initialize();
	const auto retained = ally.getAllBonuses(Selector::type()(BonusType::SPELL_RESISTANCE_AURA));
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_EQ(units[0]->magicResistance(), 40);
	graph.removeExported(ally, aura);
	EXPECT_EQ(units[0]->magicResistance(), 20);
	graph.addExported(ally, resistanceValue(BonusType::SPELL_RESISTANCE_AURA, 60));
	EXPECT_EQ(units[0]->magicResistance(), 68);
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 40);
	ASSERT_EQ(retained->size(), 1);
	EXPECT_EQ((*retained)[0].get(), aura.get());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedUnitMagicResistanceTest, NestedAuraUpdaterUsesLatestProjectedCountNotOriginalCount)
{
	ally.removeBonus(aura);
	aura = resistanceValue(BonusType::SPELL_RESISTANCE_AURA, 3);
	aura->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 1, 3);
	ally.addNewBonus(aura);
	initialize();
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 39); // floor(20 +24 -4.8)
	EXPECT_EQ(units[0]->magicResistance(), 39);
	int64_t damage = 80;
	units[1]->health.damage(damage);
	ASSERT_EQ(units[1]->getCount(), 4);
	EXPECT_EQ(units[0]->magicResistance(), 29); // floor(20 +12 -2.4)
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 39);
	EXPECT_EQ(ally.getCount(), 8);
}

TEST_F(ProjectedUnitMagicResistanceTest, CapturedMagicRulesKeepModernCapAndLegacyRangeDistinct)
{
	subject.addNewBonus(resistanceValue(BonusType::MAGIC_RESISTANCE, 80));
	battleState.magicRules["rulesetVersion"].Integer() = 3;
	initialize();
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 75);
	EXPECT_EQ(units[0]->magicResistance(), 75);
	const JsonNode legacyRules;
	battle::ProjectedUnitMagicResistance legacy(*roster, legacyRules);
	EXPECT_EQ(legacy.query(units[0].get()), 100);
}

TEST_F(ProjectedUnitMagicResistanceTest, HypnotizedEnemyAuraStillUsesRawOwnerAsLegacyRuleRequires)
{
	initialize();
	graph.addExported(enemy, resistanceValue(BonusType::HYPNOTIZED, 1));
	ASSERT_TRUE(units[2]->isHypnotized());
	EXPECT_EQ(units[2]->unitOwner(), PlayerColor(1));
	EXPECT_EQ(units[0]->magicResistance(), 40); // enemy90 is not a friendly aura
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 40);
}

TEST_F(ProjectedUnitMagicResistanceTest, SynchronousProjectedFormRearHexChangesResistanceAdjacency)
{
	ally.position = BattleHex(7, 5); // one hex leaves a gap; cavalry rear closes it
	const auto original = ally.save();
	ally.beginBattleForm(CreatureID(10), 2);
	ASSERT_TRUE(ally.doubleWide());
	const auto incoming = ally.save();
	ally.load(original);
	initialize();
	EXPECT_EQ(units[0]->magicResistance(), 20);
	graph.loadBoundState(ally, *units[1], incoming);
	ASSERT_TRUE(units[1]->doubleWide());
	EXPECT_EQ(units[0]->magicResistance(), 40);
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 20);
	EXPECT_EQ(ally.save(), original);
}

TEST_F(ProjectedUnitMagicResistanceTest, PoisonedPreparationCannotFallBackToLiveResistance)
{
	initialize();
	const auto before = ally.save();
	auto invalid = before;
	invalid["state"]["guardianSpiritHitPoints"].Integer() = -1;
	EXPECT_THROW(graph.loadBoundState(ally, *units[1], invalid), std::runtime_error);
	EXPECT_THROW(units[0]->magicResistance(), std::logic_error);
	EXPECT_EQ(live.battleGetMagicResistance(&subject), 40);
	EXPECT_EQ(ally.save(), before);
}
}
