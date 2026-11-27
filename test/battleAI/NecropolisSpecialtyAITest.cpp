/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/CastleSpecialtyFixture.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/entities/hero/NewHorizonsNecropolisCombatSpecialties.h"
#include "../../lib/networkPacks/NecropolisSpecialtyPacks.h"
#include "../../lib/spells/CSpell.h"

namespace
{
constexpr auto SIDE = BattleSide::ATTACKER;
class NecropolisSpecialtyAITest : public CastleSpecialtyTest
{
protected:
	void septienna()
	{
		ASSERT_NO_FATAL_FAILURE(specialist("core:septienna", 25, false));
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
		attackerSideHero->initializeSpellPoints(100, 0);
		ASSERT_NO_FATAL_FAILURE(arena());
		ASSERT_NO_FATAL_FAILURE(activate(active));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
}

TEST_F(NecropolisSpecialtyAITest, EvaluatedResidualLifeCastMatchesAcceptedManaProgressAndHealth)
{
	ASSERT_NO_FATAL_FAILURE(septienna());
	const auto liveMana = attackerSideHero->getManaAvailable();
	const auto liveHealth = enemy->getAvailableHealth();
	PossibleSpellcast candidate;
	candidate.command = HeroCommand::NONE;
	candidate.spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	candidate.dest.emplace_back(enemy);
	const auto admission = heroActionProjection::admitCandidate(*parent, SIDE, candidate);
	ASSERT_TRUE(admission.admitted);
	uint32_t target = enemy->unitId();
	const auto result = battleEvaluatorProjection::projectSpell(*parent, *live, attackerSideHero,
		SIDE, candidate, admission, target, {});
	ASSERT_TRUE(result.accepted);
	const auto mana = parent->getProjectedMana(SIDE);
	ASSERT_TRUE(mana);
	EXPECT_GT(mana->getBuffer(), 0);
	EXPECT_GT(parent->getResidualLifeDamage(SIDE), 0);
	EXPECT_EQ(sibling->getResidualLifeDamage(SIDE), 0);
	EXPECT_EQ(sibling->getProjectedManaAvailable(SIDE), liveMana);
	EXPECT_EQ(enemy->getAvailableHealth(), liveHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), liveMana);
	ASSERT_TRUE(submit(spellAt(SIDE, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(parent->getResidualLifeDamage(SIDE), battle()->getResidualLifeDamage(SIDE));
	EXPECT_EQ(mana->getNormal(), attackerSideHero->getNormalSpellPoints());
	EXPECT_EQ(mana->getBuffer(), attackerSideHero->getBufferSpellPoints());
	EXPECT_EQ(parent->battleGetUnitByID(enemy->unitId())->getAvailableHealth(), enemy->getAvailableHealth());
}

TEST_F(NecropolisSpecialtyAITest, ResidualReceiptCapAndRejectedInputRemainBranchLocal)
{
	ASSERT_NO_FATAL_FAILURE(septienna());
	RecordResidualLife receipt;
	receipt.battleID = BattleID(0);
	receipt.side = SIDE;
	receipt.damage = 24;
	parent->getServerCallback()->apply(receipt);
	EXPECT_EQ(parent->getResidualLifeDamage(SIDE), 24);
	EXPECT_EQ(parent->getProjectedMana(SIDE)->getBuffer(), 0);
	receipt.damage = 10000;
	parent->getServerCallback()->apply(receipt);
	EXPECT_EQ(parent->getProjectedMana(SIDE)->getBuffer(), 20);
	EXPECT_EQ(parent->getResidualLifeDamage(SIDE), 500);
	parent->getServerCallback()->apply(receipt);
	EXPECT_EQ(parent->getProjectedMana(SIDE)->getBuffer(), 20);
	const auto before = parent->forecastSide(SIDE);
	receipt.damage = -1;
	EXPECT_THROW(parent->getServerCallback()->apply(receipt), std::invalid_argument);
	EXPECT_EQ(parent->forecastSide(SIDE), before);
	EXPECT_EQ(sibling->getResidualLifeDamage(SIDE), 0);
	EXPECT_EQ(sibling->getProjectedMana(SIDE)->getBuffer(), 0);
	EXPECT_EQ(battle()->getResidualLifeDamage(SIDE), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
}
#endif
