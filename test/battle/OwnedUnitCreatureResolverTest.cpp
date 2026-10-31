/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/json/JsonNode.h"
#include <stdexcept>
namespace
{
using Resolver = battle::OwnedUnitCreatureResolver;
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
Resolver::Creature descriptor(int id)
{
	Resolver::Creature result;
	result.id = CreatureID(id);
	result.faction = FactionID(id + 2);
	result.level = id + 1;
	result.doubleWide = id == 2;
	result.icon = id + 100;
	result.warMachine = id == 2 ? ArtifactID::BALLISTA : ArtifactID::NONE;
	result.cost[EGameResID::GOLD] = id + 70;
	result.key = "owned:creature" + std::to_string(id);
	result.pluralTextID = "owned.creature" + std::to_string(id) + ".plural";
	result.singularTextID = "owned.creature" + std::to_string(id) + ".singular";
	return result;
}
class Info final : public battle::IUnitInfo
{
	const Resolver & values;
public:
	explicit Info(const Resolver & values) : values(values) {}
	int32_t unitBaseAmount() const override { return 3; }
	uint32_t unitId() const override { return 17; }
	BattleSide unitSide() const override { return BattleSide::ATTACKER; }
	PlayerColor unitOwner() const override { return PlayerColor(0); }
	SlotID unitSlot() const override { return SlotID(0); }
	const CCreature * unitType() const override { return values.creature(CreatureID(1)); }
	const battle::UnitCreatureResolver * unitCreatureResolver() const override { return &values; }
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override
	{
		return battle::UnitPreparationHealthBasis{30, 30};
	}
};
// Existing complete-form view contract; only generic source-independent records
// are present, so both forms have the same raw HP/speed. No alternate form formula.
class GenericView final : public ProjectedBonusView
{
	BonusList raw;
public:
	const battle::Unit * subject = nullptr;
	mutable int queries = 0, changes = 0;
	GenericView()
	{
		raw.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 10, BonusSourceID()));
		raw.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 5, BonusSourceID()));
	}
	const battle::Unit * evaluatedUnit() const override { return subject; }
	void onBattleFormChanged(const battle::CUnitState & unit) const override
	{
		if(&unit != subject) throw std::logic_error("Wrong generic fixture view identity");
		++changes;
	}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		++queries;
		auto result = std::make_shared<BonusList>(); raw.getBonuses(*result, selector); result->stackBonuses(); return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return getAllBonuses(selector, {}); }
	int32_t getTreeVersion() const override { return 0; }
};
}
TEST(OwnedUnitCreatureResolverTest, InertCopiedMetadataKeysAndNativeGenerationRemainIndependent)
{
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	CBonusSystemNode sentinel(BonusNodeType::UNKNOWN, true);
	const auto version = sentinel.getTreeVersion();
	auto input = descriptor(2);
	const auto expectedCost = input.cost[EGameResID::GOLD];
	{
		Resolver values({input, descriptor(1)});
		input.level = 99; input.key = "changed";
		const auto * stable = values.creature(CreatureID(2));
		WithoutLibrary absent;
		EXPECT_EQ(values.creature(CreatureID(2)), stable);
		EXPECT_EQ(stable->getLevel(), 3);
		EXPECT_EQ(stable->getFactionID(), FactionID(4));
		EXPECT_TRUE(stable->isDoubleWide());
		EXPECT_EQ(stable->getIconIndex(), 102);
		EXPECT_EQ(stable->getRecruitCost(EGameResID::GOLD), expectedCost);
		EXPECT_EQ(stable->warMachine, ArtifactID::BALLISTA);
		EXPECT_EQ(values.decode("owned:creature2"), 2);
		EXPECT_EQ(values.encode(2), "owned:creature2");
		EXPECT_THROW(values.decode("missing"), std::logic_error);
		EXPECT_THROW(values.creature(CreatureID(8)), std::logic_error);
		EXPECT_THROW(stable->getBaseHitPoints(), std::logic_error);
		EXPECT_THROW(stable->getBonusBearer(), std::logic_error);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(sentinel.getTreeVersion(), version);
}
TEST(OwnedUnitCreatureResolverTest, InvalidDuplicateIdentitiesNeverPublishDescriptorLoans)
{
	EXPECT_THROW(Resolver({descriptor(1), descriptor(1)}), std::invalid_argument);
	auto other = descriptor(2); other.key = descriptor(1).key;
	EXPECT_THROW(Resolver({descriptor(1), other}), std::invalid_argument);
	other = descriptor(2); other.id = CreatureID::NONE;
	EXPECT_THROW(Resolver({other}), std::invalid_argument);
}
TEST(OwnedUnitCreatureResolverTest, OrdinaryAndNestedMetadataNeverResolveThroughLibrary)
{
	Resolver values({descriptor(1), descriptor(2)}); Info info(values); GenericView view;
	battle::CUnitStateDetached unit(&info, &view); view.subject = &unit;
	WithoutLibrary absent;
	unit.localInit(nullptr);
	EXPECT_EQ(unit.creatureLevel(), 2);
	EXPECT_EQ(unit.creatureCost(), 71);
	EXPECT_EQ(unit.creatureIconIndex(), 101);
	EXPECT_EQ(unit.getFactionID(), FactionID(3));
	EXPECT_FALSE(unit.doubleWide());
	EXPECT_EQ(unit.getCasterNameTextID(), "owned.creature1.plural");
	battle::CUnitStateDetached nested(&unit, &unit);
	EXPECT_EQ(nested.unitCreatureResolver(), &values);
	EXPECT_EQ(nested.creatureLevel(), 2);
	EXPECT_EQ(nested.getBattleStartMaximumAggregateHP(), 30);
}
TEST(OwnedUnitCreatureResolverTest, ActiveFormMetadataSaveLoadAndPendingRestoreUseOwnedKeys)
{
	Resolver values({descriptor(1), descriptor(2)}); Info info(values); GenericView view;
	battle::CUnitStateDetached unit(&info, &view); view.subject = &unit;
	WithoutLibrary absent;
	unit.localInit(nullptr);
	unit.beginBattleForm(CreatureID(2), 1);
	EXPECT_EQ(unit.unitType(), values.creature(CreatureID(2)));
	EXPECT_EQ(unit.creatureLevel(), 3);
	EXPECT_EQ(unit.creatureCost(), 72);
	EXPECT_TRUE(unit.doubleWide());
	EXPECT_TRUE(unit.isBallista());
	EXPECT_EQ(unit.getCasterNameTextID(), "owned.creature2.plural");
	auto saved = unit.save();
	EXPECT_EQ(saved["state"]["battleFormCreature"].String(), "owned:creature2");
	EXPECT_EQ(saved["state"]["battleFormOriginalCreature"].String(), "owned:creature1");
	saved["state"]["battleFormRestorationPending"].Bool() = true;
	unit.load(saved);
	EXPECT_TRUE(unit.hasBattleForm());
	EXPECT_EQ(unit.battleFormCreature(), CreatureID(2));
	EXPECT_EQ(unit.battleFormOriginalCreature(), CreatureID(1));
	EXPECT_EQ(unit.getCount(), 3);
	EXPECT_GT(view.changes, 0);
	EXPECT_GT(view.queries, 0);
	unit.endBattleForm();
	EXPECT_EQ(unit.creatureLevel(), 2);
	EXPECT_EQ(unit.getCount(), 3);
}
TEST(OwnedUnitCreatureResolverTest, MissingFormRefusesBeforeChangingCurrentIdentityOrHealth)
{
	Resolver values({descriptor(1)}); Info info(values); GenericView view;
	battle::CUnitStateDetached unit(&info, &view); view.subject = &unit;
	WithoutLibrary absent;
	unit.localInit(nullptr);
	const auto before = unit.save();
	EXPECT_THROW(unit.beginBattleForm(CreatureID(2), 1), std::logic_error);
	EXPECT_EQ(unit.save(), before);
	auto invalid = before;
	invalid["state"]["battleFormCreature"].String() = "unknown";
	invalid["state"]["battleFormRoundsRemaining"].Integer() = 1;
	invalid["state"]["battleFormRestorationPending"].Bool() = true;
	EXPECT_THROW(unit.load(invalid), std::logic_error);
	EXPECT_EQ(unit.save(), before);
}
TEST(OwnedUnitCreatureResolverTest, UnknownNonpendingCurrentAndOriginalKeysPreserveWoundedState)
{
	Resolver values({descriptor(1), descriptor(2)}); Info info(values); GenericView view;
	battle::CUnitStateDetached unit(&info, &view); view.subject = &unit;
	WithoutLibrary absent;
	unit.localInit(nullptr);
	int64_t damage = 5; unit.damage(damage);
	ASSERT_EQ(unit.getAvailableHealth(), 25);
	const auto before = unit.save();
	for(const auto * field : {"battleFormCreature", "battleFormOriginalCreature"})
	{
		auto invalid = before;
		invalid["state"]["battleFormCreature"].String() = "owned:creature2";
		invalid["state"]["battleFormOriginalCreature"].String() = "owned:creature1";
		invalid["state"]["battleFormRoundsRemaining"].Integer() = 1;
		invalid["state"]["battleFormRestorationPending"].Bool() = false;
		invalid["state"][field].String() = "notCaptured";
		EXPECT_THROW(unit.load(invalid), std::logic_error);
		EXPECT_EQ(unit.save(), before);
		EXPECT_EQ(unit.getAvailableHealth(), 25);
		EXPECT_FALSE(unit.hasBattleFormState());
	}
}
