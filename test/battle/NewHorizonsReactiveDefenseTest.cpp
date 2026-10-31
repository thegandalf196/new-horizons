/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsReactiveDefenseFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../server/battles/BattlePrimaryStrikeAftermath.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
template<typename Random> auto reactiveRandomBytes(Random & random)
{
	CMemorySerializer memory;
	memory.oser & random;
	return memory.extractBuffer();
}
class NewHorizonsReactiveDefenseTest : public NewHorizonsReactiveDefenseFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	bool enabled() const { return GetParam() == 0; }
	size_t guardCount() const
	{
		return subject->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isSolidarityProtection))->size();
	}
	UnitChanges preparedDamage(CStack & unit, int64_t amount)
	{
		const auto original = unit.save();
		unit.damage(amount);
		UnitChanges change(unit.unitId(), UnitChanges::EOperation::UPDATE);
		change.data = unit.save(); change.healthDelta = -amount;
		unit.load(original);
		return change;
	}
	const battle::CUnitStateDetached * ownedUnit(const battle::ProjectedBattleWorldArena & arena, uint32_t id)
	{
		for(size_t index = 0; index != arena.size(); ++index)
			if(arena.unitAt(index).unitId() == id) return &arena.unitAt(index);
		return nullptr;
	}
	void exactIronHidePipeline(int physicalPolicy)
	{
		prepare(false, GetParam(), false, physicalPolicy);
		ASSERT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(battle()->getMagicRules()), physicalPolicy == 0 ? 80 : -1);
		for(const auto & [unit, skill] : {std::pair{enemy, PrimarySkill::ATTACK}, std::pair{subject, PrimarySkill::DEFENSE}})
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
					BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(skill), bound));
		ASSERT_EQ(enemy->getAttack(false), 0); ASSERT_EQ(subject->getDefense(false), 0);
		const auto attack = enemy->getAttack(false); const auto defense = subject->getDefense(false);
		BattleAttackInfo hit(enemy, subject, 0, false);
		const auto protectedRange = battle()->calculateDmgRange(hit);
		if(const auto marker = newHorizonsReactiveDefense::ironHideSpending(*battle(), hit)) markSpent(*marker);
		const auto spentRange = battle()->calculateDmgRange(hit);
		ASSERT_EQ(spentRange.damage.min, 2000); ASSERT_EQ(spentRange.damage.max, 2000);
		EXPECT_EQ(protectedRange.damage.min, enabled() ? 1400 : 2000);
		EXPECT_EQ(protectedRange.damage.max, enabled() ? 1400 : 2000);
		EXPECT_EQ(protectedRange.damageBeforeDefense.min, 2000);
		EXPECT_EQ(protectedRange.damageBeforeDefense.max, 2000);
		EXPECT_EQ(enemy->getAttack(false), attack); EXPECT_EQ(subject->getDefense(false), defense);
	}
	void markSpent(const Bonus & marker)
	{
		SetStackEffect effect; effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(subject->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(effect);
	}
};
}
TEST_P(NewHorizonsReactiveDefenseTest, DifferentFriendlyCombatDestructionProtectsSurvivingDemons)
{
	prepare(true, GetParam());
	EXPECT_EQ(guardCount(), 0u);
	injure({{ally, ally->getAvailableHealth()}});
	EXPECT_FALSE(ally->alive());
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	if(enabled()) EXPECT_EQ(subject->valOfBonuses(CSelector(newHorizonsReactiveDefense::isSolidarityProtection)), 20);
}
TEST_P(NewHorizonsReactiveDefenseTest, EnemyDestructionDoesNotProtectDemons)
{
	prepare(true, GetParam());
	injure({{enemy, enemy->getAvailableHealth()}});
	EXPECT_FALSE(enemy->alive()); EXPECT_EQ(guardCount(), 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, WholeDamagePacketFinishesBeforeSolidarityIsGranted)
{
	prepare(true, GetParam());
	const auto before = subject->getAvailableHealth();
	injure({{ally, ally->getAvailableHealth()}, {subject, 10}});
	EXPECT_EQ(subject->getAvailableHealth(), before - 10);
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, CombatSummonAndCloneDestructionQualifyButDismissalDoesNot)
{
	prepare(true, GetParam());
	const auto summoned = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 1), 1);
	BattleUnitsChanged state; state.battleID = BattleID(0);
	state.changedStacks.emplace_back(summoned->unitId(), UnitChanges::EOperation::UPDATE);
	state.changedStacks.back().data = summoned->acquireState()->save();
	state.changedStacks.back().data["state"]["summoned"].Bool() = true;
	gameHandler->sendAndApply(state);
	injure({{summoned, summoned->getAvailableHealth()}});
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	if(enabled()) subject->removeBonusesRecursive(CSelector(newHorizonsReactiveDefense::isSolidarityProtection));
	const auto clone = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 2), 1);
	state.changedStacks.clear(); state.changedStacks.emplace_back(clone->unitId(), UnitChanges::EOperation::UPDATE);
	state.changedStacks.back().data = clone->acquireState()->save();
	state.changedStacks.back().data["state"]["cloned"].Bool() = true;
	gameHandler->sendAndApply(state);
	injure({{clone, clone->getAvailableHealth()}});
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	if(enabled()) subject->removeBonusesRecursive(CSelector(newHorizonsReactiveDefense::isSolidarityProtection));
	BattleUnitsChanged dismissal; dismissal.battleID = BattleID(0);
	dismissal.changedStacks.emplace_back(ally->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(dismissal);
	EXPECT_EQ(guardCount(), 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, SolidarityStrongestRefreshDoesNotAddPercentages)
{
	prepare(true, GetParam(), true);
	injure({{ally, ally->getAvailableHealth()}});
	if(enabled()) battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(),
		newHorizonsReactiveDefense::solidarityProtection(*subject, 40));
	const auto next = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 1), 1);
	injure({{next, next->getAvailableHealth()}});
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	if(enabled()) EXPECT_EQ(subject->valOfBonuses(CSelector(newHorizonsReactiveDefense::isSolidarityProtection)), 40);
}
TEST_P(NewHorizonsReactiveDefenseTest, IronHideProtectsOnlyFirstHostilePrimaryMeleeHit)
{
	prepare(false, GetParam());
	BattleAttackInfo hit(enemy, subject, 0, false);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), enabled() ? 30 : 0);
	const auto before = battle()->calculateDmgRange(hit);
	const auto marker = newHorizonsReactiveDefense::ironHideSpending(*battle(), hit);
	ASSERT_EQ(marker.has_value(), enabled());
	if(marker) markSpent(*marker);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), 0);
	const auto after = battle()->calculateDmgRange(hit);
	EXPECT_EQ(before.damageBeforeDefense.min, after.damageBeforeDefense.min);
	EXPECT_EQ(before.damageBeforeDefense.max, after.damageBeforeDefense.max);
	if(enabled()) EXPECT_LT(before.damage.min, after.damage.min);
	else EXPECT_EQ(before.damage.min, after.damage.min);
}
TEST_P(NewHorizonsReactiveDefenseTest, OrdinaryRetaliationQualifiesButSecondaryPreemptiveAndRangedDoNot)
{
	prepare(false, GetParam(), true);
	BattleAttackInfo hit(enemy, subject, 0, false); hit.retaliation = true;
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), enabled() ? 30 : 0);
	auto friendly = hit; friendly.attacker = ally;
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), friendly), 0);
	for(int kind = 0; kind != 4; ++kind)
	{
		auto excluded = hit;
		if(kind == 0) excluded.secondaryAttack = true;
		if(kind == 1) excluded.bracePreemptive = true;
		if(kind == 2) excluded.shooting = true;
		if(kind == 3) excluded.physicalDamage = false;
		EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), excluded), 0);
		EXPECT_FALSE(newHorizonsReactiveDefense::ironHideSpending(*battle(), excluded));
	}
}
TEST_P(NewHorizonsReactiveDefenseTest, PreparedZeroDamagePrimaryCommitStillSpendsIronHide)
{
	prepare(false, GetParam());
	BattleAttackInfo hit(enemy, subject, 0, false);
	BattleStackAttacked primary; primary.stackAttacked = subject->unitId(); primary.damageAmount = 0;
	const auto prepared = battlePrimaryStrikeAftermath::prepare(*battle(), hit, {primary});
	battlePrimaryStrikeAftermath::apply(*gameHandler, *battle(), prepared);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), 0);
	EXPECT_EQ(subject->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent))->size(), enabled() ? 1u : 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, NativeTimeStoppedRoundKeepsOldMarkerWithoutConsumingTheNewRound)
{
	prepare(false, GetParam());
	battle()->nextRound();
	BattleAttackInfo hit(enemy, subject, 0, false);
	const auto marker = newHorizonsReactiveDefense::ironHideSpending(*battle(), hit);
	ASSERT_EQ(marker.has_value(), enabled());
	if(marker) markSpent(*marker);
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	SetStackEffect freeze; freeze.battleID = BattleID(0);
	freeze.toAdd.emplace_back(subject->unitId(), std::vector<Bonus>{stopped});
	gameHandler->sendAndApply(freeze); ASSERT_TRUE(subject->isTimeStopped());
	const auto oldRound = battle()->battleGetRound();
	battle()->nextRound(); ASSERT_GT(battle()->battleGetRound(), oldRound);
	const auto spent = subject->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent));
	ASSERT_EQ(spent->size(), enabled() ? 1u : 0u);
	if(enabled()) { EXPECT_EQ(spent->front()->val, oldRound); EXPECT_EQ(spent->front()->turnsRemain, 1); }
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), 0);
	SetStackEffect thaw; thaw.battleID = BattleID(0);
	thaw.toRemove.emplace_back(subject->unitId(), std::vector<Bonus>{stopped});
	gameHandler->sendAndApply(thaw); ASSERT_FALSE(subject->isTimeStopped());
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), enabled() ? 30 : 0);
}

