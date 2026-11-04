/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/ArrowContinuationReuse.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/networkPacks/PacksForClient.h"

namespace
{
constexpr auto SIDE = BattleSide::ATTACKER;
const SpellID SPELL(SpellID::HASTE);
class NewHorizonsWisdomAITest : public ProjectedTargetHitFixture
{
protected:
	void prepareWisdom(const char * basic = nullptr, const char * advanced = nullptr, const char * expert = nullptr)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
		const auto select = [&](const char * perk, int rank)
		{
			if(!perk) return;
			const SecondarySkill skill(SecondarySkill::decode("new-horizons:wisdom"));
			ASSERT_GE(skill.getNum(), 0);
			attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
			const auto id = std::string("new-horizons:wisdom.") + perk;
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", id});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", id));
		};
		ASSERT_NO_FATAL_FAILURE(select(basic, MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(select(advanced, MasteryLevel::ADVANCED));
		ASSERT_NO_FATAL_FAILURE(select(expert, MasteryLevel::EXPERT));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SPELL);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		beginCombat();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		resetChild();
	}
};

TEST_F(NewHorizonsWisdomAITest, CompletedReceiptCopiesHistoryAndKeepsParentSiblingWorldAndRngUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	auto parent = child;
	parent->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, parent->getRound(), false);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto world = gameState()->saveToMemory();
	const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
	descendant->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, descendant->getRound(), false);
	EXPECT_EQ(descendant->getWisdomCombatState(SIDE).priorCasts(SPELL), 2u);
	EXPECT_EQ(parent->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
	EXPECT_EQ(sibling->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
	EXPECT_EQ(battle()->getWisdomCombatState(SIDE).priorCasts(SPELL), 0u);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
	EXPECT_EQ(descendant->getWisdomCombatState(BattleSide::DEFENDER).priorCasts(SPELL), 0u);
	EXPECT_THROW(descendant->getServerCallback()->recordCompletedWisdomSpellCast(
		SIDE, SPELL, descendant->getRound() + 1, false), std::invalid_argument);
	EXPECT_EQ(descendant->getWisdomCombatState(SIDE).priorCasts(SPELL), 2u);
}

TEST_F(NewHorizonsWisdomAITest, OwnedExportAndDescendantRetainTheirOwnWisdomHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto exported = InitialExportBattleSubject::capture(allKnowing, *battle());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), exported);
	parent->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, parent->getRound(), false);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_EQ(descendant->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
	parent->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, parent->getRound(), false);
	EXPECT_EQ(descendant->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
	EXPECT_EQ(descendant->initialExportSubject()->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
	EXPECT_EQ(exported->getWisdomCombatState(SIDE).priorCasts(SPELL), 0u);
	EXPECT_EQ(battle()->getWisdomCombatState(SIDE).priorCasts(SPELL), 0u);
}

TEST_F(NewHorizonsWisdomAITest, SharedFamiliarityPriceUsesBranchHistoryBeforeAnyPaidCast)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom("spellFamiliarity"));
	const auto * spell = SPELL.toSpell();
	const int first = child->battleGetSpellCost(spell, attackerSideHero);
	EXPECT_EQ(first, live->battleGetSpellCost(spell, attackerSideHero));
	for(int i = 0; i < 8; ++i)
		child->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	const int repeated = child->battleGetSpellCost(spell, attackerSideHero);
	EXPECT_GE(repeated, 1);
	EXPECT_LT(repeated, first);
	EXPECT_EQ(live->battleGetSpellCost(spell, attackerSideHero), first);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	EXPECT_EQ(descendant->battleGetSpellCost(spell, attackerSideHero), repeated);
}

TEST_F(NewHorizonsWisdomAITest, SharedCastEvalRecordsExactlyOneCastWithoutChangingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	const auto world = gameState()->saveToMemory();
	const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
	spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, SPELL.toSpell());
	auto mechanics = SPELL.toSpell()->battleMechanics(&cast);
	spells::Target target{spells::Destination(child->battleGetUnitByID(battle()->battleActiveUnit()->unitId()))};
	mechanics->castEval(child->getServerCallback(), target);
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
}
TEST_F(NewHorizonsWisdomAITest, BegunManualEpochRecordsSkippedEffectsOnceAndSeparatesSameSpellAutomaticCast)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	const auto prepared = child->prepareHeroSpellAllowance(SIDE, SPELL, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	// An automatic cast of the same spell must not satisfy the manual receipt.
	child->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), true);
	ASSERT_TRUE(child->projectAcceptedHeroSpell(SIDE, SPELL, 0, false, false, true, true, *prepared));
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 2u);
	EXPECT_FALSE(child->projectAcceptedHeroSpell(SIDE, SPELL, 0, false, false, true, true, *prepared));
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 2u);
}

TEST_F(NewHorizonsWisdomAITest, NormalCallbackAndAcceptedActionDoNotRecordTheSameCastTwice)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	const auto prepared = child->prepareHeroSpellAllowance(SIDE, SPELL, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	child->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	ASSERT_TRUE(child->projectAcceptedHeroSpell(SIDE, SPELL, 0, false, false, false, false, *prepared));
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 1u);
}

