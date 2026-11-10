/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/battle/CommandArmyBonuses.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/spells/MagicalDamageReduction.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/CStack.h"

namespace
{
struct Environment : battle::IUnitEnvironment
{
	int veteran = 0;
	bool unitHasAmmoCart(const battle::Unit *) const override { return false; }
	PlayerColor unitEffectiveOwner(const battle::Unit *) const override { return PlayerColor(0); }
	int unitWarfareStatBonus(const battle::Unit *) const override { return veteran; }
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const battle::Unit *) const override { return std::pair{-10, 10}; }
	std::optional<std::pair<int32_t, int32_t>> unitLuckLimits(const battle::Unit *) const override { return std::pair{-10, 10}; }
};
class CommandArmyBonusesTest : public testing::Test
{
protected:
	UnitInfoMock info;
	BonusBearerMock bonuses;
	Environment environment;
	battle::CUnitStateDetached unit{&info, &bonuses};
	void SetUp() override
	{
		using namespace testing;
		ON_CALL(info, unitBaseAmount()).WillByDefault(Return(10));
		ON_CALL(info, unitType()).WillByDefault(Return(CreatureID(CreatureID::decode("core:pikeman")).toCreature()));
		ON_CALL(info, unitSlot()).WillByDefault(Return(SlotID(0)));
		add(BonusType::STACK_HEALTH, 101, BonusValueType::BASE_NUMBER);
		add(BonusType::STACKS_SPEED, 5);
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
			BonusSource::CREATURE_ABILITY, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK)));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
			BonusSource::CREATURE_ABILITY, 12, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	}
	void add(BonusType type, int value, BonusValueType mode = BonusValueType::ADDITIVE_VALUE)
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::CREATURE_ABILITY, value, BonusSourceID(), BonusSubtypeID(), mode));
	}
	void project(const newHorizonsHeroes::CommandSlotModifiers & modifiers)
	{
		for(auto bonus : commandArmyBonuses::project(modifiers)) bonuses.addNewBonus(std::make_shared<Bonus>(std::move(bonus)));
	}
};
}

TEST_F(CommandArmyBonusesTest, ActualStatsKeepSpeedAndInitiativeSeparate)
{
	newHorizonsHeroes::CommandSlotModifiers modifiers;
	modifiers.enhancements.attack = 3;
	modifiers.enhancements.defense = 4;
	modifiers.enhancements.speed = 2;
	modifiers.authority.initiative = 1;
	project(modifiers);
	unit.localInit(&environment);
	EXPECT_EQ(unit.getAttack(false), 13);
	EXPECT_EQ(unit.getAttack(true), 13);
	EXPECT_EQ(unit.getDefense(false), 16);
	EXPECT_EQ(unit.getMovementRange(), 7u);
	EXPECT_EQ(unit.getInitiative(), 6);
}

TEST_F(CommandArmyBonusesTest, EnduranceAwardsAddBeforeOrdinaryMaximumHealthRounding)
{
	newHorizonsHeroes::CommandSlotModifiers modifiers;
	modifiers.enhancements.maximumHealthPercent = 10 + 20;
	modifiers.authority.maximumHealthPercent = 10;
	project(modifiers);
	unit.localInit(&environment);
	EXPECT_EQ(unit.getMaxHealth(), 141u); // floor(101 * 1.40), not successive percentages.
	EXPECT_EQ(unit.getCount(), 10);
	int64_t damage = 150;
	unit.damage(damage);
	EXPECT_EQ(unit.getCount(), 9);
	EXPECT_EQ(unit.getAvailableHealth(), 1260);
	const auto detached = unit.acquireState();
	EXPECT_EQ(detached->getCount(), 9);
	EXPECT_EQ(detached->getAvailableHealth(), 1260);
}

TEST_F(CommandArmyBonusesTest, WardingIsOneSummedEnhancementSourceAndSeparateAuthoritySource)
{
	newHorizonsHeroes::CommandSlotModifiers modifiers;
	modifiers.enhancements.magicalReductionPercent = 10 + 20;
	modifiers.authority.magicalReductionPercent = 10;
	project(modifiers);
	unit.localInit(&environment);
	std::vector<int> reductions;
	for(const auto & bonus : *unit.getBonuses(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION))) reductions.push_back(bonus->val);
	ASSERT_EQ(reductions.size(), 2u);
	EXPECT_EQ(spells::calculateMagicalDamageReduction(1000, reductions, 0).damageWithPenetration, 630);
	modifiers.enhancements.magicalReductionPercent = 10000;
	const auto capped = commandArmyBonuses::project(modifiers);
	for(const auto & bonus : capped)
		if(bonus.type == BonusType::SPELL_DAMAGE_REDUCTION) EXPECT_LE(bonus.val, 100);
	EXPECT_EQ(spells::calculateMagicalDamageReduction(1000, {100, 10}, 0).damageWithPenetration, 50);
}