TEST_P(NewHorizonsReactiveDefenseTest, ActualOrdinaryCounterPublishesOneSpentReceipt)
{
	prepare(false, GetParam());
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), subject);
	ASSERT_TRUE(attack(subject, enemy->getPosition()));
	ASSERT_TRUE(subject->alive()); ASSERT_TRUE(enemy->alive());
	ASSERT_TRUE(std::ranges::any_of(server.attacks, [this](const BattleAttack & hit)
		{ return hit.counter() && hit.stackAttacking == enemy->unitId(); }));
	EXPECT_EQ(subject->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent))->size(), enabled() ? 1u : 0u);
	BattleAttackInfo next(enemy, subject, 0, false);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), next), 0);
}
TEST_P(NewHorizonsReactiveDefenseTest, ActualLethalCounterStillSpendsProtectionAcrossSameRoundResurrection)
{
	prepare(false, GetParam());
	std::vector<std::shared_ptr<Bonus>> oldBounds;
	for(const auto & bonus : enemy->getExportedBonusList())
		if(bonus->type == BonusType::CREATURE_DAMAGE && bonus->source == BonusSource::OTHER)
			oldBounds.push_back(bonus);
	for(const auto & bonus : oldBounds) enemy->removeBonus(bonus);
	for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
				BonusSource::OTHER, 100, BonusSourceID(), subtype, bound));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), subject);
	ASSERT_TRUE(attack(subject, enemy->getPosition()));
	ASSERT_TRUE(std::ranges::any_of(server.attacks, [this](const BattleAttack & hit)
		{ return hit.counter() && hit.stackAttacking == enemy->unitId(); }));
	ASSERT_FALSE(subject->alive());
	EXPECT_EQ(subject->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent))->size(), enabled() ? 1u : 0u);
	auto restored = subject->acquireState();
	int64_t amount = subject->getMaxHealth();
	restored->heal(amount, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	BattleUnitsChanged revival; revival.battleID = BattleID(0);
	revival.changedStacks.emplace_back(subject->unitId(), UnitChanges::EOperation::UPDATE);
	revival.changedStacks.back().data = restored->save(); revival.changedStacks.back().healthDelta = amount;
	gameHandler->sendAndApply(revival); ASSERT_TRUE(subject->alive());
	BattleAttackInfo next(enemy, subject, 0, false);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), next), 0);
}

