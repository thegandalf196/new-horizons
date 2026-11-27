/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/CastleSpecialtyFixture.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/DispelValue.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsInfernoSpecialties.h"

namespace
{
class InfernoSpellProjectionTest : public CastleSpecialtyTest
{
protected:
	void fireballArena(const char * hero)
	{
		ASSERT_NO_FATAL_FAILURE(specialist(hero, 10, false));
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FIREBALL, "new-horizons:havocMagic"));
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(1, 5), 1, 1000);
		actor = place(BattleSide::ATTACKER, "core:swordsman", BattleHex(7, 5), 100, 800);
		enemy = place(BattleSide::DEFENDER, "core:halberdier", BattleHex(8, 5), 200, 400);
		ASSERT_TRUE(active && actor && enemy);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}

	void projectFireball()
	{
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		spells::BattleCast preview(parent.get(), attackerSideHero, spells::Mode::HERO,
			SpellID(SpellID::FIREBALL).toSpell());
		const spells::Target target{spells::Destination(parent->battleGetUnitByID(enemy->unitId()))};
		preview.getSpell()->battleMechanics(&preview)->castEval(parent->getServerCallback(), target);
	}
};
}

TEST_F(InfernoSpellProjectionTest, BloodfireForecastPreservesFriendlyHealthAndMatchesAcceptedAttackGain)
{
	ASSERT_NO_FATAL_FAILURE(fireballArena("core:ash"));
	const auto health = actor->getAvailableHealth();
	const auto attack = actor->getAttack(false);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_NO_FATAL_FAILURE(projectFireball());
	const auto * projected = parent->battleGetUnitByID(actor->unitId());
	ASSERT_NE(projected, nullptr);
	EXPECT_EQ(projected->getAvailableHealth(), health);
	EXPECT_GT(projected->getAttack(false), attack);
	EXPECT_EQ(actor->getAttack(false), attack);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_EQ(actor->getAvailableHealth(), projected->getAvailableHealth());
	EXPECT_EQ(actor->getAttack(false), projected->getAttack(false));
	EXPECT_EQ(enemy->getAvailableHealth(), parent->battleGetUnitByID(enemy->unitId())->getAvailableHealth());
}

TEST_F(InfernoSpellProjectionTest, BurnAwayForecastCreditsRemovedBuffAndMatchesAcceptedCast)
{
	ASSERT_NO_FATAL_FAILURE(fireballArena("core:xarfax"));
	Bonus buff(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 10,
		BonusSourceID(SpellID(SpellID::BLOODLUST)), BonusSubtypeID(PrimarySkill::ATTACK));
	buff.turnsRemain = 3;
	effect(enemy, buff);
	ASSERT_NO_FATAL_FAILURE(projectFireball());
	const auto source = BonusSourceID(SpellID(SpellID::BLOODLUST));
	const auto * projected = parent->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projected, nullptr);
	EXPECT_FALSE(projected->hasBonusFrom(BonusSource::SPELL_EFFECT, source));
	EXPECT_TRUE(enemy->hasBonusFrom(BonusSource::SPELL_EFFECT, source));
	EXPECT_GT(dispelAI::removedEffectValue(SpellID(SpellID::FIREBALL).toSpell(), *live,
		parent, PlayerColor(0), 1, 1), 0);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_FALSE(enemy->hasBonusFrom(BonusSource::SPELL_EFFECT, source));
	EXPECT_EQ(enemy->getAvailableHealth(), projected->getAvailableHealth());
	EXPECT_EQ(enemy->getAttack(false), projected->getAttack(false));
}

TEST_F(InfernoSpellProjectionTest, KindlingForecastReadsAcceptedFireHistoryThroughPlayerView)
{
	ASSERT_NO_FATAL_FAILURE(fireballArena("core:xyron"));
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	const auto history = battle()->getUsedSpells(BattleSide::ATTACKER);
	ASSERT_FALSE(history.empty());
	environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto expected = infernoSpellSpecialties::kindlingBonusBasisPoints(*battle(), BattleSide::ATTACKER, SpellID::FIREBALL);
	ASSERT_GT(expected, 0);
	EXPECT_EQ(parent->getUsedSpells(BattleSide::ATTACKER), history);
	EXPECT_EQ(child->getUsedSpells(BattleSide::ATTACKER), history);
	EXPECT_EQ(infernoSpellSpecialties::kindlingBonusBasisPoints(*parent, BattleSide::ATTACKER, SpellID::FIREBALL), expected);
	EXPECT_EQ(infernoSpellSpecialties::kindlingBonusBasisPoints(*child, BattleSide::ATTACKER, SpellID::FIREBALL), expected);
	const auto * spell = SpellID(SpellID::FIREBALL).toSpell();
	spells::BattleCast actual(battle(), attackerSideHero, spells::Mode::HERO, spell);
	spells::BattleCast forecast(child.get(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&actual)->adjustEffectValue(enemy),
		spell->battleMechanics(&forecast)->adjustEffectValue(child->battleGetUnitByID(enemy->unitId())));
	EXPECT_EQ(battle()->getUsedSpells(BattleSide::ATTACKER), history);
}

