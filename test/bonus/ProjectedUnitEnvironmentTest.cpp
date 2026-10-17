/*
 * ProjectedUnitEnvironmentTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedUnitEnvironmentFixture.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include <limits>
#include <type_traits>

namespace test
{
using namespace ::testing;
using namespace environmentFixture;
class ProjectedUnitEnvironmentTest : public Test, protected Projection
{
protected:
	void expectAllRefuse(const battle::Unit * unit)
	{
		EXPECT_THROW(environment->unitHasAmmoCart(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitEffectiveOwner(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitMagicResistance(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitFortuneSpeed(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitSpeedBonus(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitAdditionalRetaliations(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitBloodragePainIncrement(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitHasVeteranCohesion(unit), std::invalid_argument);
		EXPECT_THROW(environment->unitMoraleLimits(unit), std::invalid_argument);
	}
};
static_assert(!std::is_abstract_v<battle::ProjectedUnitEnvironment>);
static_assert(!std::is_move_constructible_v<battle::ProjectedUnitEnvironment>);
static_assert(!std::is_copy_constructible_v<battle::ProjectedUnitEnvironment>);

TEST_F(ProjectedUnitEnvironmentTest, EveryCallbackHasLiveParityAndConcreteNondefaultValues)
{
	initialize();
	const auto * unit = units[0].get();
	EXPECT_EQ(environment->unitHasAmmoCart(unit), callback.battleUnitHasAmmoCart(&subject));
	EXPECT_TRUE(environment->unitHasAmmoCart(unit));
	EXPECT_EQ(environment->unitEffectiveOwner(unit), callback.battleGetOwner(&subject));
	EXPECT_EQ(environment->unitEffectiveOwner(unit), PlayerColor(0));
	EXPECT_EQ(environment->unitMagicResistance(unit), live.unitMagicResistance(&subject));
	EXPECT_EQ(unit->magicResistance(), 40);
	EXPECT_EQ(environment->unitFortuneSpeed(unit), callback.battleFortuneSpeed(&subject));
	EXPECT_EQ(environment->unitFortuneSpeed(unit), 2);
	EXPECT_EQ(environment->unitSpeedBonus(unit), callback.battleBloodrageSpeed(&subject));
	EXPECT_EQ(environment->unitSpeedBonus(unit), 3);
	EXPECT_EQ(environment->unitAdditionalRetaliations(unit), callback.battleBloodrageRetaliations(&subject));
	EXPECT_EQ(environment->unitAdditionalRetaliations(unit), 1);
	EXPECT_EQ(environment->unitBloodragePainIncrement(unit), callback.battleBloodragePainIncrement(&subject));
	EXPECT_EQ(environment->unitBloodragePainIncrement(unit), 8);
	EXPECT_EQ(environment->unitHasVeteranCohesion(unit), live.unitHasVeteranCohesion(&subject));
	EXPECT_FALSE(environment->unitHasVeteranCohesion(unit));
	EXPECT_EQ(environment->unitMoraleLimits(unit), live.unitMoraleLimits(&subject));
	ASSERT_TRUE(environment->unitMoraleLimits(unit));
	EXPECT_EQ(*environment->unitMoraleLimits(unit), (std::pair<int32_t, int32_t>{-10, 10}));
}

TEST_F(ProjectedUnitEnvironmentTest, NullPoliciesAreExplicitAndOwnerRequiresItsLegacyNonnullSubject)
{
	initialize();
	EXPECT_FALSE(environment->unitHasAmmoCart(nullptr));
	EXPECT_THROW(environment->unitEffectiveOwner(nullptr), std::invalid_argument);
	EXPECT_EQ(environment->unitMagicResistance(nullptr), std::nullopt);
	EXPECT_EQ(environment->unitFortuneSpeed(nullptr), 0);
	EXPECT_EQ(environment->unitSpeedBonus(nullptr), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(nullptr), 0);
	EXPECT_EQ(environment->unitBloodragePainIncrement(nullptr), 0);
	EXPECT_FALSE(environment->unitHasVeteranCohesion(nullptr));
	EXPECT_EQ(environment->unitMoraleLimits(nullptr), std::nullopt);
}

TEST_F(ProjectedUnitEnvironmentTest, LiveNoBattleDefaultsDoNotDereferenceMissingMetadata)
{
	CPlayerBattleCallback absent(nullptr, PlayerColor(0));
	battle::LiveUnitEnvironmentContext context(absent);
	battle::UnitEnvironmentQueries query(context);
	EXPECT_FALSE(query.unitHasAmmoCart(&subject));
	EXPECT_EQ(query.unitEffectiveOwner(&subject), PlayerColor::CANNOT_DETERMINE);
	EXPECT_EQ(query.unitMagicResistance(&subject), std::nullopt);
	EXPECT_EQ(query.unitFortuneSpeed(&subject), 0);
	EXPECT_EQ(query.unitSpeedBonus(&subject), 0);
	EXPECT_EQ(query.unitAdditionalRetaliations(&subject), 0);
	EXPECT_EQ(query.unitBloodragePainIncrement(&subject), 0);
	EXPECT_FALSE(query.unitHasVeteranCohesion(&subject));
	EXPECT_EQ(query.unitMoraleLimits(&subject), std::nullopt);
}

TEST_F(ProjectedUnitEnvironmentTest, AllNineCallbacksRejectLiveOrForeignSubjectsEvenWhenDead)
{
	initialize();
	expectAllRefuse(&subject);
	battle::CUnitStateDetached outside(&subject, &subject);
	battle::UnitStateValueFields::copy(outside, subject);
	outside.health.reset();
	expectAllRefuse(&outside);
}

TEST_F(ProjectedUnitEnvironmentTest, EnvironmentConstructionRequiresThisGraphExactProvidersForWholeRoster)
{
	initialize();
	ProjectedBonusRoster bad(std::vector<const battle::Unit *>{&subject});
	EXPECT_THROW((battle::ProjectedUnitEnvironment(bad, graph, *state)), std::invalid_argument);
	ProjectedBonusGraph foreign;
	EXPECT_THROW((battle::ProjectedUnitEnvironment(*roster, foreign, *state)), std::invalid_argument);
}

TEST_F(ProjectedUnitEnvironmentTest, PublicProviderReplacementCannotRestoreLiveFallback)
{
	initialize();
	battle::CUnitStateDetached ordinary(&subject, &subject);
	battle::UnitStateValueFields::copy(ordinary, subject);
	*units[0] = ordinary; // same form; deliberately changes provider via public derived assignment
	expectAllRefuse(units[0].get());
}

TEST_F(ProjectedUnitEnvironmentTest, ReplacedAdjacentProviderRefusesEveryCallbackBeforeNestedLiveFallback)
{
	initialize();
	battle::CUnitStateDetached ordinary(&supporter, &supporter);
	battle::UnitStateValueFields::copy(ordinary, supporter);
	*units[3] = ordinary;
	expectAllRefuse(units[0].get());
}

TEST_F(ProjectedUnitEnvironmentTest, FirstDeadCartBlocksLaterLivingCartWithoutSortingOrArtifactFallback)
{
	initialize();
	ASSERT_TRUE(environment->unitHasAmmoCart(units[0].get()));
	units[1]->health.reset();
	ASSERT_TRUE(units[2]->alive());
	EXPECT_FALSE(environment->unitHasAmmoCart(units[0].get()));
	EXPECT_TRUE(callback.battleUnitHasAmmoCart(&subject));
	EXPECT_TRUE(firstCart.alive());
}

TEST_F(ProjectedUnitEnvironmentTest, HypnotizedCartOwnershipIsAsymmetricAndKeepsFirstMatchingDeadPolicy)
{
	initialize();
	graph.addExported(firstCart, bonus(BonusType::HYPNOTIZED, 1));
	ASSERT_EQ(environment->unitEffectiveOwner(units[1].get()), PlayerColor(1));
	EXPECT_TRUE(environment->unitHasAmmoCart(units[0].get()));
	units[2]->health.reset();
	EXPECT_FALSE(environment->unitHasAmmoCart(units[0].get()));
	EXPECT_TRUE(callback.battleUnitHasAmmoCart(&subject));
}

TEST_F(ProjectedUnitEnvironmentTest, HypnosisChangesSideReceiptsButNotRawOwnerAuraOrInitialSideCartMatch)
{
	initialize();
	graph.addExported(subject, bonus(BonusType::HYPNOTIZED, 1));
	EXPECT_EQ(environment->unitEffectiveOwner(units[0].get()), PlayerColor(1));
	EXPECT_EQ(units[0]->unitOwner(), PlayerColor(0));
	EXPECT_EQ(units[0]->magicResistance(), 40);
	EXPECT_TRUE(environment->unitHasAmmoCart(units[0].get()));
	EXPECT_EQ(environment->unitFortuneSpeed(units[0].get()), 0);
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 0);
	EXPECT_EQ(environment->unitBloodragePainIncrement(units[0].get()), 12);
	EXPECT_EQ(callback.battleGetOwner(&subject), PlayerColor(0));
}

TEST_F(ProjectedUnitEnvironmentTest, SequentialPrivateFortuneAndBloodrageReceiptsAreNotFrozenLiveReads)
{
	initialize();
	auto & side = state->state(BattleSide::ATTACKER);
	side.fortune.speedUnits.erase(17);
	side.bloodrageDamagePercent = 0;
	EXPECT_EQ(environment->unitFortuneSpeed(units[0].get()), 0);
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 0);
	side.fortune.speedUnits.insert(17);
	side.bloodrageDamagePercent = 20;
	EXPECT_EQ(environment->unitFortuneSpeed(units[0].get()), 2);
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 3);
	EXPECT_EQ(source.sides.at(BattleSide::ATTACKER).damage, 12);
}

TEST_F(ProjectedUnitEnvironmentTest, PersonalBloodrageThresholdChangesWithoutASecondGlobalHistory)
{
	initialize();
	units[0]->personalBloodrageIncrement = 0;
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 0);
	units[0]->personalBloodrageIncrement = 8;
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 3);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 1);
	EXPECT_EQ(subject.personalBloodrageIncrement, 8);
}

TEST_F(ProjectedUnitEnvironmentTest, DeadGhostBloodrageStopsButExistingFortuneTokenDoesNotInventAliveCheck)
{
	initialize();
	units[0]->health.reset();
	units[0]->ghost = true;
	EXPECT_EQ(environment->unitFortuneSpeed(units[0].get()), 2);
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 0);
	EXPECT_EQ(environment->unitBloodragePainIncrement(units[0].get()), 0);
	EXPECT_TRUE(subject.alive());
}

TEST_F(ProjectedUnitEnvironmentTest, InconsistentPainAndNegativeBonusesRetainExistingInactivePolicy)
{
	auto & side = source.sides.at(BattleSide::ATTACKER);
	side.pain = 5;
	side.speed = -3;
	side.retaliations = -2;
	initialize();
	EXPECT_EQ(environment->unitBloodragePainIncrement(units[0].get()), 0);
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 0);
	EXPECT_EQ(callback.battleBloodragePainIncrement(&subject), 0);
}

TEST_F(ProjectedUnitEnvironmentTest, WideDamageCompositionClampsToCapWithoutOverflow)
{
	source.sides.at(BattleSide::ATTACKER).damage = std::numeric_limits<int32_t>::max();
	subject.personalBloodrageIncrement = 12;
	initialize();
	EXPECT_EQ(callback.battleGetBloodrageDamagePercent(&subject), 40);
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 3);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 1);
}

TEST_F(ProjectedUnitEnvironmentTest, SavedMagicRulesAreOwnedAndCapturedAbsenceRemainsLegacy)
{
	source.rules = JsonNode();
	initialize();
	EXPECT_EQ(environment->unitMoraleLimits(units[0].get()), std::nullopt);
	source.rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_EQ(environment->unitMoraleLimits(units[0].get()), std::nullopt);
	EXPECT_TRUE(live.unitMoraleLimits(&subject));
}

TEST_F(ProjectedUnitEnvironmentTest, MetadataCaptureFailureLeavesOriginalUnitsAndSideReceiptsUntouched)
{
	const auto before = subject.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	source.rejectCapture = true;
	EXPECT_THROW(battle::ProjectedUnitEnvironmentState::capture(source), std::runtime_error);
	EXPECT_EQ(subject.save(), before);
	EXPECT_EQ(source.sides.at(BattleSide::ATTACKER).damage, 12);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedUnitEnvironmentTest, SideColorMappingNotRawOwnerControlsAllegianceButNotResistanceAura)
{
	source.sides.at(BattleSide::ATTACKER).color = PlayerColor(7);
	initialize();
	EXPECT_EQ(environment->unitEffectiveOwner(units[0].get()), PlayerColor(7));
	EXPECT_EQ(units[0]->unitOwner(), PlayerColor(0));
	EXPECT_EQ(units[0]->magicResistance(), 40);
	EXPECT_EQ(environment->unitFortuneSpeed(units[0].get()), 2);
	EXPECT_EQ(callback.battleGetOwner(&subject), PlayerColor(7));
}

TEST_F(ProjectedUnitEnvironmentTest, ZeroCapturedCapKeepsBloodrageInactiveDespitePositivePrivateHistory)
{
	source.sides.at(BattleSide::ATTACKER).cap = 0;
	initialize();
	state->state(BattleSide::ATTACKER).bloodrageDamagePercent = 100;
	EXPECT_EQ(environment->unitSpeedBonus(units[0].get()), 0);
	EXPECT_EQ(environment->unitAdditionalRetaliations(units[0].get()), 0);
	EXPECT_EQ(environment->unitBloodragePainIncrement(units[0].get()), 0);
	EXPECT_EQ(environment->unitFortuneSpeed(units[0].get()), 2);
}
}
