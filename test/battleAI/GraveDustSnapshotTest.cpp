/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <stdexcept>
#include <limits>
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
#include "../server/battles/BattleTestFixture.h"
#include "../../server/CGameHandler.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/ScopedStackEffectReplay.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../lib/battle/BattleForm.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/InitialExportEffectQueries.h"
#include "../../AI/BattleAI/OwnedStackEffectPreflight.h"
namespace
{
class GraveDustEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GraveDustEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class GraveDustSnapshotTest : public BattleTestFixture {};
TEST_F(GraveDustSnapshotTest, MaterializedChildrenKeepDustAfterParentAndLiveClearBeforeFirstExplicitQuery)
{
	startGame();
	startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr);
	beginCombat();
	int64_t damage = 15;
	live->damage(damage);
	ASSERT_EQ(damage, 15);
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(live->unitId(), dust);
	const auto originalHealth = live->getAvailableHealth();
	auto environment = std::make_shared<GraveDustEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	EXPECT_FALSE(parent->initialExportSubject()); // No ALL_KNOWING promotion.
	auto parentUnit = parent->getForUpdate(live->unitId());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto childUnit = child->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto siblingUnit = sibling->getForUpdate(live->unitId());
	ASSERT_EQ(child->battleGetUnitByID(live->unitId()), childUnit.get());
	battle()->removeUnitBonus(live->unitId(), dust);
	parent->removeUnitBonus(live->unitId(), dust);
	// No explicit child coefficient/group query preceded removal of both ancestors' copies.
	EXPECT_EQ(battle::healingReceivedReduction(*childUnit), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingUnit), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*parentUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	int64_t requested = 1000;
	EXPECT_EQ(childUnit->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 8);
	EXPECT_EQ(childUnit->getAvailableHealth(), originalHealth + 8);
	EXPECT_EQ(siblingUnit->getAvailableHealth(), originalHealth);
	EXPECT_EQ(parentUnit->getAvailableHealth(), originalHealth);
	EXPECT_EQ(live->getAvailableHealth(), originalHealth);
	child->removeUnitBonus(live->unitId(), dust);
	EXPECT_EQ(battle::healingReceivedReduction(*childUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingUnit), 5000);
}

TEST_F(GraveDustSnapshotTest, OrdinaryProjectionPreflightsPairBeforeAnyOverlayAndMatchesNativeTimeStop)
{
	startGame();
	startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr);
	beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(live->unitId(), dust);
	auto environment = std::make_shared<GraveDustEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback);
	ASSERT_FALSE(branch->initialExportSubject());
	auto unit = branch->getForUpdate(live->unitId());
	const auto before = unit->save();
	EXPECT_THROW(unit->updateUnitBonus(newHorizonsGraveDust::records(7500, PlayerColor(0))), std::invalid_argument);
	EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{dust.front()}), std::invalid_argument);
	EXPECT_EQ(unit->save(), before);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	unit->removeUnitBonus(dust);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	unit->addUnitBonus(dust);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus(std::vector<Bonus>{stopped});
	ASSERT_TRUE(unit->isTimeStopped());
	unit->removeUnitBonus(dust);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_THROW(unit->updateUnitBonus(dust), std::invalid_argument);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 5000);
	EXPECT_FALSE(live->isTimeStopped());
}

