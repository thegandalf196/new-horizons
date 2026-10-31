/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsPrismaticVanguard.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
namespace
{
class PrismaticVanguardCoreOnlyTest : public BattleTestFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the core-only test profile";
		startGame(); startBattle();
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr);
	}
	Bonus marker(bool active)
	{
		Bonus result(active ? BonusDuration::STACK_ACTIVATION : BonusDuration::ONE_BATTLE,
			BonusType::NONE, BonusSource::HERO_SPECIAL, 20,
			BonusSourceID(HeroTypeID(HeroTypeID::decode("core:monere"))),
			BonusSubtypeID(), BonusValueType::BASE_NUMBER);
		result.stacking = "new-horizons:ordinary-creature-specialty:core:monere:prismaticVanguard";
		result.statusIdentity = active ? "prismaticVanguard" : "prismaticVanguardPending";
		result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
		return result;
	}
};
}
TEST_F(PrismaticVanguardCoreOnlyTest, RealOrdinaryPhysicalDamageAndHistoricalMonereEntryNeedNoOptionalIdentifier)
{
	BattleAttackInfo hit(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsPrismaticVanguard::activeDamagePercent(hit), 0);
	EXPECT_NO_THROW(battle()->battleEstimateDamage(hit));
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:monere")));
	std::optional<Bonus> entry;
	EXPECT_NO_THROW(entry = newHorizonsPrismaticVanguard::entryBonus(attackerSideHero, actor->creatureId()));
	EXPECT_FALSE(entry);
}
TEST_F(PrismaticVanguardCoreOnlyTest, ReservedMetadataOnOrdinaryCoreBodyNeverResolvesOrPretendsToBeOptionalWisp)
{
	actor->addNewBonus(std::make_shared<Bonus>(marker(false)));
	const auto plan = newHorizonsPrismaticVanguard::activationPlan(*battle(), *actor, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(plan.remove.size(), 1u); EXPECT_TRUE(plan.add.empty());
	actor->removeBonuses(CSelector(newHorizonsPrismaticVanguard::isEffect));
	actor->addNewBonus(std::make_shared<Bonus>(marker(true)));
	BattleAttackInfo hit(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsPrismaticVanguard::activeDamagePercent(hit), 0);
	EXPECT_NO_THROW(battle()->battleEstimateDamage(hit));
	actor->beginBattleForm(creatureByName("core:psychicElemental"), 2);
	EXPECT_EQ(newHorizonsPrismaticVanguard::activeDamagePercent(hit), 0);
	EXPECT_NO_THROW(battle()->battleEstimateDamage(hit));
}
