/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/MetamagicDoubleCast.h"
#include "../../../lib/battle/NewHorizonsAcademySpecialties.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/hero/NewHorizonsAcademySpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsSchoolSpellcraft.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsSorceryRevision.h"
#include "../../../lib/battle/NewHorizonsSpecialtyProcs.h"
#include "../../SpellPointTestUtils.h"

namespace
{
constexpr auto ACADEMY_ATTACKER = BattleSide::ATTACKER;
constexpr auto ACADEMY_DEFENDER = BattleSide::DEFENDER;

/// Revised Academy combat specialties on the shipped rules, struck and cast through the
/// authoritative battle processor. `stripped` reproduces a 0.94.0-beta.1 capture.
class AcademyCombatSpecialtyTest : public CastleSpecialtyTest
{
protected:
	double academyMultiplier(const CStack * attacker, const CStack * defender) const
	{
		return newHorizonsAcademy::attackDamageMultiplier(*battle(), BattleAttackInfo(attacker, defender, 0, false));
	}

	int64_t academySpellDamage(SpellID spell, const CStack * target) const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		return cast.getSpell()->battleMechanics(&cast)->adjustEffectValue(target);
	}

	int enhancedBasisPoints(SpellID spell) const
	{
		const auto context = newHorizonsHeroes::captureSpellcraftContext(attackerSideHero->getMagicRules(),
			attackerSideHero, spell, true);
		return context.factors ? context.factors->enhancedEfficiencyBasisPoints : 0;
	}


	void restoreAttackerLedger(const SideInBattle & ledger)
	{
		battle()->getSide(ACADEMY_ATTACKER) = ledger;
	}
};
}

TEST_F(AcademyCombatSpecialtyTest, HardenedHitsReduceLaterDamageUntilTheStackActs)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:neela"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * pikemen = place(ACADEMY_ATTACKER, "core:pikeman", BattleHex(7, 5), 200, 300);
	// Archers act between the two Angels and the Pikemen, so the queue does not end the mark early.
	auto * archers = place(ACADEMY_ATTACKER, "core:archer", BattleHex(1, 1), 5, 600);
	auto * first = place(ACADEMY_DEFENDER, "core:angel", BattleHex(8, 5), 1, 1000);
	auto * second = place(ACADEMY_DEFENDER, "core:angel", BattleHex(14, 9), 1, 900);
	ASSERT_TRUE(pikemen && archers && first && second);
	for(const auto * angel : {first, second})
		effect(angel, Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto plain = battle()->battleEstimateDamage(BattleAttackInfo(first, pikemen, 0, false));
	EXPECT_EQ(newHorizonsAcademy::hardenedPermille(*pikemen), 0);
	ASSERT_NO_FATAL_FAILURE(activate(first));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(first, pikemen, first->getPosition()), PlayerColor(1)));
	EXPECT_EQ(newHorizonsAcademy::hardenedPermille(*pikemen), 50) << "One hit: 5%";
	EXPECT_EQ(newHorizonsAcademy::hardenedPermille(*first), 0) << "Only her own stacks";
	EXPECT_NEAR(academyMultiplier(second, pikemen), 0.95, 1e-9);
	ASSERT_NO_FATAL_FAILURE(activate(second));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(second, pikemen, BattleHex(6, 5)), PlayerColor(1)));
	EXPECT_EQ(newHorizonsAcademy::hardenedPermille(*pikemen), 100) << "Two hits: 10%, the level 1 total";
	EXPECT_EQ(pikemen->getBonuses(CSelector(newHorizonsAcademy::isHardenedMarker))->size(), 1u) << "One mark, refreshed";
	const auto third = newHorizonsAcademy::hardenedMarker(attackerSideHero, *pikemen);
	ASSERT_TRUE(third);
	EXPECT_EQ(third->val, 100) << "A further hit stays at the level 1 total";
	// The estimate the authority and the AI share carries the reduction.
	const auto reduced = battle()->battleEstimateDamage(BattleAttackInfo(first, pikemen, 0, false));
	EXPECT_NEAR(static_cast<double>(reduced.damage.max) / plain.damage.max, 0.9, 0.01);
	ASSERT_NO_FATAL_FAILURE(activate(pikemen));
	EXPECT_EQ(newHorizonsAcademy::hardenedPermille(*pikemen), 0) << "Her stack's next activation ends the mark";
}

TEST_F(AcademyCombatSpecialtyTest, HardenedTotalScalesWithLevel)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:neela", 11));
	EXPECT_EQ(newHorizonsHeroes::hardenedTotalPermille(attackerSideHero), 150);
	attackerSideHero->level = 30;
	EXPECT_EQ(newHorizonsHeroes::hardenedTotalPermille(attackerSideHero), 200);
}

