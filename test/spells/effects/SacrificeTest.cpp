/*
 * SacrificeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "EffectFixture.h"

#include <vstd/RNG.h>

#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/json/JsonNode.h"

#include "mock/mock_UnitEnvironment.h"

namespace test
{
using namespace ::spells;
using namespace ::spells::effects;
using namespace ::testing;

class PhantomUnitFake final : public battle::UnitFake
{
public:
	int64_t getPhantomInitialIntegrity() const override
	{
		return 1;
	}
};

void expectHeroCasterByDefault(EffectFixture & fixture)
{
	EXPECT_CALL(fixture.mechanicsMock, getUnitCaster()).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
}

class SacrificeTest : public Test, public EffectFixture
{
public:

	SacrificeTest()
		: EffectFixture("core:sacrifice")
	{
	}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		expectHeroCasterByDefault(*this);

		{
			JsonNode config;
			config["healLevel"].String() = "resurrect";
			EffectFixture::setupEffect(config);
		}
	}
};

TEST_F(SacrificeTest, HexTransformedPrimaryMustNotSubstituteAnotherCorpse)
{
	auto & first = unitsFake.add(BattleSide::ATTACKER);
	auto & selected = unitsFake.add(BattleSide::ATTACKER);
	ON_CALL(first, unitId()).WillByDefault(Return(1u));
	ON_CALL(selected, unitId()).WillByDefault(Return(2u));
	const BattleHex corpseHex(5, 5);
	for(auto * corpse : {&first, &selected})
	{
		ON_CALL(*corpse, alive()).WillByDefault(Return(false));
		EXPECT_CALL(*corpse, getPosition()).WillRepeatedly(Return(corpseHex));
		EXPECT_CALL(*corpse, isValidTarget(Eq(false))).WillRepeatedly(Return(false));
		EXPECT_CALL(*corpse, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
		ON_CALL(*corpse, getTotalHealth()).WillByDefault(Return(200));
		ON_CALL(*corpse, getAvailableHealth()).WillByDefault(Return(0));
	}
	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	ON_CALL(victim, unitId()).WillByDefault(Return(3u));
	ON_CALL(victim, alive()).WillByDefault(Return(true));
	EXPECT_CALL(victim, alive()).Times(0);
	EXPECT_CALL(victim, getPosition()).WillRepeatedly(Return(BattleHex(5, 10)));
	EXPECT_CALL(victim, isValidTarget(_)).WillRepeatedly(Return(true));
	ON_CALL(victim, getTotalHealth()).WillByDefault(Return(300));
	ON_CALL(victim, getAvailableHealth()).WillByDefault(Return(300));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isMassive()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isNegativeSpell()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, alwaysHitFirstTarget()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&selected)))
		.WillOnce(Return(true))
		.WillOnce(Return(false))
		.WillOnce(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&selected)))
		.WillOnce(Return(true))
		.WillOnce(Return(false));
	Target aimPoint{Destination(&selected, corpseHex), Destination(&victim, victim.getPosition())};
	Target spellTarget{Destination(corpseHex)};
	const auto transformed = subject->transformTarget(&mechanicsMock, aimPoint, spellTarget);
	ASSERT_EQ(transformed.size(), 2u);
	EXPECT_EQ(transformed[0].unitValue, &selected);
	EXPECT_EQ(transformed[1].unitValue, &victim);

	// An explicit rejected source must not fall back to its eligible neighbour.
	ON_CALL(mechanicsMock, isReceptive(Eq(&selected))).WillByDefault(Return(false));
	EXPECT_TRUE(subject->transformTarget(&mechanicsMock, aimPoint, spellTarget).empty());
	ON_CALL(mechanicsMock, isReceptive(Eq(&selected))).WillByDefault(Return(true));
	ON_CALL(mechanicsMock, ownerMatches(Eq(&selected))).WillByDefault(Return(false));
	EXPECT_TRUE(subject->transformTarget(&mechanicsMock, aimPoint, spellTarget).empty());
}

TEST_F(SacrificeTest, ApplicableForTwoTargets)
{
	auto & unit = unitsFake.add(BattleSide::ATTACKER);

	EXPECT_CALL(unit, getPosition()).WillRepeatedly(Return(BattleHex(5,5)));
	EXPECT_CALL(unit, isValidTarget(Eq(false))).WillRepeatedly(Return(false));
	EXPECT_CALL(unit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	EXPECT_CALL(unit, getTotalHealth()).WillRepeatedly(Return(0));
	EXPECT_CALL(unit, getAvailableHealth()).WillRepeatedly(Return(200));

	auto & victim = unitsFake.add(BattleSide::ATTACKER);

	victim.makeAlive();

	EXPECT_CALL(victim, getPosition()).WillRepeatedly(Return(BattleHex(5,10)));
	EXPECT_CALL(victim, isValidTarget(_)).WillRepeatedly(Return(true));
	EXPECT_CALL(victim, getTotalHealth()).WillRepeatedly(Return(300));
	EXPECT_CALL(victim, getAvailableHealth()).WillRepeatedly(Return(300));

	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isMassive()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isNegativeSpell()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, alwaysHitFirstTarget()).WillRepeatedly(Return(false));

	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&unit))).Times(AtLeast(1)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&unit))).Times(AtLeast(1)).WillRepeatedly(Return(true));

	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&victim))).Times(AtLeast(1)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&victim))).Times(AtLeast(1)).WillRepeatedly(Return(true));


	Target aimPoint;
	aimPoint.emplace_back(&unit, BattleHex());
	aimPoint.emplace_back(&victim, BattleHex());

	Target spellTarget;
	spellTarget.emplace_back(&unit, BattleHex());

	Target transformed = subject->transformTarget(&mechanicsMock, aimPoint, spellTarget);

	EXPECT_TRUE(subject->applicableTarget(problemMock, &mechanicsMock, transformed));
}

TEST_F(SacrificeTest, TransformTargetPreservesPhantomVictimForPairRejection)
{
	auto & deadUnit = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadUnit, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(deadUnit, unitId()).WillRepeatedly(Return(1u));
	EXPECT_CALL(deadUnit, getPosition()).WillRepeatedly(Return(BattleHex(5, 5)));
	EXPECT_CALL(deadUnit, isValidTarget(Eq(false))).WillRepeatedly(Return(false));
	EXPECT_CALL(deadUnit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(deadUnit, getTotalHealth()).WillRepeatedly(Return(200));
	EXPECT_CALL(deadUnit, getAvailableHealth()).WillRepeatedly(Return(0));

	auto phantomVictim = std::make_shared<PhantomUnitFake>();
	phantomVictim->makeAlive();
	phantomVictim->redirectBonusesToFake();
	EXPECT_CALL(*phantomVictim, getPosition()).WillRepeatedly(Return(BattleHex(5, 10)));
	unitsFake.allUnits.emplace_back(phantomVictim);

	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isMassive()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isNegativeSpell()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, alwaysHitFirstTarget()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, ownerMatches(_)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).WillRepeatedly(Return(true));

	Target aimPoint{Destination(&deadUnit, BattleHex(5, 5)), Destination(phantomVictim.get(), BattleHex(5, 10))};
	Target spellTarget{Destination(&deadUnit, BattleHex(5, 5))};
	const auto transformed = subject->transformTarget(&mechanicsMock, aimPoint, spellTarget);

	ASSERT_EQ(transformed.size(), 2u);
	EXPECT_EQ(transformed.front().unitValue, &deadUnit);
	EXPECT_EQ(transformed.back().unitValue, phantomVictim.get());
	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, transformed));
}

#if 0

TEST_F(SacrificeTest, NotApplicableWithoutVictim)
{
	{
		JsonNode config(JsonNode::JsonType::DATA_STRUCT);
		config["healLevel"].String() = "resurrect";
		EffectFixture::setupEffect(config);
	}

	auto & unit = unitsFake.add(BattleSide::ATTACKER);

	EXPECT_CALL(unit, getPosition()).WillRepeatedly(Return(BattleHex(5,5)));
	EXPECT_CALL(unit, isValidTarget(Eq(false))).WillRepeatedly(Return(false));
	EXPECT_CALL(unit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	EXPECT_CALL(unit, getTotalHealth()).WillRepeatedly(Return(0));
	EXPECT_CALL(unit, getAvailableHealth()).WillRepeatedly(Return(200));

	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isMassive()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, alwaysHitFirstTarget()).WillRepeatedly(Return(false));

	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&unit))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&unit))).WillRepeatedly(Return(true));

	Target aimPoint;
	aimPoint.emplace_back(&unit, BattleHex());

	Target transformed = subject->transformTarget(&mechanicsMock, aimPoint, aimPoint);

	EXPECT_FALSE(subject->applicable(problemMock, &mechanicsMock, transformed));
}

#endif

struct AdjustTargetTypesParam
{
	std::vector<TargetType> input;
	std::vector<TargetType> expected;
};

class SacrificeAdjustTargetTypesTest : public TestWithParam<AdjustTargetTypesParam>, public EffectFixture
{
public:
	SacrificeAdjustTargetTypesTest() : EffectFixture("core:sacrifice") {}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		JsonNode config;
		config["healLevel"].String() = "resurrect";
		EffectFixture::setupEffect(config);
	}
};

TEST_P(SacrificeAdjustTargetTypesTest, AdjustsCorrectly)
{
	auto types = GetParam().input;
	subject->adjustTargetTypes(types, &mechanicsMock);
	EXPECT_EQ(types, GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(
	Cases,
	SacrificeAdjustTargetTypesTest,
	Values(
		AdjustTargetTypesParam{{}, {}},
		AdjustTargetTypesParam{{AimType::LOCATION}, {}},
		AdjustTargetTypesParam{{AimType::CREATURE}, {AimType::CREATURE, AimType::CREATURE}},
		AdjustTargetTypesParam{{AimType::CREATURE, AimType::CREATURE}, {AimType::CREATURE, AimType::CREATURE}},
		AdjustTargetTypesParam{{AimType::CREATURE, AimType::LOCATION}, {}}
	)
);

class SacrificeApplicableGeneralTest : public Test, public EffectFixture
{
public:
	SacrificeApplicableGeneralTest() : EffectFixture("core:sacrifice") {}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		expectHeroCasterByDefault(*this);
		JsonNode config;
		config["healLevel"].String() = "resurrect";
		EffectFixture::setupEffect(config);
	}
};

TEST_F(SacrificeApplicableGeneralTest, ReturnsFalseWhenNoValidTargets)
{
	EXPECT_CALL(mechanicsMock, adaptProblem(_, _)).WillOnce(Return(false));
	EXPECT_FALSE(subject->applicableGeneral(problemMock, &mechanicsMock));
}

TEST_F(SacrificeApplicableGeneralTest, ReturnsFalseWhenNoDeadUnits)
{
	auto & unit = unitsFake.add(BattleSide::ATTACKER);
	unit.makeAlive();
	EXPECT_CALL(unit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(unit, isInvincible()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&unit))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&unit))).WillRepeatedly(Return(true));

	EXPECT_CALL(mechanicsMock, adaptProblem(_, _)).WillOnce(Return(false));
	EXPECT_FALSE(subject->applicableGeneral(problemMock, &mechanicsMock));
}

TEST_F(SacrificeApplicableGeneralTest, ReturnsTrueWithDeadAndAliveUnits)
{
	auto & deadUnit = unitsFake.add(BattleSide::ATTACKER);
	deadUnit.makeDead();
	EXPECT_CALL(deadUnit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&deadUnit))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&deadUnit))).WillRepeatedly(Return(true));

	auto & aliveUnit = unitsFake.add(BattleSide::ATTACKER);
	aliveUnit.makeAlive();
	EXPECT_CALL(aliveUnit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&aliveUnit))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&aliveUnit))).WillRepeatedly(Return(true));

	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));

	EXPECT_TRUE(subject->applicableGeneral(problemMock, &mechanicsMock));
}

TEST_F(SacrificeApplicableGeneralTest, PhantomCorpseDoesNotMakeTheSpellApplicable)
{
	auto phantomCorpse = std::make_shared<PhantomUnitFake>();
	phantomCorpse->redirectBonusesToFake();
	unitsFake.allUnits.emplace_back(phantomCorpse);

	auto & aliveUnit = unitsFake.add(BattleSide::ATTACKER);
	aliveUnit.makeAlive();
	EXPECT_CALL(aliveUnit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&aliveUnit))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&aliveUnit))).WillRepeatedly(Return(true));

	EXPECT_CALL(mechanicsMock, adaptProblem(_, _)).WillOnce(Return(false));
	EXPECT_FALSE(subject->applicableGeneral(problemMock, &mechanicsMock));
}

TEST_F(SacrificeApplicableGeneralTest, PhantomUnitCasterCannotMakeSacrificeApplicable)
{
	auto phantomCaster = std::make_shared<PhantomUnitFake>();
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(AtLeast(1)).WillRepeatedly(Return(phantomCaster.get()));
	EXPECT_CALL(mechanicsMock, adaptProblem(_, _)).WillOnce(Return(false));
	EXPECT_CALL(*battleFake, getUnitsIf(_)).Times(0);

	EXPECT_FALSE(subject->applicableGeneral(problemMock, &mechanicsMock));

	auto & corpse = unitsFake.add(BattleSide::ATTACKER);
	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	Target pair{Destination(&corpse, BattleHex()), Destination(&victim, BattleHex())};
	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, pair));
	EXPECT_TRUE(subject->transformTarget(&mechanicsMock, pair, {pair.front()}).empty());
}

class SacrificeApplicableTargetNegativeTest : public Test, public EffectFixture
{
public:
	SacrificeApplicableTargetNegativeTest() : EffectFixture("core:sacrifice") {}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		expectHeroCasterByDefault(*this);
		JsonNode config;
		config["healLevel"].String() = "resurrect";
		EffectFixture::setupEffect(config);
	}
};

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenTargetIsAlive)
{
	auto & unit = unitsFake.add(BattleSide::ATTACKER);
	unit.makeAlive();
	EXPECT_CALL(unit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));

	Target target;
	target.emplace_back(&unit, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenVictimIsDead)
{
	auto & deadTarget = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadTarget, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(deadTarget, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));

	auto & deadVictim = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadVictim, alive()).WillRepeatedly(Return(false));

	Target target;
	target.emplace_back(&deadTarget, BattleHex());
	target.emplace_back(&deadVictim, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenCorpseIsAlsoSelectedAsVictim)
{
	auto & corpse = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(corpse, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(corpse, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	Target target;
	target.emplace_back(&corpse, BattleHex());
	target.emplace_back(&corpse, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenVictimNotReceptive)
{
	auto & deadTarget = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadTarget, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(deadTarget, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));

	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	victim.makeAlive();
	EXPECT_CALL(victim, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&victim))).WillRepeatedly(Return(false));

	Target target;
	target.emplace_back(&deadTarget, BattleHex());
	target.emplace_back(&victim, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenVictimIsHostile)
{
	auto & deadTarget = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadTarget, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(deadTarget, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	auto & victim = unitsFake.add(BattleSide::DEFENDER);
	victim.makeAlive();
	EXPECT_CALL(victim, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&victim))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&victim))).WillRepeatedly(Return(false));

	Target target;
	target.emplace_back(&deadTarget, BattleHex());
	target.emplace_back(&victim, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenVictimIsRemovedOrGhosted)
{
	auto & deadTarget = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadTarget, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(deadTarget, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	auto & removedVictim = unitsFake.add(BattleSide::ATTACKER);
	removedVictim.makeAlive();
	EXPECT_CALL(removedVictim, isValidTarget(Eq(true))).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));

	Target target;
	target.emplace_back(&deadTarget, BattleHex());
	target.emplace_back(&removedVictim, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenCorpseTargetIsPhantom)
{
	auto phantomCorpse = std::make_shared<PhantomUnitFake>();
	EXPECT_CALL(*phantomCorpse, alive()).WillRepeatedly(Return(false));
	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	ON_CALL(victim, alive()).WillByDefault(Return(true));
	EXPECT_CALL(victim, alive()).Times(0);

	Target target;
	target.emplace_back(phantomCorpse.get(), BattleHex());
	target.emplace_back(&victim, BattleHex());

	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

TEST_F(SacrificeApplicableTargetNegativeTest, ReturnsFalseWhenSacrificeVictimIsPhantom)
{
	auto & deadTarget = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(deadTarget, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(deadTarget, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	auto phantomVictim = std::make_shared<PhantomUnitFake>();
	phantomVictim->makeAlive();

	Target target;
	target.emplace_back(&deadTarget, BattleHex());
	target.emplace_back(phantomVictim.get(), BattleHex());

	EXPECT_CALL(mechanicsMock, isSmart()).WillRepeatedly(Return(false));
	EXPECT_FALSE(subject->applicableTarget(problemMock, &mechanicsMock, target));
}

class SacrificeGetHealthChangeTest : public Test, public EffectFixture
{
public:
	UnitEnvironmentMock unitEnvironmentMock;

	SacrificeGetHealthChangeTest() : EffectFixture("core:sacrifice") {}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		expectHeroCasterByDefault(*this);
		JsonNode config;
		config["healLevel"].String() = "resurrect";
		config["healPower"].String() = "permanent";
		EffectFixture::setupEffect(config);
	}
};

TEST_F(SacrificeGetHealthChangeTest, EmptyTargetReturnsZero)
{
	Target target;
	auto result = subject->getHealthChange(&mechanicsMock, target);

	EXPECT_EQ(result.hpDelta, 0);
	EXPECT_EQ(result.unitsDelta, 0);
	EXPECT_EQ(result.unitType, nullptr);
}

TEST_F(SacrificeGetHealthChangeTest, PhantomTargetHasNoResurrectionPreview)
{
	auto phantom = std::make_shared<PhantomUnitFake>();
	Target target;
	target.emplace_back(phantom.get(), BattleHex());

	const auto result = subject->getHealthChange(&mechanicsMock, target);

	EXPECT_EQ(result.hpDelta, 0);
	EXPECT_EQ(result.unitsDelta, 0);
	EXPECT_EQ(result.unitType, nullptr);
}

TEST_F(SacrificeGetHealthChangeTest, PhantomUnitCasterHasNoResurrectionPreview)
{
	auto phantomCaster = std::make_shared<PhantomUnitFake>();
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(1).WillOnce(Return(phantomCaster.get()));

	auto & corpse = unitsFake.add(BattleSide::ATTACKER);
	Target target;
	target.emplace_back(&corpse, BattleHex());

	const auto result = subject->getHealthChange(&mechanicsMock, target);

	EXPECT_EQ(result.hpDelta, 0);
	EXPECT_EQ(result.unitsDelta, 0);
	EXPECT_EQ(result.unitType, nullptr);
}

TEST_F(SacrificeGetHealthChangeTest, DeadTargetReturnsMaxResurrectionValue)
{
	const int32_t baseAmount = 5;
	const int32_t unitHP = 200;
	const uint32_t unitId = 42;

	auto & unit = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(unit, alive()).WillRepeatedly(Return(false));
	EXPECT_CALL(unit, unitBaseAmount()).WillRepeatedly(Return(baseAmount));
	EXPECT_CALL(unit, creatureId()).WillRepeatedly(Return(CreatureID(unitId)));
	unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, unitHP, BonusSourceID()));
	unitsFake.setDefaultBonusExpectations();

	Target target;
	target.emplace_back(&unit, BattleHex());

	auto result = subject->getHealthChange(&mechanicsMock, target);

	EXPECT_EQ(result.hpDelta, (int64_t)baseAmount * unitHP);
	EXPECT_EQ(result.unitsDelta, (int64_t)baseAmount);
	EXPECT_EQ(result.unitType, static_cast<const Creature *>(CreatureID(unitId).toCreature()));
}

TEST_F(SacrificeGetHealthChangeTest, AliveTargetReturnsCalculatedHealWithNegativeUnitsDelta)
{
	const int32_t effectPower = 10;
	const int64_t rawEffectValue = 500;
	const int32_t victimCount = 3;
	const int32_t victimHP = 100;
	const uint32_t unitId = 42;

	const int64_t expectedHpDelta = (effectPower + victimHP + rawEffectValue) * victimCount;

	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	victim.makeAlive();
	EXPECT_CALL(victim, getCount()).WillRepeatedly(Return(victimCount));
	EXPECT_CALL(victim, creatureId()).WillRepeatedly(Return(CreatureID(unitId)));
	victim.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, victimHP, BonusSourceID()));
	unitsFake.setDefaultBonusExpectations();

	EXPECT_CALL(mechanicsMock, getEffectPower()).WillRepeatedly(Return(effectPower));
	EXPECT_CALL(mechanicsMock, calculateRawEffectValue(Eq(0), Eq(1))).WillRepeatedly(Return(rawEffectValue));

	Target target;
	target.emplace_back(&victim, BattleHex());

	auto result = subject->getHealthChange(&mechanicsMock, target);

	EXPECT_EQ(result.hpDelta, expectedHpDelta);
	EXPECT_EQ(result.unitsDelta, -(int64_t)victimCount);
	EXPECT_EQ(result.unitType, static_cast<const Creature *>(CreatureID(unitId).toCreature()));
}

class SacrificeApplyTest : public Test, public EffectFixture
{
public:
	UnitEnvironmentMock unitEnvironmentMock;

	SacrificeApplyTest()
		: EffectFixture("core:sacrifice")
	{
	}

	void setDefaultExpectations()
	{

	}

protected:
	void SetUp() override
	{
		EffectFixture::setUp();
		expectHeroCasterByDefault(*this);

		{
			JsonNode config;
			config["healLevel"].String() = "resurrect";
			config["healPower"].String() = "permanent";
			EffectFixture::setupEffect(config);
		}
	}
};

TEST_F(SacrificeApplyTest, ResurrectsTarget)
{
	using namespace ::battle;

	const int32_t effectPower = 10;
	const int64_t effectValue = 500;
	const uint32_t unitId = 42;
	const int32_t unitAmount = 100;
	const int32_t unitHP = 1000;

	const uint32_t victimId = 4242;
	const int32_t victimCount = 5;
	const int32_t victimUnitHP = 100;
	const auto pikeman = CreatureID(unitId).toCreature();

	const int64_t expectedHealValue = (effectPower + victimUnitHP + effectValue) * victimCount;

	auto & targetUnit = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(targetUnit, alive()).Times(AtLeast(1)).WillRepeatedly(Return(false));
	EXPECT_CALL(targetUnit, isGhost()).Times(0);
	EXPECT_CALL(targetUnit, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(targetUnit, unitBaseAmount()).WillRepeatedly(Return(unitAmount));
	EXPECT_CALL(targetUnit, unitId()).WillRepeatedly(Return(unitId));
	EXPECT_CALL(targetUnit, unitType()).WillRepeatedly(Return(pikeman));
	EXPECT_CALL(targetUnit, creatureId()).WillRepeatedly(Return(CreatureID(unitId)));

	EXPECT_CALL(mechanicsMock, getEffectPower()).Times(AtLeast(1)).WillRepeatedly(Return(effectPower));
	// NOTE: seems to be incorrect assumption? Sacrifice is not affected by Sorcery, which is what applySpellBonus does
	//EXPECT_CALL(mechanicsMock, applySpellBonus(_, Eq(&targetUnit))).WillOnce(ReturnArg<0>());
	EXPECT_CALL(mechanicsMock, calculateRawEffectValue(_,_)).WillOnce(Return(effectValue));

	targetUnit.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, unitHP, BonusSourceID()));

	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	victim.makeAlive();
	EXPECT_CALL(victim, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(Eq(&victim))).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(Eq(&victim))).WillRepeatedly(Return(true));
	EXPECT_CALL(victim, unitId()).Times(AtLeast(1)).WillRepeatedly(Return(victimId));
	EXPECT_CALL(victim, getCount()).Times(AtLeast(1)).WillRepeatedly(Return(victimCount));

	victim.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, victimUnitHP, BonusSourceID()));

	EXPECT_CALL(*battleFake, updateUnit(Eq(unitId), _, Eq(expectedHealValue))).Times(1);

	EXPECT_CALL(*battleFake, removeUnit(Eq(victimId))).Times(1);

	unitsFake.setDefaultBonusExpectations();

	std::shared_ptr<CUnitState> targetUnitState = std::make_shared<CUnitStateDetached>(&targetUnit, &targetUnit);
	targetUnitState->localInit(&unitEnvironmentMock);
	{
		int64_t initialDmg = targetUnitState->getAvailableHealth();
		targetUnitState->damage(initialDmg);
	}

	EXPECT_CALL(targetUnit, acquire()).WillOnce(Return(targetUnitState));

	EXPECT_CALL(serverMock, apply(Matcher<BattleUnitsChanged &>(_))).Times(AtLeast(1));
	EXPECT_CALL(serverMock, apply(Matcher<BattleLogMessage &>(_))).Times(AtLeast(1));

	setupDefaultRNG();

	Target target;
	target.emplace_back(&targetUnit, BattleHex());
	target.emplace_back(&victim, BattleHex());

	subject->apply(&serverMock, &mechanicsMock, target);

	EXPECT_EQ(targetUnitState->getAvailableHealth(), expectedHealValue);
}

TEST_F(SacrificeApplyTest, RejectsPhantomAsEitherTargetBeforeServerChanges)
{
	auto phantomCorpse = std::make_shared<PhantomUnitFake>();
	EXPECT_CALL(*phantomCorpse, alive()).Times(AtLeast(1)).WillRepeatedly(Return(false));
	EXPECT_CALL(*phantomCorpse, isGhost()).Times(0);
	auto & ordinaryVictim = unitsFake.add(BattleSide::ATTACKER);
	ON_CALL(ordinaryVictim, alive()).WillByDefault(Return(true));
	EXPECT_CALL(ordinaryVictim, alive()).Times(0);
	Target target;
	target.emplace_back(phantomCorpse.get(), BattleHex());
	target.emplace_back(&ordinaryVictim, BattleHex());
	subject->apply(&serverMock, &mechanicsMock, target);

	auto & ordinaryCorpse = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(ordinaryCorpse, alive()).Times(AtLeast(1)).WillRepeatedly(Return(false));
	EXPECT_CALL(ordinaryCorpse, isGhost()).Times(0);
	EXPECT_CALL(ordinaryCorpse, isValidTarget(Eq(true))).WillRepeatedly(Return(true));
	auto phantomVictim = std::make_shared<PhantomUnitFake>();
	phantomVictim->makeAlive();
	target[0].unitValue = &ordinaryCorpse;
	target[1].unitValue = phantomVictim.get();
	subject->apply(&serverMock, &mechanicsMock, target);
}

TEST_F(SacrificeApplyTest, RejectsRemovedVictimBeforeServerChanges)
{
	auto & corpse = unitsFake.add(BattleSide::ATTACKER);
	EXPECT_CALL(corpse, alive()).Times(AtLeast(1)).WillRepeatedly(Return(false));
	EXPECT_CALL(corpse, isGhost()).Times(0);
	EXPECT_CALL(corpse, isValidTarget(Eq(true))).WillRepeatedly(Return(true));

	auto & removedVictim = unitsFake.add(BattleSide::ATTACKER);
	removedVictim.makeAlive();
	EXPECT_CALL(removedVictim, isValidTarget(Eq(true))).WillRepeatedly(Return(false));

	Target target;
	target.emplace_back(&corpse, BattleHex());
	target.emplace_back(&removedVictim, BattleHex());
	subject->apply(&serverMock, &mechanicsMock, target);
}

TEST_F(SacrificeApplyTest, PhantomUnitCasterCannotResurrectOrConsumeCreatures)
{
	auto phantomCaster = std::make_shared<PhantomUnitFake>();
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(1).WillOnce(Return(phantomCaster.get()));
	EXPECT_CALL(*battleFake, updateUnit(_, _, _)).Times(0);
	EXPECT_CALL(*battleFake, removeUnit(_)).Times(0);

	auto & corpse = unitsFake.add(BattleSide::ATTACKER);
	auto & victim = unitsFake.add(BattleSide::ATTACKER);
	Target target;
	target.emplace_back(&corpse, BattleHex());
	target.emplace_back(&victim, BattleHex());

	subject->apply(&serverMock, &mechanicsMock, target);
}

}
