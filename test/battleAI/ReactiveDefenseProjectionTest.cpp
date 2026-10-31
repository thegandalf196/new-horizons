/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsReactiveDefenseFixture.h"
#include "../../AI/BattleAI/PhysicalStrikeProjection.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/ProjectedBestAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>
namespace
{
class ReactiveEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ReactiveEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
template<typename Random> auto randomBytes(Random & value)
{
	CMemorySerializer memory; memory.oser & value; return memory.extractBuffer();
}
class ReactiveDefenseProjectionTest : public NewHorizonsReactiveDefenseFixture
{
protected:
	std::unique_ptr<ReactiveEnvironment> environment;
	std::shared_ptr<HypotheticBattle> projected;
	void setup(bool marius, int captureMode = 0)
	{
		if(projected)
		{
			projected.reset();
			environment.reset();
			NewHorizonsReactiveDefenseFixture::TearDown();
			NewHorizonsReactiveDefenseFixture::SetUp();
			subject = ally = enemy = nullptr;
		}
		ASSERT_NO_FATAL_FAILURE(prepare(marius, captureMode));
		environment = std::make_unique<ReactiveEnvironment>(gameState());
		auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		projected = std::make_shared<HypotheticBattle>(environment.get(), known);
	}
	bool protectedDemon(const HypotheticBattle & branch) const
	{
		return branch.battleGetUnitByID(subject->unitId())->hasBonus(
			CSelector(newHorizonsReactiveDefense::isSolidarityProtection));
	}
};
}
TEST_F(ReactiveDefenseProjectionTest, WholePacketCannotProtectLaterVictimRetroactively)
{
	setup(true);
	auto source = projected->getForUpdate(enemy->unitId());
	auto victim = projected->getForUpdate(ally->unitId());
	auto demon = projected->getForUpdate(subject->unitId());
	PhysicalStrikeProjection packet(projected.get());
	BattleAttackInfo hit(source.get(), victim.get(), 0, false);
	int64_t lethal = victim->getAvailableHealth();
	packet.damage(*projected, hit, *victim, lethal, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(protectedDemon(*projected));
	const auto before = demon->getAvailableHealth();
	int64_t later = 10;
	packet.damage(*projected, hit, *demon, later, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(demon->getAvailableHealth(), before - 10);
	EXPECT_FALSE(protectedDemon(*projected));
	packet.finish(source->unitId());
	EXPECT_TRUE(protectedDemon(*projected));
	EXPECT_FALSE(subject->hasBonus(CSelector(newHorizonsReactiveDefense::isSolidarityProtection)));
}
TEST_F(ReactiveDefenseProjectionTest, AllDeadRecipientsExcludedAndReceiptDoesNotLeakToNextPacket)
{
	setup(true);
	auto source = projected->getForUpdate(enemy->unitId());
	auto victim = projected->getForUpdate(ally->unitId());
	auto demon = projected->getForUpdate(subject->unitId());
	PhysicalStrikeProjection packet(projected.get());
	BattleAttackInfo hit(source.get(), victim.get(), 0, false);
	int64_t lethal = victim->getAvailableHealth();
	packet.damage(*projected, hit, *victim, lethal, battle::DamageProvenance::PHYSICAL_CREATURE);
	int64_t demonLethal = demon->getAvailableHealth();
	packet.damage(*projected, hit, *demon, demonLethal, battle::DamageProvenance::PHYSICAL_CREATURE);
	packet.finish(source->unitId()); EXPECT_FALSE(protectedDemon(*projected));
	packet.finish(source->unitId()); EXPECT_FALSE(protectedDemon(*projected));
}
TEST_F(ReactiveDefenseProjectionTest, AcceptedZeroSpendsFirstHitAndOrdinaryRetaliationQualifies)
{
	setup(false);
	auto source = projected->getForUpdate(enemy->unitId());
	auto target = projected->getForUpdate(subject->unitId());
	BattleAttackInfo hit(source.get(), target.get(), 0, false); hit.retaliation = true;
	ASSERT_EQ(newHorizonsReactiveDefense::ironHidePercent(*projected, hit), 30);
	PhysicalStrikeProjection packet(projected.get()); int64_t zero = 0;
	packet.damage(*projected, hit, *target, zero, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*projected, hit), 0);
	const auto spent = target->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent));
	ASSERT_EQ(spent->size(), 1u); EXPECT_EQ(spent->front()->val, projected->battleGetRound());
	packet.finish(source->unitId());
	EXPECT_FALSE(subject->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
}
TEST_F(ReactiveDefenseProjectionTest, LethalHitRetainsSpentReceipt)
{
	setup(false);
	auto source = projected->getForUpdate(enemy->unitId());
	auto target = projected->getForUpdate(subject->unitId());
	BattleAttackInfo hit(source.get(), target.get(), 0, false);
	PhysicalStrikeProjection packet(projected.get()); int64_t lethal = target->getAvailableHealth();
	packet.damage(*projected, hit, *target, lethal, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(target->alive());
	EXPECT_TRUE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
}
TEST_F(ReactiveDefenseProjectionTest, RangedMagicCollateralCleaveAndPreemptiveDoNotSpend)
{
	setup(false);
	auto source = projected->getForUpdate(enemy->unitId());
	auto target = projected->getForUpdate(subject->unitId());
	for(int kind = 0; kind != 6; ++kind)
	{
		BattleAttackInfo hit(source.get(), target.get(), 0, kind == 0);
		if(kind == 1) hit.physicalDamage = false;
		if(kind == 2) hit.secondaryAttack = true;
		if(kind == 3) hit.cleaveDamagePercent = 50;
		if(kind == 4) hit.bracePreemptive = true;
		if(kind == 5) hit.preemptiveDamagePercent = 50;
		PhysicalStrikeProjection packet(projected.get()); int64_t zero = 0;
		packet.damage(*projected, hit, *target, zero, battle::DamageProvenance::PHYSICAL_CREATURE);
		packet.finish(source->unitId());
		EXPECT_FALSE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent))) << kind;
	}
}
TEST_F(ReactiveDefenseProjectionTest, HistoricalAndZeroDoNotCreateProjectionState)
{
	for(int mode : {1, 2})
	{
		ASSERT_NO_FATAL_FAILURE(setup(false, mode));
		EXPECT_FALSE(PhysicalStrikeProjection::needsProjection(*projected, enemy, subject));
		auto source = projected->getForUpdate(enemy->unitId());
		auto target = projected->getForUpdate(subject->unitId());
		PhysicalStrikeProjection packet(projected.get()); int64_t zero = 0;
		packet.damage(*projected, BattleAttackInfo(source.get(), target.get(), 0, false),
			*target, zero, battle::DamageProvenance::PHYSICAL_CREATURE);
		EXPECT_FALSE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
	}
}
TEST_F(ReactiveDefenseProjectionTest, HiddenHeroDoesNotSupplyConfiguredReactivePolicy)
{
	prepare(true); environment = std::make_unique<ReactiveEnvironment>(gameState());
	auto hidden = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	projected = std::make_shared<HypotheticBattle>(environment.get(), hidden);
	EXPECT_FALSE(PhysicalStrikeProjection::needsProjection(*projected, enemy, subject));
}
TEST_F(ReactiveDefenseProjectionTest, FullPreviewSelectedAndSnapshotReplayRetainSpentWithoutSourceOrSiblingMutation)
{
	setup(false);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), projected);
	auto selected = std::make_shared<HypotheticBattle>(environment.get(), projected);
	auto snapshot = std::make_shared<HypotheticBattle>(environment.get(), projected);
	const auto liveTarget = subject->save(), liveSource = enemy->save();
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto a = randomBytes(*nativeRng), b = randomBytes(*fixtureRng);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(enemy, subject, 0, false),
		enemy->getPosition(), cache, projected);
	ASSERT_NE(preview.effectPreview, nullptr); ASSERT_FALSE(preview.fortuneStrikes.empty());
	EXPECT_TRUE(preview.effectPreview->battleGetUnitByID(subject->unitId())->hasBonus(
		CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
	BattleExchangeVariant exchange; exchange.trackAttack(preview, selected, cache);
	applyProjectedBestAction(*snapshot, enemy, preview);
	for(const auto & branch : {selected, snapshot})
	{
		const auto * target = branch->battleGetUnitByID(subject->unitId());
		EXPECT_EQ(target->getAvailableHealth(), preview.effectPreview->battleGetUnitByID(subject->unitId())->getAvailableHealth());
		EXPECT_TRUE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
	}
	EXPECT_EQ(subject->save(), liveTarget); EXPECT_EQ(enemy->save(), liveSource);
	EXPECT_EQ(sibling->battleGetUnitByID(subject->unitId())->acquireState()->save(), liveTarget);
	EXPECT_EQ(randomBytes(*nativeRng), a); EXPECT_EQ(randomBytes(*fixtureRng), b);
}
TEST_F(ReactiveDefenseProjectionTest, SnapshotRestoresGuardOnUnharmedRecipientOutsideAffectedUnits)
{
	setup(true);
	auto destination = std::make_shared<HypotheticBattle>(environment.get(), projected);
	auto source = projected->getForUpdate(enemy->unitId());
	auto victim = projected->getForUpdate(ally->unitId());
	PhysicalStrikeProjection packet(projected.get()); int64_t lethal = victim->getAvailableHealth();
	packet.damage(*projected, BattleAttackInfo(source.get(), victim.get(), 0, false), *victim, lethal,
		battle::DamageProvenance::PHYSICAL_CREATURE);
	packet.finish(source->unitId()); ASSERT_TRUE(protectedDemon(*projected));
	PhysicalStrikeProjection::restoreEffects(*destination, *projected);
	EXPECT_TRUE(protectedDemon(*destination));
	projected->getForUpdate(subject->unitId())->removeUnitBonus(CSelector(newHorizonsReactiveDefense::isSolidarityProtection));
	PhysicalStrikeProjection::restoreEffects(*destination, *projected);
	EXPECT_FALSE(protectedDemon(*destination));
}

TEST_F(ReactiveDefenseProjectionTest, SelectedReplayRetainsNonBracePreemptiveExclusion)
{
	setup(false);
	auto source = projected->getForUpdate(enemy->unitId());
	auto target = projected->getForUpdate(subject->unitId());
	FortuneStrikeProjection receipt;
	receipt.preemptiveDamagePercent = 50;
	receipt.damageProvenance = battle::DamageProvenance::PHYSICAL_CREATURE;
	const auto attack = selectedAttackReceipt::attackForStrike(receipt, source.get(), target.get());
	EXPECT_EQ(attack.preemptiveDamagePercent, 50);
	PhysicalStrikeProjection packet(projected.get()); int64_t zero = 0;
	packet.damage(*projected, attack, *target, zero, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
}
TEST_F(ReactiveDefenseProjectionTest, KilledSummonAndCloneProduceReceiptsButGenericRemovalDoesNot)
{
	for(const bool clone : {false, true})
	{
		ASSERT_NO_FATAL_FAILURE(setup(true));
		auto source = projected->getForUpdate(enemy->unitId());
		battle::UnitInfo info; info.type = ally->creatureId(); info.count = 1;
		info.side = BattleSide::ATTACKER; info.position = BattleHex(6, 2); info.summoned = true;
		const auto id = static_cast<uint32_t>(battle()->nextUnitId());
		JsonNode data; info.save(data); projected->addUnit(id, data);
		auto victim = projected->getForUpdate(id);
		ASSERT_TRUE(victim->isSummoned());
		auto state = victim->save();
		state["state"]["cloned"].Bool() = clone;
		victim->load(state);
		PhysicalStrikeProjection packet(projected.get()); int64_t lethal = victim->getAvailableHealth();
		packet.damage(*projected, BattleAttackInfo(source.get(), victim.get(), 0, false), *victim, lethal,
			battle::DamageProvenance::PHYSICAL_CREATURE);
		packet.finish(source->unitId()); EXPECT_TRUE(protectedDemon(*projected));
	}
	ASSERT_NO_FATAL_FAILURE(setup(true)); projected->removeUnit(ally->unitId());
	EXPECT_FALSE(protectedDemon(*projected));
}
TEST_F(ReactiveDefenseProjectionTest, TimeStopPausedMarkerDoesNotSpendNewRound)
{
	setup(false);
	auto source = projected->getForUpdate(enemy->unitId());
	auto target = projected->getForUpdate(subject->unitId());
	BattleAttackInfo hit(source.get(), target.get(), 0, false);
	const auto marker = newHorizonsReactiveDefense::ironHideSpending(*projected, hit);
	ASSERT_TRUE(marker); target->addUnitBonus({*marker});
	// Model an expired Time Stop: round cleanup has already happened while
	// the stopped unit kept its old marker. Retain the exact old marker after
	// advancing; no timing assumptions about cleanup enter eligibility.
	projected->nextRound();
	target->removeUnitBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent));
	target->addUnitBonus({*marker});
	EXPECT_EQ(newHorizonsReactiveDefense::ironHidePercent(*projected, hit), 30);
	PhysicalStrikeProjection packet(projected.get()); int64_t zero = 0;
	packet.damage(*projected, hit, *target, zero, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto spent = target->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent));
	ASSERT_EQ(spent->size(), 1u); EXPECT_EQ(spent->front()->val, projected->battleGetRound());
}
TEST_F(ReactiveDefenseProjectionTest, FutureDirectStrikeSpendsAndExposesSameReductionAsSharedCallback)
{
	setup(false);
	auto source = projected->getForUpdate(enemy->unitId());
	auto target = projected->getForUpdate(subject->unitId());
	DamageCache cache;
	const auto before = projected->battleExpectedLuckDamage(BattleAttackInfo(source.get(), target.get(), 0, false));
	BattleExchangeVariant exchange;
	exchange.trackAttack(source, target, false, true, cache, projected, false, false);
	EXPECT_TRUE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
	EXPECT_GE(projected->battleExpectedLuckDamage(BattleAttackInfo(source.get(), target.get(), 0, false)), before);
}