template<typename Random> auto dustRandomBytes(Random & random)
{
	CMemorySerializer wire; wire.oser & random; return wire.extractBuffer();
}
TEST_F(GraveDustSnapshotTest, InvalidLaterPacketGroupLeavesEarlierTargetParentSiblingAndBothRandomStreamsExact)
{
	startGame(); startBattle();
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	auto * last = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 2);
	ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto firstUnit = parent->getForUpdate(first->unitId()), lastUnit = parent->getForUpdate(last->unitId());
	auto siblingFirst = sibling->getForUpdate(first->unitId());
	const auto firstState = firstUnit->save(), lastState = lastUnit->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = dustRandomBytes(*nativeRandom), fixtureBefore = dustRandomBytes(*fixtureRandom);
	SetStackEffect packet; packet.battleID = BattleID(0);
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	packet.toAdd.emplace_back(first->unitId(), dust);
	packet.toAdd.emplace_back(last->unitId(), std::vector<Bonus>{dust.front()});
	EXPECT_THROW(parent->getServerCallback()->apply(packet), std::invalid_argument);
	EXPECT_EQ(firstUnit->save(), firstState); EXPECT_EQ(lastUnit->save(), lastState);
	EXPECT_EQ(battle::healingReceivedReduction(*firstUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*lastUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingFirst), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*first), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*last), 0);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore);
	EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
}
TEST_F(GraveDustSnapshotTest, PreparedNativeRebirthCleansDustThroughExistingVisitorAndRestoresUnreducedHealing)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0)); battle()->addUnitBonus(live->unitId(), dust);
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto unit = parent->getForUpdate(live->unitId()); auto other = sibling->getForUpdate(live->unitId());
	unit->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(unit->canCast()); const auto maximum = unit->getAvailableHealth();
	BattleStackAttacked hit; hit.stackAttacked = unit->unitId(); hit.damageAmount = maximum;
	CStack::prepareAttacked(hit, *parent->getServerCallback()->getRNG(), unit->acquireState());
	ASSERT_TRUE(hit.killed()); ASSERT_TRUE(hit.willRebirth());
	StacksInjured injury; injury.battleID = BattleID(0); injury.stacks.push_back(hit);
	parent->getServerCallback()->apply(injury);
	EXPECT_TRUE(unit->alive()); EXPECT_EQ(unit->getAvailableHealth(), maximum);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*other), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 5000);
	int64_t damage = 15; unit->damage(damage);
	int64_t requested = 1000;
	EXPECT_EQ(unit->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 15);
	EXPECT_EQ(unit->getAvailableHealth(), maximum);
}
TEST_F(GraveDustSnapshotTest, TimeStopAlsoProtectsSelectorRemovalInTheOrdinaryOverlay)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = branch->getForUpdate(live->unitId());
	unit->addUnitBonus(newHorizonsGraveDust::records(5000, PlayerColor(0)));
	unit->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	unit->removeUnitBonus(CSelector(newHorizonsGraveDust::isEffect));
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	unit->removeUnitBonus(Selector::type()(BonusType::TIME_STOP));
	unit->removeUnitBonus(CSelector(newHorizonsGraveDust::isEffect));
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
}
TEST_F(GraveDustSnapshotTest, PartialSelectorRemovalRejectsBeforeEitherCompanionChanges)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = branch->getForUpdate(live->unitId());
	unit->addUnitBonus(newHorizonsGraveDust::records(5000, PlayerColor(0)));
	const auto before = unit->save();
	EXPECT_THROW(unit->removeUnitBonus(Selector::type()(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS)), std::invalid_argument);
	EXPECT_EQ(unit->save(), before); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_EQ(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->size(), 2u);
}

TEST_F(GraveDustSnapshotTest, PreparedIndirectInjuryExpiresIndirectStatusAndExhaustedGuardianWithoutChangingSibling)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::SPELL_EFFECT, 5, BonusSourceID(SpellID(SpellID::SHIELD)));
	guardian.turnsRemain = 2;
	unit->addUnitBonus({guardian, Bonus(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE,
		BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 9, BonusSourceID(SpellID(SpellID::SHIELD)),
		BonusSubtypeID(PrimarySkill::DEFENSE))});
	ASSERT_EQ(unit->guardianSpiritHitPoints, 5);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto before = unit->getAvailableHealth();
	BattleStackAttacked hit; hit.stackAttacked = unit->unitId(); hit.damageAmount = 10;
	CStack::prepareAttacked(hit, *parent->getServerCallback()->getRNG(), unit->acquireState(),
		false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_NE(hit.flags & BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED, 0);
	StacksInjured injury; injury.battleID = BattleID(0); injury.stacks.push_back(hit);
	parent->getServerCallback()->apply(injury);
	EXPECT_EQ(unit->getAvailableHealth(), before - 5);
	EXPECT_EQ(unit->guardianSpiritHitPoints, 0); EXPECT_EQ(unit->guardianSpiritRoundsRemaining, 0);
	EXPECT_TRUE(unit->getUnstackedBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT))->empty());
	EXPECT_TRUE(unit->getUnstackedBonuses(Bonus::UntilTakingIndirectDamage)->empty());
	EXPECT_EQ(other->getAvailableHealth(), before); EXPECT_EQ(other->guardianSpiritHitPoints, 5);
	EXPECT_FALSE(other->getUnstackedBonuses(Bonus::UntilTakingIndirectDamage)->empty());
	EXPECT_EQ(live->getAvailableHealth(), before); EXPECT_EQ(live->guardianSpiritHitPoints, 0);
}

TEST_F(GraveDustSnapshotTest, CompleteVectorInEitherOrderPreservesControlCleanupAndRejectedPrefix)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::OTHER, 5, BonusSourceID());
	guardian.turnsRemain = 2;
	for(const bool reversed : {false, true})
	{
		auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto unit = branch->getForUpdate(live->unitId());
		unit->addUnitBonus(dust); unit->addUnitBonus({guardian});
		ASSERT_EQ(unit->guardianSpiritHitPoints, 5);
		const auto before = unit->save();
		EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{guardian, dust.front()}), std::invalid_argument);
		EXPECT_EQ(unit->save(), before); EXPECT_EQ(unit->guardianSpiritHitPoints, 5);
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
		std::vector<Bonus> removal{dust.at(reversed ? 1 : 0), dust.at(reversed ? 0 : 1), guardian};
		EXPECT_NO_THROW(unit->removeUnitBonus(removal));
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
		EXPECT_TRUE(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->empty());
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::GUARDIAN_SPIRIT));
		EXPECT_EQ(unit->guardianSpiritHitPoints, 0); EXPECT_EQ(unit->guardianSpiritRoundsRemaining, 0);
		EXPECT_EQ(live->guardianSpiritHitPoints, 0); EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	}
}

