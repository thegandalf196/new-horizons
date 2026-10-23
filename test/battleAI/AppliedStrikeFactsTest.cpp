/*
 * AppliedStrikeFactsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <bit>
#include <cmath>

class AppliedStrikeFactsTest : public ProjectedTargetHitFixture
{
protected:
	using Bytes = std::vector<std::byte>;
	using Snapshot = std::vector<Bytes>;
	using Branch = std::shared_ptr<HypotheticBattle>;

	void mapLoaded(CMap * map) override
	{
		ProjectedTargetHitFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepareScene()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		forceMaximumDamage(incoming);
		resetChild();
	}

	Branch fork()
	{
		// Match the established fixture's setup receipt, before isolation snapshots.
		for(const auto * unit : child->battleGetAllUnits(false))
			(void)child->getForUpdate(unit->unitId())->counterAttacks.available();
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), child);
		for(const auto * unit : branch->battleGetAllUnits(false))
			(void)branch->getForUpdate(unit->unitId())->counterAttacks.available();
		return branch;
	}

	Snapshot snapshot(const Branch & branch)
	{
		Snapshot result;
		for(const auto * unit : branch->battleGetAllUnits(false))
		{
			const auto projected = branch->getForUpdate(unit->unitId());
			result.push_back(projectedHitTest::serialized(projected->save()));
			const auto owner = projected->getUnstackedBonuses(Selector::all);
			std::vector<JsonNode> bonuses;
			for(const auto & bonus : *owner)
				bonuses.push_back(bonus->toJsonNode());
			result.push_back(projectedHitTest::serialized(bonuses));
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			result.push_back(projectedHitTest::serialized(branch->getSylvanLuckState(side)));
			result.push_back(projectedHitTest::serialized(branch->getPerfectFortuneState(side)));
			result.push_back(projectedHitTest::serialized(branch->getLuckSerendipityState(side)));
			result.push_back(projectedHitTest::serialized(branch->getRelentlessAssaultState(side)));
			result.push_back(projectedHitTest::serialized(branch->getHeroOrderStates(side)));
			result.push_back(projectedHitTest::serialized(branch->getHeroActionAllowances(side)));
		}
		result.push_back(projectedHitTest::serialized(branch->getActiveStackID()));
		result.push_back(projectedHitTest::serialized(branch->battleGetRound()));
		return result;
	}

	Snapshot nativeSnapshot()
	{
		return {gameState()->saveToMemory(), projectedHitTest::serialized(*battle()),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}

	int64_t damage(const Branch & branch)
	{
		const auto actor = branch->getForUpdate(incoming->unitId());
		const auto target = branch->getForUpdate(braced->unitId());
		return branch->battleExpectedLuckDamage(BattleAttackInfo(actor.get(), target.get(), 0, false));
	}

	float score(const Branch & branch)
	{
		DamageCache cache;
		return AttackPossibility::calculateDamageReduce(branch->getForUpdate(incoming->unitId()).get(),
			branch->getForUpdate(braced->unitId()).get(), damage(branch), cache, branch);
	}

	float strike(const Branch & branch, bool probe, bool retaliation = false)
	{
		DamageCache cache;
		BattleExchangeVariant exchange;
		return exchange.trackAttack(branch->getForUpdate(incoming->unitId()),
			branch->getForUpdate(braced->unitId()), false, true, cache, branch, probe, retaliation);
	}

	void expectScore(float actual, float expected)
	{
		EXPECT_TRUE(std::isfinite(actual));
		EXPECT_EQ(std::bit_cast<uint32_t>(actual), std::bit_cast<uint32_t>(expected));
	}
};

TEST_F(AppliedStrikeFactsTest, NightProwlerProbePreservesMarkerAndAppliedHitConsumesItOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepareScene());
	child->getForUpdate(incoming->unitId())->addUnitBonus(newHorizonsShroud::nightProwlerDamageBonuses());
	auto ordinary = fork();
	ordinary->getForUpdate(incoming->unitId())->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
	const auto ordinaryDamage = damage(ordinary);
	const auto boostedDamage = damage(child);
	ASSERT_GT(ordinaryDamage, 0);
	ASSERT_GT(boostedDamage, ordinaryDamage);
	ASSERT_LT(boostedDamage, child->getForUpdate(braced->unitId())->getAvailableHealth());
	auto probe = fork(), applied = fork(), sibling = fork();
	const auto expected = score(probe);
	const auto probeBefore = snapshot(probe), parentBefore = snapshot(child), siblingBefore = snapshot(sibling);
	const auto nativeBefore = nativeSnapshot();
	const auto marker = CSelector(newHorizonsShroud::isNightProwlerBonus);
	ASSERT_TRUE(probe->getForUpdate(incoming->unitId())->hasBonus(marker));
	expectScore(strike(probe, true), expected);
	expectScore(strike(probe, true), expected);
	EXPECT_TRUE(probe->getForUpdate(incoming->unitId())->hasBonus(marker));
	EXPECT_EQ(snapshot(probe), probeBefore);

	auto target = applied->getForUpdate(braced->unitId());
	const auto firstHealth = target->getAvailableHealth();
	expectScore(strike(applied, false), expected);
	EXPECT_EQ(target->getAvailableHealth(), firstHealth - boostedDamage);
	EXPECT_FALSE(applied->getForUpdate(incoming->unitId())->hasBonus(marker));
	EXPECT_EQ(damage(applied), ordinaryDamage);
	const auto secondHealth = target->getAvailableHealth();
	EXPECT_TRUE(std::isfinite(strike(applied, false)));
	EXPECT_EQ(target->getAvailableHealth(), secondHealth - ordinaryDamage);
	EXPECT_FALSE(applied->getForUpdate(incoming->unitId())->hasBonus(marker));
	EXPECT_EQ(snapshot(child), parentBefore);
	EXPECT_EQ(snapshot(sibling), siblingBefore);
	EXPECT_EQ(nativeSnapshot(), nativeBefore);
}

TEST_F(AppliedStrikeFactsTest, BastionProbePricesProtectionAndAppliedHitsSpendItOnlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepareScene());
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::BASIC,
		std::string(newHorizonsCombatSkills::PAVISE_PERK_ID)));
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::ADVANCED,
		std::string(newHorizonsCombatSkills::VETERAN_PERK_ID)));
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::EXPERT,
		std::string(newHorizonsCombatSkills::BASTION_PERK_ID)));
	activate(braced->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(braced)));
	activate(incoming->unitId());
	resetChild();
	ASSERT_TRUE(child->getForUpdate(braced->unitId())->defended());
	ASSERT_TRUE(child->battleHasBastionProtection(child->getForUpdate(braced->unitId()).get()));
	auto spent = fork();
	spent->getForUpdate(braced->unitId())->armorerBastionRound = spent->battleGetRound();
	const auto ordinaryDamage = damage(spent), protectedDamage = damage(child);
	ASSERT_GT(protectedDamage, 0);
	ASSERT_LT(protectedDamage, ordinaryDamage);
	ASSERT_LT(protectedDamage, child->getForUpdate(braced->unitId())->getAvailableHealth());
	const auto scaled = ordinaryDamage * newHorizonsCombatSkills::BASTION_FINAL_DAMAGE_MULTIPLIER / 100;
	EXPECT_GE(protectedDamage, scaled - 1);
	EXPECT_LE(protectedDamage, scaled + 1);
	auto probe = fork(), applied = fork(), sibling = fork();
	const auto expected = score(probe);
	const auto probeBefore = snapshot(probe), parentBefore = snapshot(child), siblingBefore = snapshot(sibling);
	const auto nativeBefore = nativeSnapshot();
	const auto markerBefore = probe->getForUpdate(braced->unitId())->armorerBastionRound;
	expectScore(strike(probe, true), expected);
	expectScore(strike(probe, true), expected);
	EXPECT_EQ(probe->getForUpdate(braced->unitId())->armorerBastionRound, markerBefore);
	EXPECT_TRUE(probe->battleHasBastionProtection(probe->getForUpdate(braced->unitId()).get()));
	EXPECT_EQ(snapshot(probe), probeBefore);

	auto target = applied->getForUpdate(braced->unitId());
	const auto firstHealth = target->getAvailableHealth();
	expectScore(strike(applied, false), expected);
	EXPECT_EQ(target->getAvailableHealth(), firstHealth - protectedDamage);
	EXPECT_EQ(target->armorerBastionRound, applied->battleGetRound());
	EXPECT_FALSE(applied->battleHasBastionProtection(target.get()));
	EXPECT_EQ(damage(applied), ordinaryDamage);
	const auto secondHealth = target->getAvailableHealth();
	EXPECT_TRUE(std::isfinite(strike(applied, false)));
	EXPECT_EQ(target->getAvailableHealth(), secondHealth - ordinaryDamage);
	EXPECT_EQ(target->armorerBastionRound, applied->battleGetRound());
	EXPECT_EQ(snapshot(child), parentBefore);
	EXPECT_EQ(snapshot(sibling), siblingBefore);
	EXPECT_EQ(nativeSnapshot(), nativeBefore);
}

TEST_F(AppliedStrikeFactsTest, FrozenProbeKeepsShatterPremiumAndAppliedHitThawsWithoutRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepareScene());
	ASSERT_EQ(newHorizonsFrozen::shatterBonusPercent(battle()->getMagicRules()), 25);
	SetStackEffect frozen;
	frozen.battleID = BattleID(0);
	frozen.toAdd.emplace_back(braced->unitId(), std::vector<Bonus>{newHorizonsFrozen::makeFrozenMarker(
		BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
	gameHandler->sendAndApply(frozen);
	resetChild();
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*child->getForUpdate(braced->unitId())));
	auto thawed = fork();
	const auto plainTarget = thawed->getForUpdate(braced->unitId());
	plainTarget->removeUnitBonus(newHorizonsFrozen::removalPlan(*plainTarget));
	const auto ordinaryDamage = damage(thawed), shatterDamage = damage(child);
	ASSERT_GT(ordinaryDamage, 0);
	ASSERT_GT(shatterDamage, ordinaryDamage);
	ASSERT_LT(shatterDamage, child->getForUpdate(braced->unitId())->getAvailableHealth());
	EXPECT_GE(shatterDamage, ordinaryDamage * 125 / 100 - 1);
	EXPECT_LE(shatterDamage, ordinaryDamage * 125 / 100 + 1);
	auto probe = fork(), applied = fork(), sibling = fork();
	const auto expected = score(probe);
	const auto probeBefore = snapshot(probe), parentBefore = snapshot(child), siblingBefore = snapshot(sibling);
	const auto nativeBefore = nativeSnapshot();
	const auto stamp = applied->getForUpdate(braced->unitId())->frozenLastAppliedRound();
	expectScore(strike(probe, true, true), expected);
	expectScore(strike(probe, true, true), expected);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*probe->getForUpdate(braced->unitId())));
	EXPECT_EQ(snapshot(probe), probeBefore);

	auto actor = applied->getForUpdate(incoming->unitId()), target = applied->getForUpdate(braced->unitId());
	const auto actorHealth = actor->getAvailableHealth(), targetHealth = target->getAvailableHealth();
	const auto retaliations = target->counterAttacks.available();
	ASSERT_GT(retaliations, 0);
	expectScore(strike(applied, false, true), expected);
	EXPECT_EQ(target->getAvailableHealth(), targetHealth - shatterDamage);
	EXPECT_EQ(actor->getAvailableHealth(), actorHealth);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), stamp);
	EXPECT_EQ(target->counterAttacks.available(), retaliations);
	ASSERT_TRUE(target->ableToRetaliate());
	const auto secondHealth = target->getAvailableHealth();
	EXPECT_TRUE(std::isfinite(strike(applied, false, true)));
	EXPECT_EQ(target->getAvailableHealth(), secondHealth - ordinaryDamage);
	EXPECT_LT(actor->getAvailableHealth(), actorHealth);
	EXPECT_EQ(target->counterAttacks.available(), retaliations - 1);
	EXPECT_EQ(snapshot(child), parentBefore);
	EXPECT_EQ(snapshot(sibling), siblingBefore);
	EXPECT_EQ(nativeSnapshot(), nativeBefore);
}
