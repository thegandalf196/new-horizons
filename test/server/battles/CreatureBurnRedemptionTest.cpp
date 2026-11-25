/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/ConsecratedArmsOrder.h"
#include "../../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../../lib/battle/UnitShieldState.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/battles/BattleCreatureBurn.h"

namespace
{
class CreatureBurnRedemptionTest : public HeroCommandFixture
{
protected:
	CStack * recipient = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:rashka")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName("core:efreet"), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		const auto skills = defenderSideHero->secSkills;
		for(const auto & [skill, rank] : skills)
			defenderSideHero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(consecratedArms::SKILL)),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
				recipient = unit.get();
		ASSERT_NE(recipient, nullptr);
		ASSERT_EQ(recipient->getMaxHealth(), 10);
		ASSERT_EQ(recipient->getBattleStartMaximumAggregateHP(), 1000);
		auto prayer = consecratedArms::orderEffects(*battle(), BattleSide::DEFENDER,
			{recipient->unitId()}, {consecratedArms::Prayer::VICARIOUS_REDEMPTION},
			consecratedArms::capturePolicy(defenderSideHero), 200);
		gameHandler->sendAndApply(prayer);
		ASSERT_EQ(recipient->redemptionShield.capacity, 200);
		recipient->redemptionShield.remaining = 5;
		recipient->guardianSpiritHitPoints = 100;
		recipient->guardianSpiritRoundsRemaining = 3;
		recipient->addNewBonus(std::make_shared<Bonus>(newHorizonsCreatureBurn::marker(
			HeroTypeID(HeroTypeID::decode("core:rashka")), 999999, 20)));
	}
};

TEST_F(CreatureBurnRedemptionTest, ActualPeriodicProducerAbsorbsBeforeBodyThenPaysAfterCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *recipient,
		BattleUnitTurnReason::TURN_QUEUE));
	// Five shield HP absorb first; the remaining fifteen kill one ten-HP creature.
	EXPECT_EQ(recipient->getAvailableHealth(), 985);
	EXPECT_EQ(recipient->getCount(), 99);
	EXPECT_EQ(recipient->guardianSpiritHitPoints, 100);
	// Its casualty grants three HP only after the full tick, so it cannot absorb itself.
	EXPECT_EQ(recipient->redemptionShield.remaining, 3);
	EXPECT_EQ(recipient->redemptionShield.capacity, 200);
	EXPECT_EQ(recipient->getHealth().getCasualtyCount(battle::DamageProvenance::OTHER), 1);
	EXPECT_EQ(recipient->getHealth().getCasualtyCount(battle::DamageProvenance::SPELL), 0);
	EXPECT_EQ(recipient->getHealth().getCasualtyCount(battle::DamageProvenance::MAGICAL_CREATURE), 0);
	EXPECT_FALSE(recipient->hasBonus(CSelector(newHorizonsCreatureBurn::isBurn)));
	EXPECT_FALSE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *recipient,
		BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_EQ(recipient->redemptionShield.remaining, 3);
}
}