TEST_F(GraveDustSnapshotTest, AdmittedSameStrengthRefreshKeepsActivationDurationStampAndRejectsStrengthOrCasterChanges)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = branch->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0)); unit->addUnitBonus(dust);
	const auto markers = unit->getUnstackedBonuses(Selector::type()(BonusType::PHYSICAL_AFFLICTION));
	ASSERT_EQ(markers->size(), 1u); const auto initial = physicalAfflictions::markerMetadata(*markers->front());
	ASSERT_TRUE(initial); ASSERT_GT(initial->applicationOrder, 0);
	for(int attempt = 0; attempt < 2; ++attempt)
	{
		EXPECT_NO_THROW(unit->updateUnitBonus(dust));
		const auto current = unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
		ASSERT_EQ(current->size(), 2u); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
		for(const auto & effect : *current)
		{
			EXPECT_EQ(effect->duration, BonusDuration::STACK_ACTIVATION); EXPECT_EQ(effect->turnsRemain, 0);
			EXPECT_EQ(effect->spellCasterOwner, PlayerColor(0));
			if(const auto metadata = physicalAfflictions::markerMetadata(*effect))
				EXPECT_EQ(metadata->applicationOrder, initial->applicationOrder);
		}
	}
	const auto before = unit->save();
	const auto snapshot = [&] {
		std::vector<JsonNode> result;
		const auto records = unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
		for(const auto & effect : *records) result.push_back(effect->toJsonNode());
		return result;
	};
	const auto effects = snapshot();
	EXPECT_THROW(unit->updateUnitBonus(newHorizonsGraveDust::records(7500, PlayerColor(0))), std::invalid_argument);
	const auto other = battle()->getSidePlayer(BattleSide::DEFENDER); ASSERT_NE(other, PlayerColor(0));
	EXPECT_THROW(unit->updateUnitBonus(newHorizonsGraveDust::records(5000, other)), std::invalid_argument);
	EXPECT_EQ(unit->save(), before); EXPECT_EQ(snapshot(), effects);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
}

TEST_F(GraveDustSnapshotTest, MixedTimeStopRemovalPreflightsOrderedVectorBeforeOverlayMutation)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus(dust); unit->addUnitBonus({stopped});
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto before = unit->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = dustRandomBytes(*nativeRandom), fixtureBefore = dustRandomBytes(*fixtureRandom);
	for(const auto & partial : dust)
	{
		EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{stopped, partial}), std::invalid_argument);
		EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(unit->save(), before);
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	}
	SetStackEffect packet; packet.battleID = BattleID(0);
	packet.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{stopped});
	packet.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{dust.front()});
	EXPECT_THROW(parent->getServerCallback()->apply(packet), std::invalid_argument);
	EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(unit->save(), before);
	unit->removeUnitBonus(std::vector<Bonus>{dust.front(), stopped});
	EXPECT_FALSE(unit->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_TRUE(other->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*other), 5000);
	EXPECT_FALSE(live->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore);
	EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
}

