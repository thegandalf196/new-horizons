/*
 * ProjectedUnitEnvironmentHeroInputsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedUnitEnvironmentFixture.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/modding/CModHandler.h"

namespace test
{
using namespace ::testing;
using namespace environmentFixture;
class ProjectedUnitEnvironmentHeroInputsTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped-active New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void veteran()
	{
		const SecondarySkill discipline(SecondarySkill::decode(newHorizonsDiscipline::SKILL));
		ASSERT_GE(discipline.getNum(), 0);
		attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsDiscipline::SKILL, newHorizonsDiscipline::STEADFAST});
		attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({newHorizonsDiscipline::SKILL, newHorizonsDiscipline::VETERAN_COHESION});
		ASSERT_TRUE(newHorizonsDiscipline::hasVeteranCohesion(attackerSideHero));
	}
	JsonNode regularForm(CStack & unit)
	{
		const auto original = unit.save();
		unit.beginBattleForm(CreatureID(0), 2);
		const auto incoming = unit.save();
		unit.load(original);
		return incoming;
	}
};

TEST_F(ProjectedUnitEnvironmentHeroInputsTest, RealSelectedVeteranCapabilityFollowsProjectedAllegiance)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(veteran());
	Projection projected;
	projected.source.sides.at(BattleSide::ATTACKER).hero = attackerSideHero;
	projected.source.sides.at(BattleSide::DEFENDER).hero = defenderSideHero;
	projected.initialize();
	ASSERT_TRUE(projected.state->inputs(BattleSide::ATTACKER).veteranCohesion);
	EXPECT_TRUE(projected.environment->unitHasVeteranCohesion(projected.units[0].get()));
	projected.graph.addExported(projected.subject, bonus(BonusType::HYPNOTIZED, 1));
	EXPECT_FALSE(projected.environment->unitHasVeteranCohesion(projected.units[0].get()));
	EXPECT_TRUE(newHorizonsDiscipline::hasVeteranCohesion(attackerSideHero));
}

TEST_F(ProjectedUnitEnvironmentHeroInputsTest, ActualHeroAmmoArtifactDoesNotOverrideFirstMatchingDeadCart)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	Projection projected;
	projected.source.sides.at(BattleSide::ATTACKER).hero = attackerSideHero;
	projected.initialize();
	ASSERT_TRUE(projected.state->inputs(BattleSide::ATTACKER).ammoArtifact);
	EXPECT_TRUE(projected.environment->unitHasAmmoCart(projected.units[0].get()));
	projected.units[1]->health.reset();
	ASSERT_TRUE(projected.units[2]->alive());
	EXPECT_FALSE(projected.environment->unitHasAmmoCart(projected.units[0].get()));
	EXPECT_TRUE(battle::heroHasAmmoCartArtifact(attackerSideHero));
}

TEST_F(ProjectedUnitEnvironmentHeroInputsTest, ArtifactFallbackAfterSequentialCartFormsUsesCurrentAllegiance)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
	Projection projected;
	projected.source.sides.at(BattleSide::ATTACKER).hero = attackerSideHero;
	projected.source.sides.at(BattleSide::DEFENDER).hero = defenderSideHero;
	const auto first = regularForm(projected.firstCart);
	const auto second = regularForm(projected.secondCart);
	projected.initialize();
	projected.graph.loadBoundState(projected.firstCart, *projected.units[1], first);
	projected.graph.loadBoundState(projected.secondCart, *projected.units[2], second);
	ASSERT_FALSE(projected.units[1]->isAmmoCart());
	ASSERT_FALSE(projected.units[2]->isAmmoCart());
	ASSERT_TRUE(projected.state->inputs(BattleSide::ATTACKER).ammoArtifact);
	ASSERT_FALSE(projected.state->inputs(BattleSide::DEFENDER).ammoArtifact);
	EXPECT_TRUE(projected.environment->unitHasAmmoCart(projected.units[0].get()));
	projected.graph.addExported(projected.subject, bonus(BonusType::HYPNOTIZED, 1));
	EXPECT_FALSE(projected.environment->unitHasAmmoCart(projected.units[0].get()));
	EXPECT_TRUE(projected.firstCart.isAmmoCart());
	EXPECT_TRUE(projected.secondCart.isAmmoCart());
}
}