TEST_F(AcademyCombatSpecialtyTest, HistoricalNeelaIsNotHardened)
{
	stripped = {"hardened"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:neela"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * pikemen = place(ACADEMY_ATTACKER, "core:pikeman", BattleHex(7, 5), 200, 300);
	ASSERT_TRUE(place(ACADEMY_ATTACKER, "core:archer", BattleHex(1, 1), 5, 600));
	auto * angels = place(ACADEMY_DEFENDER, "core:angel", BattleHex(8, 5), 1, 1000);
	ASSERT_TRUE(pikemen && angels);
	effect(angels, Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(angels));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(angels, pikemen, angels->getPosition()), PlayerColor(1)));
	EXPECT_EQ(newHorizonsAcademy::hardenedPermille(*pikemen), 0);
	EXPECT_EQ(newHorizonsHeroes::hardenedHitPermille(attackerSideHero), 0);
}

TEST_F(AcademyCombatSpecialtyTest, SerpentsEmbraceNagasDealMoreDamageToTargetsTheyBound)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:fafner"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * nagas = place(ACADEMY_ATTACKER, "core:naga", BattleHex(7, 5), 5, 1000);
	auto * queens = place(ACADEMY_ATTACKER, "core:nagaQueen", BattleHex(2, 9), 5, 900);
	auto * pikemen = place(ACADEMY_DEFENDER, "core:pikeman", BattleHex(8, 5), 200, 300);
	ASSERT_TRUE(nagas && queens && pikemen);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	EXPECT_NEAR(academyMultiplier(nagas, pikemen), 1.0, 1e-9) << "Not yet bound";
	ASSERT_NO_FATAL_FAILURE(activate(nagas));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(nagas, pikemen, nagas->getPosition())));
	ASSERT_TRUE(pikemen->alive());
	ASSERT_TRUE(pikemen->hasBonusOfType(BonusType::BIND_EFFECT)) << "The Naga binds its target";
	EXPECT_NEAR(academyMultiplier(nagas, pikemen), 1.02, 1e-9) << "+2% at level 1";
	EXPECT_NEAR(academyMultiplier(queens, pikemen), 1.0, 1e-9) << "Only the stack that holds the target";
	attackerSideHero->level = 20;
	EXPECT_NEAR(academyMultiplier(nagas, pikemen), 1.4, 1e-9) << "+2% per further level, up to +40%";
	attackerSideHero->level = 40;
	EXPECT_NEAR(academyMultiplier(nagas, pikemen), 1.4, 1e-9);
}

TEST_F(AcademyCombatSpecialtyTest, HistoricalFafnerBindsWithoutTheDamageBonus)
{
	stripped = {"serpentsEmbraceBound"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:fafner"));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * nagas = place(ACADEMY_ATTACKER, "core:naga", BattleHex(7, 5), 5, 1000);
	auto * pikemen = place(ACADEMY_DEFENDER, "core:pikeman", BattleHex(8, 5), 200, 300);
	ASSERT_TRUE(nagas && pikemen);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(nagas));
	ASSERT_TRUE(submit(BattleAction::makeMeleeAttack(nagas, pikemen, nagas->getPosition())));
	ASSERT_TRUE(pikemen->hasBonusOfType(BonusType::BIND_EFFECT)) << "The unchanged rule still binds";
	EXPECT_NEAR(academyMultiplier(nagas, pikemen), 1.0, 1e-9);
}

TEST_F(AcademyCombatSpecialtyTest, StormLordStrengthensLightningAndTeachesChainLightningAtTen)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:solmyr"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::LIGHTNING_BOLT, "new-horizons:havocMagic"));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 400, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(arena());
	const int level1 = enhancedBasisPoints(SpellID::LIGHTNING_BOLT);
	ASSERT_GT(level1, 0) << "Storm Lord scales the Spell Power term";
	EXPECT_EQ(enhancedBasisPoints(SpellID::FIREBALL), 0) << "Only Lightning Bolt and Chain Lightning";
	const auto weak = academySpellDamage(SpellID::LIGHTNING_BOLT, enemy);
	attackerSideHero->level = 20;
	EXPECT_EQ(enhancedBasisPoints(SpellID::LIGHTNING_BOLT) * 103, level1 * 160) << "+3% at level 1, +60% at level 20";
	EXPECT_GT(academySpellDamage(SpellID::LIGHTNING_BOLT, enemy), weak);
	// Chain Lightning is learned on reaching level 10 and is an ordinary spell for him again.
	attackerSideHero->level = 8;
	attackerSideHero->removeSpellFromSpellbook(SpellID::CHAIN_LIGHTNING);
	attackerSideHero->levelUp();
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::CHAIN_LIGHTNING));
	attackerSideHero->levelUp();
	EXPECT_EQ(attackerSideHero->level, 10);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::CHAIN_LIGHTNING));
	EXPECT_TRUE(newHorizonsHeroes::stormLordAllowsSpell(*attackerSideHero, SpellID::CHAIN_LIGHTNING));
}