TEST_F(GraveDustSnapshotTest, OrdinaryIncomingClockBeforeCompleteDustIsCanonicalProtectedNoOp)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	ASSERT_FALSE(parent->initialExportSubject()); // A P0 source is never silently promoted.
	auto unit = parent->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	const auto nativeBefore = live->save(), siblingBefore = other->save();
	SetStackEffect incoming; incoming.battleID = BattleID(0);
	incoming.toAdd.emplace_back(live->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
	EXPECT_NO_THROW(parent->getServerCallback()->apply(incoming));
	EXPECT_TRUE(unit->isTimeStopped());
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	EXPECT_TRUE(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->empty());
	EXPECT_EQ(live->save(), nativeBefore); EXPECT_EQ(other->save(), siblingBefore);
	EXPECT_FALSE(live->isTimeStopped()); EXPECT_FALSE(other->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, ExplicitOwnedOrderedPreflightKeepsParentSiblingNativeAndRandomStreamsExact)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	battle()->addUnitBonus(live->unitId(), dust); battle()->addUnitBonus(live->unitId(), {stopped});
	GraveDustEnvironment environment(gameState());
	// This separate explicit full-information fixture is not a P0 capture or promotion.
	auto authority = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto exported = InitialExportBattleSubject::capture(authority, *battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, exported);
	auto unit = parent->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto nativeState = live->save(), parentState = unit->save(), siblingState = other->save();
	const auto effects = [](const battle::Unit & value)
	{
		std::vector<JsonNode> result;
		const auto records = value.getUnstackedBonuses(Selector::all);
		for(const auto & record : *records) result.push_back(record->toJsonNode());
		return result;
	};
	const auto nativeEffects = effects(*live), parentEffects = effects(*unit), siblingEffects = effects(*other);
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = dustRandomBytes(*nativeRandom), fixtureBefore = dustRandomBytes(*fixtureRandom);
	const auto globalBefore = dustRandomBytes(CRandomGenerator::getDefault());
	SetStackEffect complete; complete.battleID = BattleID(0);
	complete.toRemove.emplace_back(live->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
	EXPECT_NO_THROW(graveDustAI::validateOwnedEffectChanges(*parent, complete));
	auto partial = complete; partial.toRemove.front().second.pop_back();
	EXPECT_THROW(graveDustAI::validateOwnedEffectChanges(*parent, partial), std::invalid_argument);
	auto foreign = complete; foreign.battleID = BattleID(777);
	EXPECT_THROW(graveDustAI::validateOwnedEffectChanges(*parent, foreign), std::invalid_argument);
	const auto parentSource = parent->initialExportSubject(); ASSERT_NE(parentSource, nullptr);
	EXPECT_THROW(parentSource->effectQueries(*sibling), std::invalid_argument);
	EXPECT_EQ(live->save(), nativeState); EXPECT_EQ(unit->save(), parentState); EXPECT_EQ(other->save(), siblingState);
	EXPECT_EQ(effects(*live), nativeEffects); EXPECT_EQ(effects(*unit), parentEffects); EXPECT_EQ(effects(*other), siblingEffects);
	EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore); EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
	EXPECT_EQ(dustRandomBytes(CRandomGenerator::getDefault()), globalBefore);
}

TEST_F(GraveDustSnapshotTest, OrdinaryIncomingClockProtectsLaterAddAndUpdateRowsAndTheirScalarEffects)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	const auto nativeBefore = live->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeRngBefore = dustRandomBytes(*nativeRandom), fixtureRngBefore = dustRandomBytes(*fixtureRandom);
	const auto globalRngBefore = dustRandomBytes(CRandomGenerator::getDefault());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	const Bonus speed(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID());
	const Bonus luck(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 3, BonusSourceID());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::OTHER, 40, BonusSourceID());
	guardian.turnsRemain = 3;
	for(const bool updating : {false, true})
	{
		auto parent = std::make_shared<HypotheticBattle>(&environment,
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		const auto unit = parent->getForUpdate(live->unitId());
		const auto speedBefore = unit->valOfBonuses(BonusType::STACKS_SPEED);
		const auto luckBefore = unit->valOfBonuses(BonusType::LUCK);
		const auto frozenBefore = unit->frozenLastAppliedRound();
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		const auto other = sibling->getForUpdate(live->unitId()); const auto siblingBefore = other->save();
		const auto frozen = newHorizonsFrozen::marker(BonusSourceID(), parent->getRound());
		const std::vector<Bonus> incoming{speed, clock, dust.front(), dust.back(), guardian, luck, frozen};
		if(updating) EXPECT_NO_THROW(unit->updateUnitBonus(incoming));
		else EXPECT_NO_THROW(unit->addUnitBonus(incoming));
		EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
		EXPECT_EQ(unit->valOfBonuses(BonusType::STACKS_SPEED), speedBefore + 7);
		EXPECT_EQ(unit->valOfBonuses(BonusType::LUCK), luckBefore);
		EXPECT_EQ(unit->guardianSpiritHitPoints, 0); EXPECT_EQ(unit->guardianSpiritRoundsRemaining, 0);
		EXPECT_FALSE(newHorizonsFrozen::isFrozen(*unit)); EXPECT_EQ(unit->frozenLastAppliedRound(), frozenBefore);
		const auto swift = newHorizonsSwiftRebirth::marker(parent->getRound());
		EXPECT_NO_THROW(unit->addUnitBonus({swift}));
		EXPECT_NO_THROW(unit->updateUnitBonus({swift}));
		EXPECT_TRUE(newHorizonsSwiftRebirth::lifecycle(*unit));
		EXPECT_TRUE(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->empty());
		EXPECT_EQ(other->save(), siblingBefore); EXPECT_FALSE(other->isTimeStopped());
		EXPECT_EQ(live->save(), nativeBefore); EXPECT_FALSE(live->isTimeStopped());
	}
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeRngBefore);
	EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureRngBefore);
	EXPECT_EQ(dustRandomBytes(CRandomGenerator::getDefault()), globalRngBefore);
}

