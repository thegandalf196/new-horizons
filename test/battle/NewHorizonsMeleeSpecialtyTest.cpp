/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMeleeSpecialtyFixture.h"
#include "../server/battles/BattleStartSnapshotFixture.h"
#include "../../server/battles/BattlePrimaryStrikeAftermath.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
namespace
{
class NewHorizonsMeleeSpecialtyTest : public NewHorizonsMeleeSpecialtyFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	void prepare(const char * hero, bool upgraded = false)
	{
		NewHorizonsMeleeSpecialtyFixture::prepare(hero, GetParam(), upgraded);
	}
	void sequence(bool upgraded)
	{
		prepare("core:sorsha", upgraded);
		ASSERT_EQ(actor->getTotalAttacks(false), upgraded ? 2 : 1);
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_TRUE(attack(actor, victim->getPosition()));
		ASSERT_TRUE(actor->alive()); ASSERT_TRUE(victim->alive());
		EXPECT_EQ(observed.attacks.size(), upgraded ? 3u : 2u);
		EXPECT_TRUE(std::ranges::none_of(observed.shieldAtAttack, [](bool value) { return value; }));
		EXPECT_EQ(observed.shieldPublications, GetParam() == 0 ? 1 : 0);
		const auto protection = actor->getUnstackedBonuses(CSelector(newHorizonsShieldAndSword::isProtection));
		EXPECT_EQ(protection->size(), GetParam() == 0 ? 1u : 0u);
	}
	void expectWorldIdentity(const CGameState & restored)
	{
		const auto policy = attackerSideHero->getPrimaryGrowthRules();
		const auto * hero = restored.getHero(attackerSideHero->id); ASSERT_NE(hero, nullptr);
		EXPECT_EQ(hero->getPrimaryGrowthRules(), policy);
		EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero), GetParam() != 1);
	}
};
}
TEST_P(NewHorizonsMeleeSpecialtyTest, SwordsmanGuardIsPublishedOnlyAfterTheCompletedSequence)
{
	ASSERT_NO_FATAL_FAILURE(sequence(false));
}
TEST_P(NewHorizonsMeleeSpecialtyTest, CrusaderDoubleStrikeAndCounterResolveBeforeOneGuardPublication)
{
	ASSERT_NO_FATAL_FAILURE(sequence(true));
}
TEST_P(NewHorizonsMeleeSpecialtyTest, SorshaRejectedOrUnexecutedSequenceGrantsNothing)
{
	prepare("core:sorsha");
	EXPECT_TRUE(newHorizonsShieldAndSword::completion(*battle(), *actor, false).add.empty());
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_FALSE(attack(actor, actor->getPosition()));
	EXPECT_EQ(observed.shieldPublications, 0);
	EXPECT_FALSE(newHorizonsShieldAndSword::eligible(*battle(), *victim));
	int64_t lethal = actor->getAvailableHealth(); actor->damage(lethal);
	EXPECT_TRUE(newHorizonsShieldAndSword::completion(*battle(), *actor, true).add.empty());
}
TEST_P(NewHorizonsMeleeSpecialtyTest, SorshaRefreshKeepsOneEffectAndPreservesPhysicalReductionCap)
{
	prepare("core:sorsha");
	ASSERT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(battle()->getMagicRules()), 80);
	const auto unguarded = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	// Spell-effect all-damage reductions have their own stage; use an eligible nonspell source.
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 80, BonusSourceID(), BonusCustomSubtype::damageTypeAll));
	const auto atCap = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	ASSERT_LT(atCap.damage.min, unguarded.damage.min);
	ASSERT_GT(atCap.damage.min, 0);
	for(int application = 0; application < 2; ++application)
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(),
			newHorizonsShieldAndSword::completion(*battle(), *actor, true));
	const auto guarded = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	EXPECT_EQ(guarded.damage.min, atCap.damage.min); EXPECT_EQ(guarded.damage.max, atCap.damage.max);
	const auto effects = actor->getUnstackedBonuses(CSelector(newHorizonsShieldAndSword::isProtection));
	ASSERT_EQ(effects->size(), GetParam() == 0 ? 1u : 0u);
	if(!effects->empty()) EXPECT_EQ(effects->front()->val, 20);
}
TEST_P(NewHorizonsMeleeSpecialtyTest, SorshaGuardExpiresAtGenuineActivationNotContinuation)
{
	prepare("core:sorsha");
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(),
		newHorizonsShieldAndSword::completion(*battle(), *actor, true));
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(actor->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)), GetParam() == 0);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
	EXPECT_EQ(actor->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)), GetParam() == 0);
	battle()->nextTurn(actor->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(actor->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)));
}
TEST_P(NewHorizonsMeleeSpecialtyTest, SorshaCapturedIdentityAndGuardSurviveCurrentWorldRoundtrip)
{
	prepare("core:sorsha");
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(),
		newHorizonsShieldAndSword::completion(*battle(), *actor, true));
	const auto original = actor->getUnstackedBonuses(CSelector(newHorizonsShieldAndSword::isProtection))->toJsonNode();
	CGameState restored; restored.preInit(LIBRARY); restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NO_FATAL_FAILURE(expectWorldIdentity(restored));
	// World snapshots omit battles. Roundtrip the real unit's exported marker through the
	// existing BattleStart descriptor writer, resolved against the restored world's heroes.
	ASSERT_EQ(restored.getBattle(BattleID(0)), nullptr);
	auto combat = battleStartFixture::snapshot(*battle(), &restored);
	ASSERT_NE(combat, nullptr);
	const auto * loaded = combat->getStack(actor->unitId(), false); ASSERT_NE(loaded, nullptr);
	BonusList decodedEffects;
	loaded->getExportedBonusList().getBonuses(decodedEffects, CSelector(newHorizonsShieldAndSword::isProtection));
	EXPECT_EQ(decodedEffects.toJsonNode(), original);
	// The BattleStart consumer binds decoded descriptors before live bonus queries.
	ASSERT_NO_THROW(combat->localInit());
	EXPECT_EQ(loaded->hasBonus(CSelector(newHorizonsShieldAndSword::isProtection)), GetParam() == 0);
	EXPECT_EQ(loaded->getUnstackedBonuses(CSelector(newHorizonsShieldAndSword::isProtection))->toJsonNode(), original);
}
TEST_P(NewHorizonsMeleeSpecialtyTest, StrakerActualPrimaryHitReducesMovementWithoutInitiativeOrBinding)
{
	prepare("core:straker");
	const auto movement = victim->getMovementRange(); const auto initiative = victim->getInitiative();
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(attack(actor, victim->getPosition()));
	ASSERT_TRUE(victim->alive());
	EXPECT_EQ(victim->getMovementRange(), movement - (GetParam() == 0 ? 1u : 0u));
	EXPECT_EQ(victim->getInitiative(), initiative);
	EXPECT_FALSE(victim->hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_FALSE(victim->hasBonusOfType(BonusType::NOT_ACTIVE));
	EXPECT_EQ(victim->hasBonus(CSelector(newHorizonsSepulchralGrip::isEffect)), GetParam() == 0);
}
TEST_P(NewHorizonsMeleeSpecialtyTest, UpgradedZombieUsesTheSamePrimaryHitRule)
{
	prepare("core:straker", true);
	const BattleAttackInfo hit(actor, victim, 0, false);
	const auto plan = newHorizonsOrdinaryStrike::plan(*battle(), hit, 1);
	ASSERT_EQ(plan.add.size(), GetParam() == 0 ? 1u : 0u);
	if(!plan.add.empty()) EXPECT_EQ(plan.add.front().val, -1);
}
TEST_P(NewHorizonsMeleeSpecialtyTest, GripRejectsZeroDamageSpellsAndSecondaryHitsButAllowsOrdinaryRetaliation)
{
	prepare("core:straker");
	BattleAttackInfo hit(actor, victim, 0, false);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), hit, 0).add.empty());
	for(int exclusion = 0; exclusion < 5; ++exclusion)
	{
		auto invalid = hit;
		if(exclusion == 0) invalid.shooting = true;
		if(exclusion == 1) invalid.physicalDamage = false;
		if(exclusion == 2) invalid.secondaryAttack = true;
		if(exclusion == 3) invalid.bracePreemptive = true;
		if(exclusion == 4) invalid.cleaveDamagePercent = 50;
		EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), invalid, 1).add.empty());
	}
	hit.retaliation = true;
	EXPECT_EQ(newHorizonsOrdinaryStrike::plan(*battle(), hit, 1).add.empty(), GetParam() != 0);
}
TEST_P(NewHorizonsMeleeSpecialtyTest, GripUsesHostilityAndDoesNotAffectFriendlyTargets)
{
	prepare("core:straker");
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 6), 10);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, ally, 0, false), 1).add.empty());
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, false), 1).add.empty());
}
TEST_P(NewHorizonsMeleeSpecialtyTest, GripPersistsThroughVictimActivationThenExpiresAfterItsAcceptedAction)
{
	prepare("core:straker");
	const auto movement = victim->getMovementRange();
	beginCombat(); ASSERT_TRUE(attack(actor, victim->getPosition()));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_EQ(victim->getMovementRange(), movement - (GetParam() == 0 ? 1u : 0u));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeDefend(victim)));
	EXPECT_FALSE(victim->hasBonus(CSelector(newHorizonsSepulchralGrip::isEffect)));
	EXPECT_EQ(victim->getMovementRange(), movement);
}
TEST_P(NewHorizonsMeleeSpecialtyTest, GripRefreshAndCurrentWorldRoundtripRetainOneCapturedEffect)
{
	prepare("core:straker");
	for(int application = 0; application < 2; ++application)
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(),
			newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(actor, victim, 0, false), 1));
	EXPECT_EQ(victim->getUnstackedBonuses(CSelector(newHorizonsSepulchralGrip::isEffect))->size(), GetParam() == 0 ? 1u : 0u);
	const auto original = victim->getUnstackedBonuses(CSelector(newHorizonsSepulchralGrip::isEffect))->toJsonNode();
	CGameState restored; restored.preInit(LIBRARY); restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NO_FATAL_FAILURE(expectWorldIdentity(restored));
	ASSERT_EQ(restored.getBattle(BattleID(0)), nullptr);
	auto combat = battleStartFixture::snapshot(*battle(), &restored);
	ASSERT_NE(combat, nullptr);
	const auto * loaded = combat->getStack(victim->unitId(), false); ASSERT_NE(loaded, nullptr);
	BonusList decodedEffects;
	loaded->getExportedBonusList().getBonuses(decodedEffects, CSelector(newHorizonsSepulchralGrip::isEffect));
	EXPECT_EQ(decodedEffects.toJsonNode(), original);
	// The BattleStart consumer binds decoded descriptors before live bonus queries.
	ASSERT_NO_THROW(combat->localInit());
	EXPECT_EQ(loaded->hasBonus(CSelector(newHorizonsSepulchralGrip::isEffect)), GetParam() == 0);
	EXPECT_EQ(loaded->getUnstackedBonuses(CSelector(newHorizonsSepulchralGrip::isEffect))->toJsonNode(), original);
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsMeleeSpecialtyTest, ::testing::Values(0, 1, 2));
TEST(NewHorizonsMeleeSpecialtyMarkers, ShieldAndSwordRetainsExactSingleTagAndRejectsMalformedLists)
{
	Bonus effect(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::HERO_SPECIAL, 20, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:sorsha"))),
		BonusCustomSubtype::damageTypeAll, BonusValueType::BASE_NUMBER);
	effect.stacking = "new-horizons:ordinary-creature-specialty:core:sorsha:shieldAndSword";
	effect.statusIdentity = "shieldAndSword";
	effect.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	EXPECT_TRUE(newHorizonsShieldAndSword::isProtection(&effect));
	for(const auto & tags : std::vector<std::vector<BonusStatusTag>>{
		{}, {BonusStatusTag::DEBUFF}, {BonusStatusTag::NON_TRANSFERABLE, BonusStatusTag::NON_TRANSFERABLE},
		{BonusStatusTag::NON_TRANSFERABLE, BonusStatusTag::DEBUFF}})
	{
		effect.statusTags = tags;
		EXPECT_THROW(newHorizonsShieldAndSword::isProtection(&effect), std::runtime_error);
	}
	EXPECT_FALSE(newHorizonsShieldAndSword::isProtection(nullptr));
	effect.stacking = "unrelated";
	EXPECT_FALSE(newHorizonsShieldAndSword::isProtection(&effect));
}
TEST(NewHorizonsMeleeSpecialtyMarkers, SepulchralGripRetainsExactOrderedTagsAndRejectsMissingDuplicateOrExtraTags)
{
	Bonus effect(BonusDuration::STACK_ACTIVATION, BonusType::STACKS_MOVEMENT_RANGE,
		BonusSource::HERO_SPECIAL, -1, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:straker"))));
	effect.stacking = "new-horizons:ordinary-creature-specialty:core:straker:sepulchralGrip";
	effect.statusIdentity = "sepulchralGrip";
	effect.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	EXPECT_TRUE(newHorizonsSepulchralGrip::isEffect(&effect));
	for(const auto & tags : std::vector<std::vector<BonusStatusTag>>{
		{}, {BonusStatusTag::DEBUFF}, {BonusStatusTag::NON_TRANSFERABLE, BonusStatusTag::DEBUFF},
		{BonusStatusTag::DEBUFF, BonusStatusTag::DEBUFF},
		{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE, BonusStatusTag::DEBUFF}})
	{
		effect.statusTags = tags;
		EXPECT_THROW(newHorizonsSepulchralGrip::isEffect(&effect), std::runtime_error);
	}
	EXPECT_FALSE(newHorizonsSepulchralGrip::isEffect(nullptr));
	effect.stacking = "unrelated";
	EXPECT_FALSE(newHorizonsSepulchralGrip::isEffect(&effect));
}