TEST_F(AcademyCombatSpecialtyTest, HistoricalSolmyrKeepsMasterChainLightningRules)
{
	stripped = {"stormLord"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:solmyr"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::LIGHTNING_BOLT, "new-horizons:havocMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	EXPECT_EQ(enhancedBasisPoints(SpellID::LIGHTNING_BOLT), 0);
	EXPECT_FALSE(newHorizonsHeroes::stormLordAllowsSpell(*attackerSideHero, SpellID::CHAIN_LIGHTNING)) << "Chain Lightning stays excluded";
	attackerSideHero->level = 9;
	attackerSideHero->removeSpellFromSpellbook(SpellID::CHAIN_LIGHTNING);
	attackerSideHero->levelUp();
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::CHAIN_LIGHTNING));
}

TEST_F(AcademyCombatSpecialtyTest, PracticeMakesPerfectStrengthensEachCastSpellUpToTheLevelCap)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:astral"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::MAGIC_ARROW), 0);
	EXPECT_EQ(enhancedBasisPoints(SpellID::MAGIC_ARROW), 0) << "An unpracticed spell is ordinary";
	const auto ledger = battle()->getSide(ACADEMY_ATTACKER);
	ASSERT_TRUE(submit(spellAt(ACADEMY_ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::MAGIC_ARROW), 1) << "Each cast: +1%";
	const int once = enhancedBasisPoints(SpellID::MAGIC_ARROW);
	ASSERT_GT(once, 0);
	restoreAttackerLedger(ledger);
	ASSERT_TRUE(submit(spellAt(ACADEMY_ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::MAGIC_ARROW), 2);
	EXPECT_EQ(enhancedBasisPoints(SpellID::MAGIC_ARROW) * 101, once * 102);
	restoreAttackerLedger(ledger);
	ASSERT_TRUE(submit(spellAt(ACADEMY_ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::MAGIC_ARROW), 2) << "Capped at 2% x level 1";
	EXPECT_EQ(attackerSideHero->getBonuses(CSelector([](const Bonus * bonus)
		{ return bonus && bonus->stacking.starts_with("new-horizons:academy-specialty:practice:"); }))->size(), 1u)
		<< "One saved marker per spell";
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::SLOW), 0) << "Per spell";
	attackerSideHero->level = 2;
	restoreAttackerLedger(ledger);
	ASSERT_TRUE(submit(spellAt(ACADEMY_ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::MAGIC_ARROW), 3) << "A higher level lifts the cap";
}

TEST_F(AcademyCombatSpecialtyTest, HistoricalAstralDoesNotPractice)
{
	stripped = {"practiceMakesPerfect"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:astral"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
	setTestSpellPointTotal(attackerSideHero, 200);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(submit(spellAt(ACADEMY_ATTACKER, SpellID::MAGIC_ARROW, enemy)));
	EXPECT_EQ(newHorizonsHeroes::practicePercent(attackerSideHero, SpellID::MAGIC_ARROW), 0);
}

TEST_F(AcademyCombatSpecialtyTest, HalonsMagiCarryNullifyingShotAndHisExtraDoubleCastUseIsRetired)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:halon"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const int uses = metamagicDoubleCast::maximumUses(attackerSideHero);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * magi = place(ACADEMY_ATTACKER, "core:mage", BattleHex(2, 5), 10, 1000);
	auto * archMagi = place(ACADEMY_ATTACKER, "core:archMage", BattleHex(2, 7), 10, 900);
	auto * enemy = place(ACADEMY_DEFENDER, "core:pikeman", BattleHex(12, 5), 50, 300);
	ASSERT_TRUE(magi && archMagi && enemy);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const SpellID dispel(SpellID::DISPEL_HELPFUL_SPELLS);
	EXPECT_EQ(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *magi, dispel, true), 20) << "Theodorus's values";
	EXPECT_EQ(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *archMagi, dispel, true), 40);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:serena")));
	EXPECT_EQ(uses, metamagicDoubleCast::maximumUses(attackerSideHero)) << "No extra Double Cast use any more";
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:theodorus")));
	EXPECT_FALSE(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *magi, dispel, true)) << "Theodorus no longer has it";
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, "Inexhaustible Knowledge");
}