TEST_F(GraveDustSnapshotTest, AcceptedFrozenReceiptMatchesNativeAndExplicitOwnedReplayForBothVectorOrders)
{
	startGame(); startBattle();
	std::vector<CStack *> units;
	for(int index = 0; index < 4; ++index)
		units.push_back(addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex + index), 2));
	for(const auto * unit : units) ASSERT_NE(unit, nullptr);
	beginCombat();
	GraveDustEnvironment environment(gameState());
	auto authority = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	size_t index = 0;
	for(const bool updating : {false, true}) for(const bool clockFirst : {false, true})
	{
		auto * live = units.at(index++);
		const auto before = live->frozenLastAppliedRound(); const auto round = battle()->getRound();
		const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
		const auto frozen = newHorizonsFrozen::marker(BonusSourceID(), round);
		const std::vector<Bonus> records = clockFirst ? std::vector<Bonus>{clock, frozen} : std::vector<Bonus>{frozen, clock};
		// Explicit full-information fixture only; this is NOT an ordinary P0
		// source capture or a replacement for either original unchanged oracle.
		auto exported = InitialExportBattleSubject::capture(authority, *battle());
		auto parent = std::make_shared<HypotheticBattle>(&environment, exported);
		auto owned = exported->effectQueries(*parent);
		battle::replayStackEffectVector(*owned, *parent, live->unitId(), records,
			updating ? newHorizonsGraveDust::EffectOperation::UPDATE : newHorizonsGraveDust::EffectOperation::ADD);
		const auto * prepared = dynamic_cast<const battle::CUnitState *>(&owned->current(live->unitId()));
		ASSERT_NE(prepared, nullptr);
		EXPECT_EQ(prepared->frozenLastAppliedRound(), clockFirst ? before : round);
		EXPECT_EQ(newHorizonsFrozen::isFrozen(*prepared), !clockFirst); EXPECT_TRUE(prepared->isTimeStopped());
		EXPECT_EQ(live->frozenLastAppliedRound(), before); EXPECT_FALSE(live->isTimeStopped());
		const auto expectedRound = prepared->frozenLastAppliedRound();
		const auto expectedFrozen = newHorizonsFrozen::isFrozen(*prepared);
		owned.reset(); parent.reset(); exported.reset(); // End the immutable native source interval FIRST.
		if(updating) battle()->updateUnitBonus(live->unitId(), records);
		else battle()->addUnitBonus(live->unitId(), records);
		EXPECT_EQ(live->frozenLastAppliedRound(), expectedRound);
		EXPECT_EQ(newHorizonsFrozen::isFrozen(*live), expectedFrozen);
		EXPECT_TRUE(live->isTimeStopped());
	}
}

}

TEST_F(GraveDustSnapshotTest, OrderedOrdinaryRejectionKeepsUnrelatedPrefixAndHeldRawRecordsExact)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	const Bonus speed(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
	unit->addUnitBonus(dust); unit->addUnitBonus({clock, speed});
	const auto before = unit->save(), nativeBefore = live->save();
	const auto held = unit->getUnstackedBonuses(Selector::all);
	std::vector<const Bonus *> identities;
	std::vector<JsonNode> values;
	for(const auto & record : *held) { identities.push_back(record.get()); values.push_back(record->toJsonNode()); }
	const auto nativeRandom = dustRandomBytes(dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator()));
	const auto fixtureRandom = dustRandomBytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{clock, speed, dust.front()}), std::invalid_argument);
	EXPECT_EQ(unit->save(), before); EXPECT_EQ(live->save(), nativeBefore);
	const auto current = unit->getUnstackedBonuses(Selector::all);
	ASSERT_EQ(current->size(), identities.size());
	for(size_t index = 0; index < current->size(); ++index)
	{
		EXPECT_EQ((*current)[index].get(), identities[index]);
		EXPECT_EQ((*current)[index]->toJsonNode(), values[index]);
		EXPECT_EQ((*held)[index]->toJsonNode(), values[index]);
	}
	EXPECT_EQ(dustRandomBytes(dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator())), nativeRandom);
	EXPECT_EQ(dustRandomBytes(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRandom);
}

TEST_F(GraveDustSnapshotTest, OrdinaryPacketFormRemovalRetainsGlobalInvalidationAndNativeIsolation)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	unit->addUnitBonus(dust);
	unit->beginBattleForm(creatureByName("core:pikeman"), 2);
	const auto marker = battle::polymorphMarker(SpellID(SpellID::decode("new-horizons:polymorph")), PlayerColor(0));
	parent->addUnitBonus(unit->unitId(), {marker});
	ASSERT_TRUE(unit->hasBattleForm());
	const auto nativeBefore = live->save();
	const auto version = parent->getTreeVersion();
	SetStackEffect changes; changes.battleID = parent->getBattleID();
	changes.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{marker, dust.front(), dust.back()});
	ASSERT_NO_THROW(parent->getServerCallback()->apply(changes));
	EXPECT_FALSE(unit->hasBattleForm());
	EXPECT_FALSE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	EXPECT_EQ(parent->getTreeVersion(), version + 1); // Exactly the real IBattleState row invalidation.
	EXPECT_EQ(live->save(), nativeBefore);
}