TEST_F(InfernoSpellProjectionTest, FirstAndSecondKindlingCastsMatchTheirPrecastForecasts)
{
	ASSERT_NO_FATAL_FAILURE(fireballArena("core:xyron"));
	for(int cast = 0; cast < 2; ++cast)
	{
		const auto before = enemy->getAvailableHealth();
		ASSERT_NO_FATAL_FAILURE(projectFireball());
		const auto expected = parent->battleGetUnitByID(enemy->unitId())->getAvailableHealth();
		ASSERT_LT(expected, before);
		ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
		EXPECT_EQ(enemy->getAvailableHealth(), expected) << "cast " << cast + 1;
		if(cast == 0)
		{
			BattleNextRound next;
			next.battleID = BattleID(0);
			gameHandler->sendAndApply(next);
			ASSERT_NO_FATAL_FAILURE(activate(active));
		}
	}
}

TEST_F(InfernoSpellProjectionTest, KindlingAcceptedDoubleCastHistoryIsLocalToEachBranch)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:xyron", 10, false));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FIREBALL, "new-horizons:havocMagic"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(SpellID::INFERNO);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	active = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(1, 5), 1000, 1000);
	enemy = place(BattleSide::DEFENDER, "core:halberdier", BattleHex(8, 5), 2000, 100);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto project = [&](const std::shared_ptr<HypotheticBattle> & state, SpellID spell)
	{
		PossibleSpellcast candidate;
		candidate.command = HeroCommand::NONE;
		candidate.spell = spell.toSpell();
		candidate.dest.emplace_back(state->battleGetUnitByID(enemy->unitId()));
		const auto admission = heroActionProjection::admitCandidate(*state, BattleSide::ATTACKER, candidate);
		if(!admission.admitted) return false;
		uint32_t target = enemy->unitId();
		return battleEvaluatorProjection::projectSpell(*state, *live, attackerSideHero,
			BattleSide::ATTACKER, candidate, admission, target, {}).accepted;
	};
	ASSERT_TRUE(project(parent, SpellID::FIREBALL));
	ASSERT_EQ(parent->getUsedSpells(BattleSide::ATTACKER).size(), 1u);
	EXPECT_TRUE(sibling->getUsedSpells(BattleSide::ATTACKER).empty());
	EXPECT_TRUE(battle()->getUsedSpells(BattleSide::ATTACKER).empty());
	const auto firstHealth = parent->battleGetUnitByID(enemy->unitId())->getAvailableHealth();
	sibling.reset(); parent.reset();
	// Capture a legal offer opened by the ordinary authoritative first cast.
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_EQ(enemy->getAvailableHealth(), firstHealth);
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_EQ(parent->getUsedSpells(BattleSide::ATTACKER).size(), 1u);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_EQ(infernoSpellSpecialties::kindlingBonusBasisPoints(*child, BattleSide::ATTACKER, SpellID::INFERNO), 1000);
	ASSERT_TRUE(project(child, SpellID::INFERNO));
	EXPECT_EQ(child->getUsedSpells(BattleSide::ATTACKER).size(), 2u);
	EXPECT_EQ(parent->getUsedSpells(BattleSide::ATTACKER).size(), 1u);
	EXPECT_EQ(sibling->getUsedSpells(BattleSide::ATTACKER).size(), 1u);
	EXPECT_EQ(battle()->getUsedSpells(BattleSide::ATTACKER).size(), 1u);
	const auto followUpHealth = child->battleGetUnitByID(enemy->unitId())->getAvailableHealth();
	auto nextRound = std::make_shared<HypotheticBattle>(environment.get(), child);
	nextRound->nextRound();
	EXPECT_GT(infernoSpellSpecialties::kindlingBonusBasisPoints(*nextRound, BattleSide::ATTACKER, SpellID::FIREBALL), 1000);
	ASSERT_TRUE(project(nextRound, SpellID::FIREBALL));
	EXPECT_EQ(nextRound->getUsedSpells(BattleSide::ATTACKER).size(), 3u);
	EXPECT_EQ(child->getUsedSpells(BattleSide::ATTACKER).size(), 2u);
	EXPECT_EQ(sibling->getUsedSpells(BattleSide::ATTACKER).size(), 1u);
	const auto nextRoundHealth = nextRound->battleGetUnitByID(enemy->unitId())->getAvailableHealth();
	// Release all source-bound forecasts before advancing their authoritative source.
	nextRound.reset(); child.reset(); sibling.reset(); parent.reset();
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::INFERNO, enemy)));
	EXPECT_EQ(enemy->getAvailableHealth(), followUpHealth);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_EQ(enemy->getAvailableHealth(), nextRoundHealth);
}

TEST_F(InfernoSpellProjectionTest, KindlingOwnedExportRootAndChildCaptureHistoryWithinNativeLease)
{
	ASSERT_NO_FATAL_FAILURE(fireballArena("core:xyron"));
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
	// Explicit originating full-information lease, as in OwnedExportMeleeTest.
	auto authority = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto owned = InitialExportBattleSubject::capture(authority, *battle());
	ASSERT_NE(owned, nullptr);
	parent = std::make_shared<HypotheticBattle>(environment.get(), owned);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_NE(parent->initialExportSubject(), nullptr);
	EXPECT_EQ(parent->getUsedSpells(BattleSide::ATTACKER), battle()->getUsedSpells(BattleSide::ATTACKER));
	EXPECT_EQ(child->getUsedSpells(BattleSide::ATTACKER), parent->getUsedSpells(BattleSide::ATTACKER));
	EXPECT_GT(infernoSpellSpecialties::kindlingBonusBasisPoints(*child, BattleSide::ATTACKER, SpellID::FIREBALL), 0);
}
#endif
