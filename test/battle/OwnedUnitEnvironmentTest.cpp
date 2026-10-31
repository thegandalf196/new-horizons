/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedUnitEnvironment.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/GameLibrary.h"
#include "../mock/mock_battle_Unit.h"
#include <stdexcept>
namespace
{
using Environment = battle::OwnedUnitEnvironment;
using Resolver = battle::OwnedUnitCreatureResolver;
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
Resolver::Creature creature(bool cart)
{
	Resolver::Creature value;
	value.id = CreatureID(cart ? 3 : 1); value.key = cart ? "owned:cart" : "owned:ordinary";
	value.warMachine = cart ? ArtifactID::AMMO_CART : ArtifactID::NONE;
	return value;
}
class OwnedFixtureUnit final : public UnitMock
{
public:
	BonusList raw;
	bool living = true, hypnotized = false;
	OwnedFixtureUnit(uint32_t id, BattleSide side, const CCreature * type, int hex = 50)
	{
		ON_CALL(*this, unitId()).WillByDefault(testing::Return(id));
		ON_CALL(*this, unitSide()).WillByDefault(testing::Return(side));
		ON_CALL(*this, unitOwner()).WillByDefault(testing::Return(PlayerColor(side == BattleSide::ATTACKER ? 0 : 1)));
		ON_CALL(*this, unitSlot()).WillByDefault(testing::Return(SlotID(0)));
		ON_CALL(*this, unitType()).WillByDefault(testing::Return(type));
		ON_CALL(*this, creatureIndex()).WillByDefault(testing::Return(type->getId().getNum()));
		ON_CALL(*this, creatureId()).WillByDefault(testing::Return(type->getId()));
		ON_CALL(*this, isGhost()).WillByDefault(testing::Return(false));
		ON_CALL(*this, isHypnotized()).WillByDefault(testing::Invoke([this]() { return hypnotized; }));
		ON_CALL(*this, alive()).WillByDefault(testing::Invoke([this]() { return living; }));
		ON_CALL(*this, doubleWide()).WillByDefault(testing::Return(false));
		ON_CALL(*this, getPosition()).WillByDefault(testing::Return(BattleHex(hex)));
	}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		auto result = std::make_shared<BonusList>(); raw.getBonuses(*result, selector); result->stackBonuses(); return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		auto result = std::make_shared<BonusList>(); raw.getBonuses(*result, selector); return result;
	}
	int32_t getTreeVersion() const override { return 0; }
	bool isBonusCacheable() const override { return false; }
};
Environment::Facts facts()
{
	Environment::Facts values;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		values.colors.at(side) = PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		values.inputs.at(side).emplace(); values.inputs.at(side)->color = *values.colors.at(side);
		values.states.at(side).emplace();
	}
	values.magicRules.emplace(); // Genuine captured legacy/null rules, not unknown.
	return values;
}
std::shared_ptr<Bonus> bonus(BonusType type, int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}
}
TEST(OwnedUnitEnvironmentTest, FirstMatchingDeadCartAndOccurrenceOrderRemainAuthoritative)
{
	Resolver types({creature(false), creature(true)});
	auto subject = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(CreatureID(1)));
	auto dead = std::make_shared<OwnedFixtureUnit>(2, BattleSide::ATTACKER, types.creature(CreatureID(3))); dead->living = false;
	auto live = std::make_shared<OwnedFixtureUnit>(2, BattleSide::ATTACKER, types.creature(CreatureID(3)));
	auto values = facts(); values.inputs.at(BattleSide::ATTACKER)->ammoArtifact = true;
	Environment deadFirst({subject, dead, live}, values), liveFirst({subject, live, dead}, values);
	WithoutLibrary absent;
	EXPECT_FALSE(deadFirst.unitHasAmmoCart(subject.get()));
	EXPECT_TRUE(liveFirst.unitHasAmmoCart(subject.get()));
}
TEST(OwnedUnitEnvironmentTest, EffectiveOwnerTransfersButAmmoTargetOriginalArmyRuleStaysAsymmetric)
{
	Resolver types({creature(false), creature(true)});
	auto subject = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(CreatureID(1)));
	auto cart = std::make_shared<OwnedFixtureUnit>(2, BattleSide::ATTACKER, types.creature(CreatureID(3)));
	auto values = facts(); values.inputs.at(BattleSide::DEFENDER)->veteranCohesion = true;
	Environment env({subject, cart}, values);
	WithoutLibrary absent;
	EXPECT_EQ(env.unitEffectiveOwner(subject.get()), PlayerColor(0));
	EXPECT_FALSE(env.unitHasVeteranCohesion(subject.get()));
	subject->hypnotized = true;
	EXPECT_EQ(env.unitEffectiveOwner(subject.get()), PlayerColor(1));
	EXPECT_TRUE(env.unitHasVeteranCohesion(subject.get()));
	EXPECT_TRUE(env.unitHasAmmoCart(subject.get())); // cart owner compared with target ORIGINAL side.
}
TEST(OwnedUnitEnvironmentTest, CopiedFortuneAndBloodrageUseCanonicalCurrentStateFormulas)
{
	Resolver types({creature(false)});
	auto subject = std::make_shared<OwnedFixtureUnit>(17, BattleSide::ATTACKER, types.creature(CreatureID(1)));
	auto values = facts(); auto & input = *values.inputs.at(BattleSide::ATTACKER);
	input.bloodrageCap = 100; input.bloodrageSpeed = 3; input.bloodrageRetaliations = 2;
	values.states.at(BattleSide::ATTACKER)->bloodrageDamagePercent = 50;
	values.states.at(BattleSide::ATTACKER)->fortune.speedUnits.insert(17);
	Environment env({subject}, values);
	values.states.at(BattleSide::ATTACKER)->fortune.speedUnits.clear();
	WithoutLibrary absent;
	EXPECT_EQ(env.unitFortuneSpeed(subject.get()), 2);
	EXPECT_EQ(env.unitSpeedBonus(subject.get()), 3);
	EXPECT_EQ(env.unitAdditionalRetaliations(subject.get()), 2);
	env.state(BattleSide::ATTACKER).bloodrageDamagePercent = 49;
	EXPECT_EQ(env.unitSpeedBonus(subject.get()), 0);
	EXPECT_EQ(env.unitAdditionalRetaliations(subject.get()), 0);
	env.state(BattleSide::ATTACKER).fortune.speedUnits.clear();
	EXPECT_EQ(env.unitFortuneSpeed(subject.get()), 0);
}
TEST(OwnedUnitEnvironmentTest, ResistanceReevaluatesOwnedRawValuesAndCopiedRulesWithoutLibrary)
{
	Resolver types({creature(false)});
	auto subject = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(CreatureID(1)), 93);
	auto aura = std::make_shared<OwnedFixtureUnit>(2, BattleSide::ATTACKER, types.creature(CreatureID(1)), 94);
	ASSERT_TRUE(subject->getSurroundingHexes().contains(aura->getPosition())); // Legal interior neighbors, not50/51 across a row boundary.
	auto resistance = bonus(BonusType::MAGIC_RESISTANCE, 80); subject->raw.push_back(resistance);
	aura->raw.push_back(bonus(BonusType::SPELL_RESISTANCE_AURA, 50));
	auto values = facts(); (*values.magicRules)["rulesetVersion"].Integer() = 1;
	Environment env({subject, aura}, values);
	values.magicRules->clear();
	WithoutLibrary absent;
	EXPECT_EQ(env.unitMagicResistance(subject.get()), 75);
	resistance->val = 20;
	EXPECT_EQ(env.unitMagicResistance(subject.get()), 60);
}
TEST(OwnedUnitEnvironmentTest, UnknownPolicyDoesNotTurnIntoZeroAndNativeEarlyReturnsRemainLazy)
{
	Resolver types({creature(false)});
	auto subject = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(CreatureID(1)));
	auto values = facts(); values.inputs.at(BattleSide::ATTACKER).reset();
	Environment env({subject}, values);
	WithoutLibrary absent;
	EXPECT_EQ(env.unitEffectiveOwner(subject.get()), PlayerColor(0));
	EXPECT_THROW(env.unitHasVeteranCohesion(subject.get()), std::logic_error);
	subject->living = false;
	EXPECT_EQ(env.unitSpeedBonus(subject.get()), 0);
	subject->living = true;
	EXPECT_THROW(env.unitSpeedBonus(subject.get()), std::logic_error);
	auto outside = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(CreatureID(1)));
	EXPECT_THROW(env.unitEffectiveOwner(outside.get()), std::invalid_argument);
}
TEST(OwnedUnitEnvironmentTest, RetainedUnitOwnerAndRequiredRulesPreventDanglingOrUnknownConstruction)
{
	Resolver types({creature(false)});
	auto unit = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(CreatureID(1)));
	std::weak_ptr<OwnedFixtureUnit> weak = unit;
	{
		Environment env({unit}, facts()); unit.reset(); EXPECT_FALSE(weak.expired());
	}
	EXPECT_TRUE(weak.expired());
	auto values = facts(); values.magicRules.reset();
	EXPECT_THROW(Environment({}, values), std::invalid_argument);
	values = facts(); values.inputs.at(BattleSide::ATTACKER)->color = PlayerColor(1);
	EXPECT_THROW(Environment({}, values), std::invalid_argument);
}
TEST(OwnedUnitEnvironmentTest, CanonicalCopiedFamilyDelegateNeedsNoLibraryAndRetainsUnknownShortcuts)
{
	auto base = creature(false); base.id = CreatureID::SKELETON; base.key = "owned:skeleton";
	auto upgraded = creature(false); upgraded.id = CreatureID(200); upgraded.key = "owned:upgrade";
	auto unrelated = creature(false); unrelated.id = CreatureID(201); unrelated.key = "owned:other";
	Resolver types({base, upgraded, unrelated});
	auto skeleton = std::make_shared<OwnedFixtureUnit>(1, BattleSide::ATTACKER, types.creature(base.id));
	auto upgrade = std::make_shared<OwnedFixtureUnit>(2, BattleSide::ATTACKER, types.creature(upgraded.id));
	auto other = std::make_shared<OwnedFixtureUnit>(3, BattleSide::ATTACKER, types.creature(unrelated.id));
	auto values = facts(); values.skeletonUpgrades = std::set<CreatureID>{upgraded.id};
	values.inputs.at(BattleSide::ATTACKER)->necromancy.movementSpeed = 4;
	values.inputs.at(BattleSide::ATTACKER)->necromancy.magicResistancePercent = 13;
	Environment captured({skeleton, upgrade, other}, values);
	values.skeletonUpgrades.reset();
	Environment unknown({skeleton, upgrade, other}, values);
	WithoutLibrary absent;
	EXPECT_EQ(captured.unitNecromancyPassives(skeleton.get()).movementSpeed, 4);
	EXPECT_EQ(captured.unitNecromancyPassives(upgrade.get()).magicResistancePercent, 13);
	EXPECT_EQ(captured.unitNecromancyPassives(other.get()).movementSpeed, 0);
	EXPECT_EQ(unknown.unitNecromancyPassives(skeleton.get()).movementSpeed, 4); // exact-base shortcut.
	EXPECT_THROW(unknown.unitNecromancyPassives(upgrade.get()), std::logic_error);
	EXPECT_THROW(unknown.unitNecromancyPassives(other.get()), std::logic_error);
	ON_CALL(*other, creatureId()).WillByDefault(testing::Return(CreatureID::NONE));
	EXPECT_EQ(unknown.unitNecromancyPassives(other.get()).movementSpeed, 0); // invalid-ID shortcut.
}
TEST(OwnedUnitEnvironmentTest, UnknownAndInvalidAffiliationRejectsWhileNeutralObjectAffiliationIsPreserved)
{
	Environment::Facts blank; blank.magicRules.emplace();
	EXPECT_THROW(Environment({}, blank), std::invalid_argument);
	for(const auto color : {PlayerColor::CANNOT_DETERMINE, PlayerColor::SPECTATOR, PlayerColor(42)})
	{
		auto values = facts(); values.colors.at(BattleSide::DEFENDER) = color;
		values.inputs.at(BattleSide::DEFENDER).reset();
		EXPECT_THROW(Environment({}, values), std::invalid_argument);
	}
	for(const auto color : {PlayerColor::NEUTRAL, PlayerColor::UNFLAGGABLE})
	{
		auto values = facts(); values.colors.at(BattleSide::DEFENDER) = color;
		values.inputs.at(BattleSide::DEFENDER)->color = color;
		EXPECT_NO_THROW(Environment({}, values));
	}
}
TEST(OwnedUnitEnvironmentTest, AffiliationPresenceDistinguishesMissingFromActualNeutralValue)
{
	EXPECT_EQ(PlayerColor(), PlayerColor::NEUTRAL); // Actual cpp constants, not stale 252–255 header comments.
	auto values = facts(); values.colors.at(BattleSide::DEFENDER).reset();
	values.inputs.at(BattleSide::DEFENDER).reset();
	EXPECT_THROW(Environment({}, values), std::invalid_argument);
	values.colors.at(BattleSide::DEFENDER) = PlayerColor(); // Explicit genuine neutral, not missing metadata.
	EXPECT_NO_THROW(Environment({}, values));
	values.colors.at(BattleSide::ATTACKER).reset();
	EXPECT_THROW(Environment({}, values), std::invalid_argument);
}