TEST_F(GraveDustSnapshotTest, ColdOrdinarySiblingFreezesClockAbsenceAndKeepsPermanentStatPolicy)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	for(const auto duration : {BonusDuration::ONE_BATTLE, BonusDuration::PERMANENT, BonusDuration::N_TURNS})
	{
		auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto unit = parent->getForUpdate(live->unitId());
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		auto other = sibling->getForUpdate(live->unitId());
		const auto base = other->valOfBonuses(BonusType::STACKS_SPEED);
		Bonus clock(duration, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()); clock.turnsRemain = 2;
		const Bonus speed(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
		unit->addUnitBonus({speed, clock});
		EXPECT_TRUE(unit->isTimeStopped()); EXPECT_FALSE(other->isTimeStopped()); EXPECT_FALSE(live->isTimeStopped());
		EXPECT_EQ(other->valOfBonuses(BonusType::STACKS_SPEED), base + 3); // Historical unrelated permanent inheritance remains.
		other->addUnitBonus({clock}); unit->removeUnitBonus(std::vector<Bonus>{clock});
		EXPECT_FALSE(unit->isTimeStopped()); EXPECT_TRUE(other->isTimeStopped()); EXPECT_FALSE(live->isTimeStopped());
	}
}

TEST_F(GraveDustSnapshotTest, OrdinarySiblingRetainsExistingClockWhenParentRemovesItsOwnCopy)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus({clock});
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	unit->removeUnitBonus(std::vector<Bonus>{clock});
	EXPECT_FALSE(unit->isTimeStopped()); EXPECT_TRUE(other->isTimeStopped()); EXPECT_FALSE(live->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, ColdSiblingPreservesInactiveNativeClockRecipeWhenNativeDependencyChanges)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	clock.limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::GUARDIAN_SPIRIT);
	live->addNewBonus(std::make_shared<Bonus>(clock));
	ASSERT_FALSE(live->isTimeStopped());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
	unit->removeUnitBonus(std::vector<Bonus>{unrelated}); // Engage the old selective snapshot while the native clock is inactive.
	ASSERT_TRUE(unit->recipientOverlayView().stackedSnapshot);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	ASSERT_FALSE(unit->isTimeStopped()); ASSERT_FALSE(other->isTimeStopped());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::OTHER, 5, BonusSourceID());
	guardian.turnsRemain = 2;
	// This is the existing native test owner, not an imported hypothetical export.
	battle()->addUnitBonus(live->unitId(), {guardian});
	ASSERT_TRUE(live->isTimeStopped());
	EXPECT_TRUE(unit->isTimeStopped()); EXPECT_TRUE(other->isTimeStopped());
	unit->removeUnitBonus(std::vector<Bonus>{clock});
	EXPECT_FALSE(unit->isTimeStopped());
	EXPECT_TRUE(other->isTimeStopped()); EXPECT_TRUE(live->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, FrozenParentClockDeltaRetainsRepeatedAliasesAndLimiterIdentity)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	auto first = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	first->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::GUARDIAN_SPIRIT);
	auto second = std::make_shared<Bonus>(*first);
	unit->bonusesToAdd = {first, first, second}; // Existing ordinary local ADD policy does not limit these records.
	const Bonus revision(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 9, BonusSourceID());
	unit->removeUnitBonus(std::vector<Bonus>{revision}); // Existing mutation advances the warmed query revision.
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	unit->removeUnitBonus(std::vector<Bonus>{*first});
	ASSERT_FALSE(unit->isTimeStopped());
	const auto clocks = other->getUnstackedBonuses(Selector::type()(BonusType::TIME_STOP));
	ASSERT_EQ(clocks->size(), 3u);
	EXPECT_EQ((*clocks)[0].get(), (*clocks)[1].get());
	EXPECT_NE((*clocks)[0].get(), (*clocks)[2].get());
	EXPECT_NE((*clocks)[0].get(), first.get());
	for(const auto & clock : *clocks) EXPECT_EQ(clock->limiter.get(), first->limiter.get());
	EXPECT_TRUE(other->isTimeStopped()); EXPECT_FALSE(live->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, OrdinaryPacketClockRemovalEnablesLaterCompleteDustVector)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus({clock});
	const auto before = unit->save(), nativeBefore = live->save();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	// A stopped vector entry remains rejected; prior clock removal is a separate row.
	EXPECT_THROW(unit->addUnitBonus(dust), std::invalid_argument);
	EXPECT_EQ(unit->save(), before);
	const auto version = parent->getTreeVersion();
	SetStackEffect changes; changes.battleID = parent->getBattleID();
	changes.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{clock});
	changes.toAdd.emplace_back(unit->unitId(), dust);
	ASSERT_NO_THROW(parent->getServerCallback()->apply(changes));
	EXPECT_FALSE(unit->isTimeStopped());
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_EQ(parent->getTreeVersion(), version + 2);
	EXPECT_EQ(live->save(), nativeBefore);
}