TEST_P(NewHorizonsReactiveDefenseTest, SuccessfulSameUnitNativeRebirthIsNotADestruction)
{
	prepare(true, GetParam());
	const auto phoenix = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(2, 1), 10);
	StacksInjured packet; packet.battleID = BattleID(0);
	auto & hit = packet.stacks.emplace_back();
	hit.stackAttacked = phoenix->unitId(); hit.damageAmount = phoenix->getAvailableHealth();
	phoenix->prepareAttacked(hit, gameHandler->getRandomGenerator());
	ASSERT_TRUE(hit.willRebirth());
	gameHandler->sendAndApply(packet);
	ASSERT_TRUE(phoenix->alive()); EXPECT_EQ(guardCount(), 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, SolidarityPlanDoesNotMutateUnitsOrEitherRandomStream)
{
	prepare(true, GetParam());
	const auto death = newHorizonsReactiveDefense::captureDestruction(*ally); ASSERT_TRUE(death);
	int64_t lethal = ally->getAvailableHealth(); ally->damage(lethal); ASSERT_FALSE(ally->alive());
	const auto before = subject->acquireState()->save();
	auto * random = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()); ASSERT_NE(random, nullptr);
	const auto world = reactiveRandomBytes(*random);
	const auto rolls = reactiveRandomBytes(*gameHandler->randomizer);
	for(int query = 0; query != 8; ++query)
	{
		const auto plans = newHorizonsReactiveDefense::solidarityAftermath(*battle(), std::span(&*death, 1));
		EXPECT_EQ(plans.size(), enabled() ? 1u : 0u);
	}
	EXPECT_EQ(subject->acquireState()->save(), before); EXPECT_EQ(guardCount(), 0u);
	EXPECT_EQ(reactiveRandomBytes(*random), world);
	EXPECT_EQ(reactiveRandomBytes(*gameHandler->randomizer), rolls);
}
TEST_P(NewHorizonsReactiveDefenseTest, IronHideQueriesDoNotSpendOrMutateEitherRandomStream)
{
	prepare(false, GetParam());
	const auto before = subject->acquireState()->save();
	auto * random = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()); ASSERT_NE(random, nullptr);
	const auto world = reactiveRandomBytes(*random);
	const auto rolls = reactiveRandomBytes(*gameHandler->randomizer);
	BattleAttackInfo hit(enemy, subject, 0, false);
	for(int query = 0; query != 8; ++query)
	{
		EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), enabled() ? 30 : 0);
		EXPECT_EQ(newHorizonsReactiveDefense::ironHideSpending(*battle(), hit).has_value(), enabled());
		(void)battle()->calculateDmgRange(hit);
	}
	EXPECT_EQ(subject->acquireState()->save(), before);
	EXPECT_FALSE(subject->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
	EXPECT_EQ(reactiveRandomBytes(*random), world);
	EXPECT_EQ(reactiveRandomBytes(*gameHandler->randomizer), rolls);
}
TEST_P(NewHorizonsReactiveDefenseTest, OwnedPacketMatchesNativePostWholeBatchProtectionAndKeepsSourceUnchanged)
{
	prepare(true, GetParam());
	const auto death = preparedDamage(*ally, ally->getAvailableHealth());
	const auto wound = preparedDamage(*subject, 10);
	const auto live = subject->save();
	{
		battle::ProjectedBattleWorldArena arena(*battle());
		arena.applyUnitChanges({death, wound});
		const auto * copied = ownedUnit(arena, subject->unitId()); ASSERT_NE(copied, nullptr);
		EXPECT_EQ(copied->getAvailableHealth(), subject->getAvailableHealth() - 10);
		EXPECT_EQ(copied->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isSolidarityProtection))->size(), enabled() ? 1u : 0u);
		EXPECT_EQ(subject->save(), live); EXPECT_TRUE(ally->alive()); EXPECT_EQ(guardCount(), 0u);
	}
	BattleUnitsChanged packet; packet.battleID = BattleID(0); packet.changedStacks = {death, wound};
	gameHandler->sendAndApply(packet);
	EXPECT_FALSE(ally->alive()); EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, OwnedWirePacketKeepsSameUnitRevivalAndDismissalDistinctFromCombatDeath)
{
	prepare(true, GetParam());
	const auto death = preparedDamage(*ally, ally->getAvailableHealth());
	UnitChanges revival(ally->unitId(), UnitChanges::EOperation::UPDATE);
	revival.data = ally->save(); revival.healthDelta = ally->getAvailableHealth();
	{
		battle::ProjectedBattleWorldArena arena(*battle());
		BattleUnitsChanged packet; packet.battleID = BattleID(0); packet.changedStacks = {death, revival};
		arena.applyPacket(packet);
		const auto * copied = ownedUnit(arena, subject->unitId()); ASSERT_NE(copied, nullptr);
		EXPECT_FALSE(copied->hasBonus(CSelector(newHorizonsReactiveDefense::isSolidarityProtection)));
		EXPECT_TRUE(ownedUnit(arena, ally->unitId())->alive());
	}
	{
		battle::ProjectedBattleWorldArena arena(*battle());
		arena.applyUnitChanges({UnitChanges(ally->unitId(), UnitChanges::EOperation::REMOVE)});
		const auto * copied = ownedUnit(arena, subject->unitId()); ASSERT_NE(copied, nullptr);
		EXPECT_FALSE(copied->hasBonus(CSelector(newHorizonsReactiveDefense::isSolidarityProtection)));
	}
	EXPECT_TRUE(ally->alive()); EXPECT_EQ(guardCount(), 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, NativeGenuineActivationExpiresSolidarityButHeroSpellAndContinuationDoNot)
{
	prepare(true, GetParam()); injure({{ally, ally->getAvailableHealth()}});
	battle()->nextTurn(subject->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	battle()->nextTurn(subject->unitId(), BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	battle()->nextTurn(subject->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_EQ(guardCount(), 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, MariusUsesVisibleOriginalArmyPolicyThroughHypnosisAndPuppetControl)
{
	prepare(true, GetParam());
	const CPlayerBattleCallback known(battle(), PlayerColor(0));
	const CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	for(auto * unit : {subject, ally})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(subject), PlayerColor(1));
	EXPECT_EQ(newHorizonsReactiveDefense::solidarityConfiguredPercent(known, BattleSide::ATTACKER), enabled() ? 20 : 0);
	EXPECT_EQ(newHorizonsReactiveDefense::solidarityConfiguredPercent(hidden, BattleSide::ATTACKER), 0);
	injure({{ally, ally->getAvailableHealth()}});
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
	subject->removeBonusesRecursive(Selector::type()(BonusType::HYPNOTIZED));
	subject->removeBonusesRecursive(CSelector(newHorizonsReactiveDefense::isSolidarityProtection));
	const auto other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 1), 1);
	const auto puppet = newHorizonsPuppetMaster::controlMarker(SpellID(SpellID::decode("new-horizons:puppetMaster")), PlayerColor(1));
	for(auto * unit : {subject, other}) unit->addNewBonus(std::make_shared<Bonus>(puppet));
	ASSERT_EQ(battle()->battleGetOwner(subject), PlayerColor(0));
	ASSERT_EQ(battle()->battleGetActionController(subject), PlayerColor(1));
	injure({{other, other->getAvailableHealth()}});
	EXPECT_EQ(guardCount(), enabled() ? 1u : 0u);
}
TEST_P(NewHorizonsReactiveDefenseTest, AlkinKnownAndAuthorityViewsUsePolicyWhileEnemyViewCannotReadIt)
{
	prepare(false, GetParam());
	const CPlayerBattleCallback known(battle(), PlayerColor(0));
	const CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(known.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).legacyHero(), attackerSideHero);
	ASSERT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	BattleAttackInfo hit(enemy, subject, 0, false);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(known, hit), enabled() ? 30 : 0);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*battle(), hit), enabled() ? 30 : 0);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(hidden, hit), 0);
}
TEST_P(NewHorizonsReactiveDefenseTest, PublishedSolidarityGuardIsObservableWithoutReadingTheHiddenHeroPolicy)
{
	prepare(true, GetParam());
	const CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	BattleAttackInfo hit(enemy, subject, 0, false);
	const auto before = hidden.calculateDmgRange(hit);
	injure({{ally, ally->getAvailableHealth()}});
	EXPECT_EQ(newHorizonsReactiveDefense::solidarityConfiguredPercent(hidden, BattleSide::ATTACKER), 0);
	const auto after = hidden.calculateDmgRange(hit);
	if(enabled()) EXPECT_LT(after.damage.min, before.damage.min);
	else EXPECT_EQ(after.damage.min, before.damage.min);
}
TEST_P(NewHorizonsReactiveDefenseTest, ExactThirtyPercentUsesCurrentCappedPhysicalPipeline)
{
	ASSERT_NO_FATAL_FAILURE(exactIronHidePipeline(0));
}
TEST_P(NewHorizonsReactiveDefenseTest, ExactThirtyPercentPreservesLegacyIndependentProtectionPipeline)
{
	ASSERT_NO_FATAL_FAILURE(exactIronHidePipeline(1));
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsReactiveDefenseTest, ::testing::Values(0, 1, 2));
