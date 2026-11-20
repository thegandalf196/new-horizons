/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/MetamagicDoubleCast.h"

namespace heroQueueTest
{
namespace
{
class MetamagicDoubleCastAITest : public HeroQueueSelectorTest
{
protected:
	void prepare(int metamagicRank)
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world()); // Books Cure.
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
		if(metamagicRank)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				metamagicRank, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
		// A frozen, badly wounded Angel makes Cure a follow-up the AI values.
		ASSERT_NO_FATAL_FAILURE(injure(actor, actor->getAvailableHealth() - 1000));
		ASSERT_NO_FATAL_FAILURE(freeze(actor));
	}
};
}

static bool submittedSpell(const QueueCallback & callback)
{
	return std::any_of(callback.heroRequests.begin(), callback.heroRequests.end(), [](const BattleAction & action)
	{
		return action.actionType == EActionType::HERO_SPELL;
	});
}

TEST_F(MetamagicDoubleCastAITest, CastsItsHeroActionSpellWhenNoOfferIsOpen)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(submittedSpell(*callback)) << "Control: Cure is a valued Hero Action spell";
}

TEST_F(MetamagicDoubleCastAITest, DeclinesTheOpenOfferWithoutSubmittingASpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(attackerSideHero, SpellID::BLESS, active));
	ASSERT_TRUE(metamagicDoubleCast::offerGranted(battle()->getHeroActionAllowances(BattleSide::ATTACKER)));
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	ASSERT_NO_FATAL_FAILURE(characterize(false));
	EXPECT_FALSE(submittedSpell(*callback)) << "The valued Cure follow-up is declined until phase 6";
	// Declining is free: the creature action that follows forfeits the offer without spending a use.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(active)));
	EXPECT_FALSE(metamagicDoubleCast::offerGranted(battle()->getHeroActionAllowances(BattleSide::ATTACKER)));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicDoubleCast.usesSpent, 0);
}
}