TEST_F(GraveDustSnapshotTest, FrozenClockRemovalUsesStableRawHandlesBeforeAndAfterParentMutation)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	for(const bool removeBeforeParent : {false, true})
	{
		auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto unit = parent->getForUpdate(live->unitId());
		Bonus clock(BonusDuration::N_TURNS, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
		clock.turnsRemain = 2; unit->addUnitBonus({clock});
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		auto other = sibling->getForUpdate(live->unitId());
		const auto held = other->getUnstackedBonuses(Selector::type()(BonusType::TIME_STOP));
		ASSERT_EQ(held->size(), 1u);
		EXPECT_NE(held->front().get(), unit->getUnstackedBonuses(Selector::type()(BonusType::TIME_STOP))->front().get());
		other->advanceTimedRound(); // Canonical Time Stop pauses its own N_TURNS clock.
		EXPECT_EQ(other->getUnstackedBonuses(Selector::type()(BonusType::TIME_STOP))->front()->turnsRemain, 2);
		if(removeBeforeParent) other->removeUnitBonus(std::vector<Bonus>{clock});
		unit->removeUnitBonus(std::vector<Bonus>{clock});
		if(!removeBeforeParent)
		{
			ASSERT_TRUE(other->isTimeStopped());
			EXPECT_EQ(other->getUnstackedBonuses(Selector::type()(BonusType::TIME_STOP))->front().get(), held->front().get());
			other->removeUnitBonus(std::vector<Bonus>{clock});
		}
		EXPECT_FALSE(other->isTimeStopped());
		other->advanceTimedRound(); EXPECT_FALSE(other->isTimeStopped());
		EXPECT_EQ(held->front()->turnsRemain, 2); EXPECT_FALSE(live->isTimeStopped());
	}
}

TEST_F(GraveDustSnapshotTest, UnrelatedParentRevisionPreservesNativeClockRawOrderAndIdentity)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto clock = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	auto ordinary = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID());
	live->addNewBonus(clock); live->addNewBonus(ordinary);
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const CSelector selected([&](const Bonus * record) { return record == clock.get() || record == ordinary.get(); });
	const auto before = other->getUnstackedBonuses(selected);
	ASSERT_EQ(before->size(), 2u); ASSERT_EQ((*before)[0].get(), clock.get()); ASSERT_EQ((*before)[1].get(), ordinary.get());
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::OTHER, 2,
		BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	unit->removeUnitBonus(std::vector<Bonus>{unrelated});
	const auto after = other->getUnstackedBonuses(selected);
	ASSERT_EQ(after->size(), before->size());
	for(size_t index = 0; index < before->size(); ++index) EXPECT_EQ((*after)[index].get(), (*before)[index].get());
}

TEST_F(GraveDustSnapshotTest, FrozenClockExperimentalImportRefusesBeforeAdmissionWhileOrdinaryRemovalWorks)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	// Existing explicit all-knowing/native-root test API only; never a player-source promotion.
	auto source = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	auto parent = std::make_shared<HypotheticBattle>(&environment, source);
	auto unit = parent->getForUpdate(live->unitId());
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus({clock});
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	unit->removeUnitBonus(std::vector<Bonus>{clock}); ASSERT_TRUE(other->isTimeStopped());
	std::vector<recipientBonusOverlay::Layer> layers; const CStack * native = nullptr;
	EXPECT_FALSE(other->captureRecipientOverlayLayers(layers, native));
	EXPECT_THROW(InitialExportBattleSubject::captureLegacyMovement(sibling, *battle()), std::invalid_argument);
	other->removeUnitBonus(std::vector<Bonus>{clock}); EXPECT_FALSE(other->isTimeStopped());
	EXPECT_THROW(InitialExportBattleSubject::captureLegacyMovement(sibling, *battle()), std::invalid_argument);
	EXPECT_FALSE(other->isTimeStopped()); EXPECT_FALSE(live->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, DormantMalformedLegacyMarkerKeepsConstructorAndCanonicalQueryErrorTiming)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus({clock});
	const SpellID invalid(std::numeric_limits<int32_t>::max());
	const Bonus dormant(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(invalid));
	// The ordinary writer already rejects this record while stopped. Raw setup
	// below characterizes prior query-only malformed-data behavior, not admission.
	EXPECT_THROW(unit->addUnitBonus({dormant}), std::runtime_error);
	unit->bonusesToAdd.push_back(std::make_shared<Bonus>(dormant));
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
	unit->removeUnitBonus(std::vector<Bonus>{unrelated}); // Revision/cache boundary, with dedicated clock short-circuit.
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	std::shared_ptr<StackWithBonuses> other;
	ASSERT_NO_THROW(other = sibling->getForUpdate(live->unitId()));
	EXPECT_TRUE(other->isTimeStopped());
	const auto selected = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(invalid));
	EXPECT_EQ(other->getUnstackedBonuses(selected)->size(), 1u);
	unit->removeUnitBonus(std::vector<Bonus>{unrelated});
	EXPECT_TRUE(other->isTimeStopped()); EXPECT_EQ(other->getUnstackedBonuses(selected)->size(), 1u);
	other->removeUnitBonus(std::vector<Bonus>{clock});
	EXPECT_THROW(other->isTimeStopped(), std::runtime_error); // Existing canonical legacy lookup, still not guessed false.
}