TEST_F(AcademyCombatSpecialtyTest, BetaOneCaptureKeepsNullifyingShotOnTheodorusAndHalonsExtraUse)
{
	stripped = {"halonNullifyingShot", "inexhaustibleKnowledge"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:halon", 20));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const int uses = metamagicDoubleCast::maximumUses(attackerSideHero);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * magi = place(ACADEMY_ATTACKER, "core:mage", BattleHex(2, 5), 10, 1000);
	ASSERT_TRUE(magi && place(ACADEMY_DEFENDER, "core:pikeman", BattleHex(12, 5), 50, 300));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const SpellID dispel(SpellID::DISPEL_HELPFUL_SPELLS);
	EXPECT_FALSE(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *magi, dispel, true));
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:serena")));
	EXPECT_EQ(uses, metamagicDoubleCast::maximumUses(attackerSideHero) + 1) << "Halon keeps his flat extra use";
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:theodorus")));
	attackerSideHero->level = 1;
	EXPECT_EQ(newHorizonsSpecialtyProcs::attackSpellChance(*battle(), *magi, dispel, true), 20);
	EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*attackerSideHero), 0);
}

TEST_F(AcademyCombatSpecialtyTest, ACaptureHoldingTheRetiredSpellweaverStillPlaysIt)
{
	stripped = {"halonNullifyingShot"};
	ASSERT_NO_FATAL_FAILURE(world());
	JsonNode weaver;
	weaver["additionalUses"].Integer() = 1;
	weaver["increasedAdditionalUses"].Integer() = 2;
	weaver["increasedFromLevel"].Integer() = 15;
	auto rules = attackerSideHero->getPrimaryGrowthRules();
	ASSERT_FALSE(newHorizonsHeroes::academySpecialtyRules(rules).spellweaver) << "Never shipped";
	rules["revisedSpecialties"]["spellweaver"] = weaver;
	const auto parsed = newHorizonsHeroes::academySpecialtyRules(rules);
	ASSERT_TRUE(parsed.spellweaver);
	EXPECT_EQ(parsed.spellweaver->increasedAdditionalUses, 2);
	EXPECT_TRUE(newHorizonsHeroes::isRetiredRevisedSpecialtyRow("spellweaver"));
}

TEST_F(AcademyCombatSpecialtyTest, InexhaustibleKnowledgeRegainsManaEachLaterRoundUpToTheMaximum)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:theodorus"));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 400, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(arena());
	const int maximum = attackerSideHero->manaLimit();
	ASSERT_GE(maximum, 50);
	setTestSpellPointTotal(attackerSideHero, 0);
	const int perRound = std::max(1, maximum * 40 / 1000);
	EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*attackerSideHero), perRound) << "4% at level 1";
	// Every stack acts; the next round starts through the authoritative battle flow.
	const auto rounds = battle()->battleGetRound();
	ASSERT_TRUE(submit(BattleAction::makeDefend(active)));
	ASSERT_TRUE(submit(BattleAction::makeDefend(actor)));
	ASSERT_TRUE(submit(BattleAction::makeDefend(waiter)));
	ASSERT_TRUE(submit(BattleAction::makeDefend(enemy), PlayerColor(1)));
	ASSERT_EQ(battle()->battleGetRound(), rounds + 1);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), perRound) << "He regains it at the start of the next round";
	EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*defenderSideHero), 0) << "Only Theodorus";
	attackerSideHero->level = 21;
	EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*attackerSideHero), std::max(1, maximum * 80 / 1000))
		<< "+0.2 points per further level: 8% at level 21";
	setTestSpellPointTotal(attackerSideHero, maximum - 1);
	EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*attackerSideHero), 1) << "Never above his maximum";
	setTestSpellPointTotal(attackerSideHero, maximum);
	EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*attackerSideHero), 0);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 0);
	if(attackerSideHero->manaLimit() > 0 && attackerSideHero->manaLimit() * 80 / 1000 == 0)
		EXPECT_EQ(newHorizonsHeroes::inexhaustibleKnowledgeMana(*attackerSideHero), 1) << "At least 1 Mana";
}

TEST_F(AcademyCombatSpecialtyTest, CyrasFocusMagicPremiumGrowsHalfAPointPerLevel)
{
	ASSERT_NO_FATAL_FAILURE(specialist("core:cyra"));
	const SpellID focus(SpellID::decode("new-horizons:focusMagic"));
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(attackerSideHero, focus), 10);
	attackerSideHero->level = 3;
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(attackerSideHero, focus), 11) << "10.5 rounds down at 2";
	attackerSideHero->level = 21;
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(attackerSideHero, focus), 20);
	attackerSideHero->level = 40;
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(attackerSideHero, focus), 20);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(attackerSideHero, focus), 0)
		<< "Brissa's flat bonus belongs to her own hero prototype, not this hero's bonuses";
}

TEST_F(AcademyCombatSpecialtyTest, HistoricalCyraKeepsTheFlatTenPoints)
{
	stripped = {"focusMagicSpecialty"};
	ASSERT_NO_FATAL_FAILURE(specialist("core:cyra", 21));
	EXPECT_FALSE(newHorizonsHeroes::focusMagicSpecialtyPermille(attackerSideHero).has_value());
}
