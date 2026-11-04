/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/ResourceSet.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/battle/WisdomCombatState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsWisdom.h"
#include "../../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../../lib/spells/SpellCostBreakdown.h"

TEST(NewHorizonsWisdomState, FamiliarityTracksParticularSpellsAcrossInterveningCastsAndFloorsPayment)
{
	WisdomCombatState state;
	EXPECT_EQ(newHorizonsWisdom::familiarityCost(21, state.priorCasts(SpellID::HASTE)), 21);
	state.recordCompletedCast(SpellID::HASTE, 1);
	state.recordCompletedCast(SpellID::BLESS, 2);
	EXPECT_EQ(newHorizonsWisdom::familiarityCost(21, state.priorCasts(SpellID::HASTE)), 20);
	EXPECT_EQ(newHorizonsWisdom::familiarityCost(21, state.priorCasts(SpellID::SLOW)), 21);
	for(int cast = 0; cast < 30; ++cast) state.recordCompletedCast(SpellID::HASTE, 2);
	EXPECT_EQ(newHorizonsWisdom::familiarityCost(21, state.priorCasts(SpellID::HASTE)), 1);
	EXPECT_EQ(newHorizonsWisdom::familiarityCost(0, 30), 0);
}
TEST(NewHorizonsWisdomState, MomentumFirstCastEarnsForLaterCastsAndMissingRoundResets)
{
	WisdomCombatState state;
	EXPECT_EQ(state.momentumBonusPercent(1), 0);
	state.recordCompletedCast(SpellID::HASTE, 1);
	EXPECT_EQ(state.momentumBonusPercent(1), 5);
	state.recordCompletedCast(SpellID::BLESS, 1);
	EXPECT_EQ(state.momentumBonusPercent(1), 5);
	EXPECT_EQ(state.momentumBonusPercent(2), 5);
	state.recordCompletedCast(SpellID::HASTE, 2);
	EXPECT_EQ(state.momentumBonusPercent(2), 10);
	for(int round = 3; round <= 8; ++round) state.recordCompletedCast(SpellID::HASTE, round);
	EXPECT_EQ(state.momentumBonusPercent(9), 20);
	EXPECT_EQ(state.momentumBonusPercent(10), 0);
	state.recordCompletedCast(SpellID::HASTE, 10);
	EXPECT_EQ(state.momentumBonusPercent(10), 5);
	EXPECT_EQ(WisdomCombatState{}.momentumBonusPercent(1), 0);
}
TEST(NewHorizonsWisdomState, CurrentWireRoundtripPreservesHistoryAndBranchCopiesAreIndependent)
{
	WisdomCombatState state;
	state.recordCompletedCast(SpellID::HASTE, 1);
	state.recordCompletedCast(SpellID::BLESS, 2);
	CMemorySerializer bytes;
	bytes.oser & state;
	WisdomCombatState loaded;
	bytes.iser & loaded;
	EXPECT_EQ(loaded.completedCasts, state.completedCasts);
	EXPECT_EQ(loaded.lastCastRound, 2);
	EXPECT_EQ(loaded.momentumBonusPercent(2), 10);
	auto branch = loaded;
	branch.recordCompletedCast(SpellID::HASTE, 3);
	EXPECT_EQ(loaded.priorCasts(SpellID::HASTE), 1);
	EXPECT_EQ(branch.priorCasts(SpellID::HASTE), 2);
	EXPECT_EQ(loaded.momentumBonusPercent(3), 10);
	EXPECT_EQ(branch.momentumBonusPercent(3), 15);
}
TEST(NewHorizonsWisdomState, EffectivenessAddsToMetamagicBeforeOneFloorAndCap)
{
	newHorizonsMetamagic::Profile profile;
	profile.pointPolicy = true;
	profile.rank = 3;
	profile.wisdomEffectivenessBonus = 45;
	EXPECT_EQ(newHorizonsMetamagic::scaleMagnitude(4, profile, 4), 6); // floor4*1.57; two separately floored stages would yield5.
	EXPECT_EQ(newHorizonsMetamagic::scaleSignedBonus(-4, profile, 4), -6);
	EXPECT_EQ(newHorizonsMetamagic::scaleDuration(4, profile, 4), 6);
	EXPECT_EQ(newHorizonsMetamagic::spellDamageOutput(100, profile, 4, 0, 120), 120);
	profile.rank = 0;
	EXPECT_EQ(newHorizonsMetamagic::scaleMagnitude(100, profile, 0), 145);
}
namespace
{
class NewHorizonsWisdomRevisionTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void learn(const char * basic, const char * advanced = nullptr, const char * expert = nullptr)
	{
		const std::array<const char *, 3> perks{basic, advanced, expert};
		for(int tier = 0; tier < 3 && perks[tier]; ++tier)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")), tier + 1, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", perks[tier]});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", perks[tier]));
		}
	}
	void prepare(const char * basic, const char * advanced = nullptr, const char * expert = nullptr, bool withTown = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame(withTown));
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:solmyr")));
		ASSERT_NO_FATAL_FAILURE(learn(basic, advanced, expert));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::HASTE, SpellID::BLESS, SpellID::MAGIC_ARROW}) attackerSideHero->addSpellToSpellbook(spell);
		for(auto * hero : {attackerSideHero, defenderSideHero}) hero->setStackCount(SlotID(0), 1000);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0)) actor = unit.get();
		ASSERT_NE(actor, nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}
	bool castHaste()
	{
		auto action = BattleAction::makeDefend(actor);
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(actor);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
}
TEST_F(NewHorizonsWisdomRevisionTest, AuthoritativeReceiptCountsOnceAndRejectedActionChangesNothing)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.spellFamiliarity"));
	const auto spell = SpellID(SpellID::HASTE).toSpell();
	const int32_t ordinary = battle()->battleGetSpellCost(spell, attackerSideHero);
	const int64_t mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castHaste());
	EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(SpellID::HASTE), 1);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), ordinary);
	EXPECT_FALSE(castHaste());
	EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(SpellID::HASTE), 1);
	advanceRound();
	const auto breakdown = battle()->battleGetSpellCostBreakdown(spell, attackerSideHero);
	const auto discounted = breakdown.finalCost;
	EXPECT_EQ(discounted, newHorizonsWisdom::familiarityCost(ordinary, 1));
	EXPECT_EQ(std::count_if(breakdown.stages.begin(), breakdown.stages.end(), [](const auto & stage)
	{ return stage.kind == SpellCostStage::Kind::SPELL_FAMILIARITY; }), 1);
}
TEST_F(NewHorizonsWisdomRevisionTest, InsightAndArchmageWorkWithoutMetamagicAndSnapshotBeforeCompletion)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.spellFamiliarity", "new-horizons:wisdom.arcaneInsight", "new-horizons:wisdom.archmage"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), 0, ChangeValueMode::ABSOLUTE);
	const auto before = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::HASTE);
	EXPECT_EQ(before.perks.wisdomEffectivenessBonus, 25);
	ASSERT_TRUE(castHaste());
	EXPECT_EQ(before.perks.wisdomEffectivenessBonus, 25);
	const auto after = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::HASTE);
	EXPECT_EQ(after.perks.wisdomEffectivenessBonus, 0);
	const auto distinct = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::BLESS);
	EXPECT_EQ(distinct.perks.wisdomEffectivenessBonus, 25);
	const auto high = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::RESURRECTION);
	EXPECT_EQ(high.perks.wisdomEffectivenessBonus, 45);
	EXPECT_EQ(newHorizonsWisdom::archmagePenetrationPercent(attackerSideHero, attackerSideHero->getMagicRules(), SpellID::RESURRECTION), 10);
	EXPECT_EQ(newHorizonsWisdom::archmagePenetrationPercent(attackerSideHero, attackerSideHero->getMagicRules(), SpellID::HASTE), 0);
	EXPECT_EQ(newHorizonsMetamagic::outgoingMagnitude(100, &high, actor), 145);
	EXPECT_EQ(newHorizonsMetamagic::outgoingMagnitude(100, &high, nullptr), 145);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::IMPLOSION).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto penetration = mechanics->getCapturedMdrPenetration();
	ASSERT_EQ(penetration["penetrations"].Vector().size(), 1);
	EXPECT_EQ(penetration["penetrations"].Vector().front().Integer(), 10);
	battle()->getSide(BattleSide::ATTACKER).wisdomCombatState.recordCompletedCast(SpellID::RESURRECTION, battle()->getRound());
	const auto repeatedHigh = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::RESURRECTION);
	EXPECT_EQ(repeatedHigh.perks.wisdomEffectivenessBonus, 20);
}
TEST_F(NewHorizonsWisdomRevisionTest, SageKnowledgeIsDerivedExactlyOnceAndIntelligenceUsesIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.intelligence", "new-horizons:wisdom.manaConservation", "new-horizons:wisdom.sage"));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 151);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 151);
	EXPECT_EQ(attackerSideHero->manaLimit(), 196);
	EXPECT_EQ(attackerSideHero->getPrimaryGrowthView()->modified[PrimarySkill::KNOWLEDGE], 151);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")), 2, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 101);
	EXPECT_EQ(attackerSideHero->manaLimit(), 131);
}
TEST_F(NewHorizonsWisdomRevisionTest, ScholarDiscountUsesVisitingOwnerOnlyAndCeilsEveryResource)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.intelligence", "new-horizons:wisdom.spellScholar", nullptr, true));
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	gameHandler->setOwner(town, attackerSideHero->getOwner());
	town->setVisitingHero(attackerSideHero);
	ASSERT_EQ(town->getVisitingHero(), attackerSideHero);
	ASSERT_EQ(attackerSideHero->getVisitedTown(), town);
	EXPECT_FALSE(attackerSideHero->isGarrisoned());
	const auto spell = SpellID(SpellID::FLY);
	auto rules = attackerSideHero->getMagicRules();
	auto & cost = rules["adventureSpells"]["core:fly"]["unlockCost"];
	cost["gold"].Integer() = 101;
	cost["mercury"].Integer() = 1;
	cost["sulfur"].Integer() = 2;
	cost["crystal"].Integer() = 3;
	cost["gems"].Integer() = 0;
	const auto price = newHorizonsWisdom::adventureSpellPurchaseCost(rules, spell, *town);
	EXPECT_EQ(price[EGameResID::GOLD], 76);
	EXPECT_EQ(price[EGameResID::MERCURY], 1);
	EXPECT_EQ(price[EGameResID::SULFUR], 2);
	EXPECT_EQ(price[EGameResID::CRYSTAL], 3);
	EXPECT_EQ(price[EGameResID::GEMS], 0);
	town->setVisitingHero(nullptr);
	town->setGarrisonedHero(attackerSideHero);
	ASSERT_EQ(town->getVisitingHero(), nullptr);
	ASSERT_EQ(town->getGarrisonHero(), attackerSideHero);
	ASSERT_EQ(attackerSideHero->getVisitedTown(), town);
	EXPECT_TRUE(attackerSideHero->isGarrisoned());
	EXPECT_EQ(newHorizonsWisdom::adventureSpellPurchaseCost(rules, spell, *town)[EGameResID::GOLD], 101);
	town->setGarrisonedHero(nullptr);
	ASSERT_EQ(attackerSideHero->getVisitedTown(), nullptr);
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	defenderSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.intelligence"});
	defenderSideHero->setSecSkillLevel(wisdom, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.spellScholar"});
	ASSERT_TRUE(defenderSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.spellScholar"));
	ASSERT_NE(defenderSideHero->getOwner(), town->getOwner());
	town->setVisitingHero(defenderSideHero);
	ASSERT_EQ(town->getVisitingHero(), defenderSideHero);
	ASSERT_EQ(defenderSideHero->getVisitedTown(), town);
	EXPECT_EQ(newHorizonsWisdom::adventureSpellPurchaseCost(rules, spell, *town)[EGameResID::GOLD], 101);
	town->setVisitingHero(nullptr);
	EXPECT_EQ(defenderSideHero->getVisitedTown(), nullptr);
}
TEST_F(NewHorizonsWisdomRevisionTest, FastingIsVictoryAndNoCastGatedAndCappedToNormalCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.intelligence", "new-horizons:wisdom.manaFasting"));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, true, false), 13);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, false, false), 0);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, true, true), 0);
	setTestSpellPointTotal(attackerSideHero, 130);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, true, false), 1);
	setTestSpellPointTotal(attackerSideHero, 131);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, true, false), 0);
}
TEST_F(NewHorizonsWisdomRevisionTest, MysticismRecoversAtDayStartWithMinimumAndMaximumCaps)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.mysticism"));
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 41, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 5);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 10);
	setTestSpellPointTotal(attackerSideHero, 100);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 101);
}

TEST_F(NewHorizonsWisdomRevisionTest, ConservationUsesActualGrossSpendFloorAndTwentyPointCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisdom.intelligence", "new-horizons:wisdom.manaConservation"));
	setTestSpellPointTotal(attackerSideHero, 1);
	EXPECT_EQ(newHorizonsWisdom::conservationRecovery(*attackerSideHero, 4), 0);
	EXPECT_EQ(newHorizonsWisdom::conservationRecovery(*attackerSideHero, 9), 1);
	EXPECT_EQ(newHorizonsWisdom::conservationRecovery(*attackerSideHero, 101), 20);
	setTestSpellPointTotal(attackerSideHero, attackerSideHero->manaLimit() - 1);
	EXPECT_EQ(newHorizonsWisdom::conservationRecovery(*attackerSideHero, 101), 1);
}