TEST_F(GraveDustSnapshotTest, FrozenLegacyClockRetainsItsRawPositionAcrossUnrelatedParentRecapture)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const SpellID timeStop(SpellID::decode("new-horizons:timeStop")); ASSERT_TRUE(timeStop.hasValue());
	const Bonus ordinary(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID());
	const Bonus legacy(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(timeStop));
	unit->addUnitBonus({ordinary, legacy}); ASSERT_TRUE(unit->isTimeStopped());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const CSelector selected([&](const Bonus * record)
	{
		return (record->source == BonusSource::OTHER && record->type == BonusType::STACKS_SPEED && record->val == 7)
			|| (record->source == BonusSource::SPELL_EFFECT && record->sid == legacy.sid);
	});
	const auto before = other->getUnstackedBonuses(selected); ASSERT_EQ(before->size(), 2u);
	ASSERT_EQ(before->front()->type, BonusType::STACKS_SPEED); ASSERT_EQ(before->back()->type, BonusType::NONE);
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::OTHER, 2,
		BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	unit->removeUnitBonus(std::vector<Bonus>{unrelated});
	const auto after = other->getUnstackedBonuses(selected); ASSERT_EQ(after->size(), before->size());
	for(size_t index = 0; index < before->size(); ++index) EXPECT_EQ((*after)[index].get(), (*before)[index].get());
	other->removeUnitBonus(std::vector<Bonus>{legacy}); EXPECT_FALSE(other->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, StableFrozenClockPointerSelectorRemovesBeforeAnyParentRevision)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus({clock});
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto held = other->getUnstackedBonuses(Selector::type()(BonusType::TIME_STOP)); ASSERT_EQ(held->size(), 1u);
	const CSelector selected([pointer = held->front().get()](const Bonus * record) { return record == pointer; });
	const auto selectedBefore = other->getUnstackedBonuses(selected); ASSERT_EQ(selectedBefore->size(), 1u);
	EXPECT_EQ(selectedBefore->front().get(), held->front().get());
	other->removeUnitBonus(selected); EXPECT_FALSE(other->isTimeStopped());
	unit->removeUnitBonus(std::vector<Bonus>{clock});
	EXPECT_FALSE(other->isTimeStopped()); EXPECT_TRUE(other->getUnstackedBonuses(selected)->empty());
}

TEST_F(GraveDustSnapshotTest, WidenedStackedQueryReselectsWithoutAnyRawAliasMatch)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto stable = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::OTHER,
		7, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	live->addNewBonus(stable);
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const SpellID timeStop(SpellID::decode("new-horizons:timeStop")); ASSERT_TRUE(timeStop.hasValue());
	const Bonus legacy(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(timeStop));
	unit->addUnitBonus({legacy});
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
	for(const bool genericSnapshot : {false, true})
	{
		if(genericSnapshot) unit->removeUnitBonus(std::vector<Bonus>{unrelated});
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		auto other = sibling->getForUpdate(live->unitId());
		const auto all = other->getAllBonuses(Selector::all);
		const CSelector selected([pointer = stable.get()](const Bonus * record) { return record == pointer; });
		for(const auto & selector : {selected, Selector::type()(BonusType::CONFUSION_PENDING)})
		{
			std::vector<const Bonus *> expected;
			for(const auto & record : *all) if(selector(record.get())) expected.push_back(record.get());
			const auto filtered = other->getAllBonuses(selector);
			ASSERT_EQ(filtered->size(), expected.size());
			for(size_t index = 0; index < expected.size(); ++index) EXPECT_EQ((*filtered)[index].get(), expected[index]);
		}
		EXPECT_FALSE(other->hasBonusOfType(BonusType::CONFUSION_PENDING));
		if(genericSnapshot)
		{
			ASSERT_TRUE(unit->recipientOverlayView().stackedSnapshot);
			const auto clocks = Selector::source(BonusSource::SPELL_EFFECT, legacy.sid);
			const auto raw = unit->getUnstackedBonuses(clocks), stacked = unit->getAllBonuses(clocks);
			ASSERT_EQ(raw->size(), 1u); ASSERT_EQ(stacked->size(), 1u);
			EXPECT_NE(raw->front().get(), stacked->front().get()); // Actual no-alias-match stacked source.
		}
	}
}