TEST_F(NewHorizonsWisdomAITest, SharedEffectSnapshotKeepsFirstInsightAndSubsequentMomentumAcrossRounds)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom("arcaneMomentum", "arcaneInsight"));
	const auto capture = [&] { return newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, child.get(), true, SPELL); };
	const auto first = capture();
	EXPECT_EQ(first.perks.wisdomEffectivenessBonus, 25);
	EXPECT_EQ(newHorizonsMetamagic::outgoingMagnitude(101, &first, nullptr), 126);
	child->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	EXPECT_EQ(capture().perks.wisdomEffectivenessBonus, 5);
	EXPECT_EQ(first.perks.wisdomEffectivenessBonus, 25);
	child->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	EXPECT_EQ(capture().perks.wisdomEffectivenessBonus, 5);
	child->nextRound();
	EXPECT_EQ(capture().perks.wisdomEffectivenessBonus, 5);
	child->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	EXPECT_EQ(capture().perks.wisdomEffectivenessBonus, 10);
	child->nextRound();
	child->nextRound();
	EXPECT_EQ(capture().perks.wisdomEffectivenessBonus, 0);
	EXPECT_EQ(battle()->getWisdomCombatState(SIDE).priorCasts(SPELL), 0u);
}

TEST_F(NewHorizonsWisdomAITest, SageIntelligenceCapacityClampsProjectedRecoveryInsideOneBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom("intelligence", "manaFasting", "sage"));
	const auto capacity = attackerSideHero->manaLimit();
	ASSERT_EQ(capacity, 195);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto before = sibling->getProjectedMana(SIDE);
	ASSERT_TRUE(before);
	SetMana recovery(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL, 1000);
	child->getServerCallback()->apply(static_cast<CPackForClient &>(recovery));
	ASSERT_TRUE(child->getProjectedMana(SIDE));
	EXPECT_EQ(child->getProjectedMana(SIDE)->getNormal(), capacity);
	EXPECT_EQ(sibling->getProjectedMana(SIDE)->getNormal(), before->getNormal());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), before->getNormal());
}

TEST_F(NewHorizonsWisdomAITest, ArrowObservationForwardsHistoryAndIncludesItInTheExactOutcome)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	const SpellID arrow(SpellID::MAGIC_ARROW);
	attackerSideHero->addSpellToSpellbook(arrow);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	sibling->getServerCallback()->recordCompletedWisdomSpellCast(SIDE, arrow, sibling->getRound(), true);
	const auto project = [&](HypotheticBattle & state)
	{
		const auto targets = state.battleGetUnitsIf([&](const battle::Unit * unit)
			{ return unit->alive() && unit->unitSide() == BattleSide::DEFENDER; });
		if(targets.empty()) return std::optional<arrowContinuation::Outcome>{};
		spells::BattleCast cast(&state, attackerSideHero, spells::Mode::HERO, arrow.toSpell());
		auto mechanics = arrow.toSpell()->battleMechanics(&cast);
		return arrowContinuation::project(*mechanics, state, {spells::Destination(targets.front())});
	};
	const auto first = project(*child);
	const auto repeated = project(*sibling);
	ASSERT_TRUE(first);
	ASSERT_TRUE(repeated);
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(arrow), 1u);
	EXPECT_EQ(sibling->getWisdomCombatState(SIDE).priorCasts(arrow), 2u);
	EXPECT_FALSE(first->wisdomHistory.empty());
	EXPECT_EQ(first->injury, repeated->injury);
	EXPECT_NE(first->wisdomHistory, repeated->wisdomHistory);
	EXPECT_NE(*first, *repeated);
	EXPECT_EQ(battle()->getWisdomCombatState(SIDE).priorCasts(arrow), 0u);
}

TEST_F(NewHorizonsWisdomAITest, DuplicateManualCallbackCannotMutateHistoryAndNextEpochAcceptsTheSameSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepareWisdom());
	const auto prepared = child->prepareHeroSpellAllowance(SIDE, SPELL, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	auto * server = child->getServerCallback();
	server->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	const auto once = projectedHitTest::serialized(child->getWisdomCombatState(SIDE));
	EXPECT_THROW(server->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false), std::logic_error);
	EXPECT_EQ(projectedHitTest::serialized(child->getWisdomCombatState(SIDE)), once);
	server->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), true);
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 2u);
	ASSERT_TRUE(child->projectAcceptedHeroSpell(SIDE, SPELL, 0, false, false, false, false, *prepared));
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 2u);
	child->nextRound();
	const auto next = child->prepareHeroSpellAllowance(SIDE, SPELL, false, false);
	ASSERT_TRUE(next);
	ASSERT_NE(next->action.epoch, prepared->action.epoch);
	ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *next));
	server->recordCompletedWisdomSpellCast(SIDE, SPELL, child->getRound(), false);
	ASSERT_TRUE(child->projectAcceptedHeroSpell(SIDE, SPELL, 0, false, false, false, false, *next));
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(SPELL), 3u);
	EXPECT_EQ(battle()->getWisdomCombatState(SIDE).priorCasts(SPELL), 0u);
}

}