TEST_F(CommandArmyBonusesTest, CounterattackCannotOverrideNoRetaliationOrUnlimitedAllowance)
{
	newHorizonsHeroes::CommandSlotModifiers modifiers;
	modifiers.enhancements.retaliations = 2;
	modifiers.authority.retaliations = 1;
	project(modifiers);
	unit.localInit(&environment);
	EXPECT_EQ(unit.counterAttacks.total(), 4);
	add(BonusType::UNLIMITED_RETALIATIONS, 1);
	EXPECT_FALSE(unit.counterAttacks.isLimited());
	add(BonusType::NO_RETALIATION, 1);
	EXPECT_TRUE(unit.counterAttacks.isLimited());
	EXPECT_EQ(unit.counterAttacks.total(), 0);
}

TEST_F(CommandArmyBonusesTest, VeteranContributionBypassesBonusCacheAndRebindsAcrossDetachedBranches)
{
	unit.localInit(&environment);
	const int tree = unit.getTreeVersion();
	EXPECT_EQ(unit.getAttack(false), 10);
	environment.veteran = 2;
	EXPECT_EQ(unit.getTreeVersion(), tree);
	EXPECT_EQ(unit.getAttack(false), 12);
	EXPECT_EQ(unit.getDefenseIgnoringDefensiveStance(false), 14);
	const auto detached = unit.acquireState();
	Environment otherBranch;
	detached->bindStateEnvironment(&otherBranch);
	EXPECT_EQ(detached->getAttack(false), 10);
	EXPECT_EQ(unit.getAttack(false), 12);
	environment.veteran = 0;
	EXPECT_EQ(unit.getAttack(false), 10);
	EXPECT_EQ(unit.getDefenseIgnoringDefensiveStance(false), 12);
}

TEST_F(CommandArmyBonusesTest, FortuneAndCourageRetainFinalLimitsAndImmunities)
{
	newHorizonsHeroes::CommandSlotModifiers modifiers;
	modifiers.enhancements.luck = 20;
	modifiers.enhancements.morale = 20;
	project(modifiers);
	unit.localInit(&environment);
	EXPECT_EQ(unit.luckVal(), 10);
	EXPECT_EQ(unit.moraleVal(), 10);
	add(BonusType::NO_LUCK, 1);
	add(BonusType::NO_MORALE, 1);
	EXPECT_EQ(unit.luckVal(), 0);
	EXPECT_EQ(unit.moraleVal(), 0);
}

TEST(CommandArmyProvenance, SlotNumberOnDescriptorDoesNotManufactureHeroTraining)
{
	CStackBasicDescriptor descriptor(CreatureID(CreatureID::decode("core:pikeman")), 10);
	CStack fabricated(&descriptor, PlayerColor(0), 1, BattleSide::ATTACKER, SlotID(0));
	EXPECT_EQ(commandArmyBonuses::originHero(fabricated), nullptr);
	commandArmyBonuses::initialize(fabricated);
	EXPECT_TRUE(fabricated.getExportedBonusList().empty());
}

TEST(CommandArmyProvenance, UnsupportedStackWriterRejectsFormationBonusesBeforePrefix)
{
	CStackBasicDescriptor descriptor(CreatureID(CreatureID::decode("core:pikeman")), 10);
	CStack stack(&descriptor, PlayerColor(0), 1, BattleSide::ATTACKER, SlotID(0));
	newHorizonsHeroes::CommandSlotModifiers modifiers;
	modifiers.enhancements.attack = 2;
	for(auto bonus : commandArmyBonuses::project(modifiers)) stack.addNewBonus(std::make_shared<Bonus>(std::move(bonus)));
	CMemorySerializer memory;
	memory.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY) - 1);
	EXPECT_THROW(stack.serialize(memory.oser), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}
