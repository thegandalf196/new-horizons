/*
 * OwnedExportRoundSequenceTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportLifecycleFixture.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/ProjectedBonusView.h"

TEST_F(OwnedExportLifecycleTest, RealRoundPacketThenIncomingAndRetaliationMatchFrozenSequenceOracles)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true, false, false, 1));
	auto value = aura(BonusDuration::N_TURNS);
	value.turnsRemain = 1;
	incoming->addNewBonus(std::make_shared<Bonus>(value));
	captureOwned();
	startOwned();
	known->nextRound();
	known = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	EXPECT_FALSE(result.activationComplete);
	EXPECT_FALSE(result.absoluteHorizonComplete);
	const auto expected = freezeOracle(result);
	retire(result);
	roundPacket();
	compareNativeFrozen(expected);
}

TEST_F(OwnedExportLifecycleTest, ActivationClockPlagueIsNotAgedByNativeOrOwnedRoundClock)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	const auto plague = SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::COMBAT_EVENT_TRIGGER, BonusSource::SPELL_EFFECT, 1, BonusSourceID(plague));
	marker->turnsRemain = 3;
	incoming->addNewBonus(marker);
	captureOwned();
	startOwned();
	known->nextRound();
	const auto selected = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plague));
	const auto effects = known->battleGetUnitByID(incoming->unitId())->getBonuses(selected);
	ASSERT_EQ(effects->size(), 1);
	EXPECT_EQ(effects->front()->turnsRemain, 3);
	const auto frozen = freezeFrame();
	retireFrame();
	roundPacket();
	EXPECT_EQ(marker->turnsRemain, 3);
	compareFrame(frozen);
}

TEST_F(OwnedExportLifecycleTest, SpellLockPolarityIsCapturedBeforeItsOwnLastRoundExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	const auto lock = SpellID(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	for(const auto type : {BonusType::NONE, BonusType::MAGIC_RESISTANCE})
	{
		auto seal = std::make_shared<Bonus>(BonusDuration::N_TURNS,
			type, BonusSource::SPELL_EFFECT, 1, BonusSourceID(lock));
		seal->turnsRemain = 1;
		incoming->addNewBonus(seal);
	}
	auto blessing = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::ALWAYS_MAXIMUM_DAMAGE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::BLESS)));
	blessing->turnsRemain = 2;
	incoming->addNewBonus(blessing);
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*incoming));
	ASSERT_TRUE(newHorizonsSorcery::isPreservedSpellLockEffect(blessing.get(), true));
	captureOwned();
	startOwned();
	known->nextRound();
	const auto frozen = freezeFrame();
	const auto projected = known->battleGetUnitByID(incoming->unitId())->getBonuses(
		Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))));
	ASSERT_EQ(projected->size(), 1);
	const auto duration = projected->front()->turnsRemain;
	retireFrame();
	roundPacket();
	EXPECT_EQ(blessing->turnsRemain, duration);
	EXPECT_EQ(duration, 2);
	compareFrame(frozen);
}

TEST_F(OwnedExportLifecycleTest, PropagationUpdaterCopyKeepsNativeMaterializedTimerUntilSourceRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	ASSERT_GT(battle()->getRound(), 0);
	auto value = aura(BonusDuration::N_TURNS);
	value.turnsRemain = 2;
	value.propagationUpdater = std::make_shared<TimesStackSizeUpdater>(0, 1000, 10, 1);
	incoming->addNewBonus(std::make_shared<Bonus>(value));
	captureOwned();
	startOwned();
	known->nextRound();
	const auto projected = known->battleGetUnitByID(braced->unitId())->getBonuses(
		CSelector([](const Bonus * record) { return record->propagationUpdater != nullptr; }));
	ASSERT_EQ(projected->size(), 1);
	EXPECT_EQ(projected->front()->turnsRemain, 2); // Not the source's new duration 1.
	known = std::make_shared<HypotheticBattle>(environment.get(), known);
	known->nextRound();
	EXPECT_TRUE(known->battleGetUnitByID(braced->unitId())->getBonuses(
		CSelector([](const Bonus * record) { return record->propagationUpdater != nullptr; }))->empty());
	const auto expected = freezeFrame();
	retireFrame();
	roundPacket();
	const auto actual = braced->getBonuses(CSelector([](const Bonus * record) { return record->propagationUpdater != nullptr; }));
	ASSERT_EQ(actual->size(), 1);
	EXPECT_EQ(actual->front()->turnsRemain, 2);
	roundPacket();
	compareFrame(expected);
}

TEST_F(OwnedExportLifecycleTest, TimeStopPausesExportedRoundRecordsButStillRunsNativeRoundScalars)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	auto stopped = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped->turnsRemain = 2;
	incoming->addNewBonus(stopped);
	auto speed = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -2, BonusSourceID());
	speed->turnsRemain = 2;
	incoming->addNewBonus(speed);
	ASSERT_TRUE(incoming->isTimeStopped());
	captureOwned();
	startOwned();
	known->nextRound();
	ASSERT_TRUE(known->getForUpdate(incoming->unitId())->isTimeStopped());
	const auto frozen = freezeFrame();
	retireFrame();
	roundPacket();
	EXPECT_EQ(stopped->turnsRemain, 2);
	EXPECT_EQ(speed->turnsRemain, 2);
	compareFrame(frozen);
}

TEST_F(OwnedExportLifecycleTest, CanonicalSourceSpellProvenanceExcludesInheritanceAndKeepsRemovalForkLocal)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	auto local = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE)));
	local->turnsRemain = 3;
	incoming->addNewBonus(local);
	auto inherited = std::make_shared<Bonus>(*local);
	inherited->val = 7;
	defenderSideHero->addNewBonus(inherited);
	const auto nativeExports = incoming->getExportedBonusList().size();
	captureOwned();
	const auto index = seed->occurrence(incoming->unitId());
	const auto source = seed->world().sourceAt(index);
	auto read = [index](const InitialExportWorld & world)
	{
		const auto * view = dynamic_cast<const ProjectedBonusView *>(world.unitAt(index).getBonusBearer());
		if(!view)
			throw std::logic_error("Canonical fixture lost its actual source provider");
		return view->sourceLocalSpellEffects();
	};
	ASSERT_EQ(seed->world().unitAt(index).getBonuses(
		Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE))))->size(), 2);
	const auto initial = read(seed->world());
	ASSERT_EQ(initial.size(), 1);
	EXPECT_EQ(projectedHitTest::serialized(initial.front()), projectedHitTest::serialized(*local));
	Bonus added(*local);
	added.val = 11;
	const auto handle = seed->world().add(source, added);
	const auto held = read(seed->world());
	ASSERT_EQ(held.size(), 2);
	auto sibling = seed->world().fork();
	seed->world().remove(handle);
	EXPECT_EQ(read(seed->world()).size(), 1);
	EXPECT_EQ(read(*sibling).size(), 2);
	EXPECT_EQ(held.back().val, 11);
	EXPECT_EQ(projectedHitTest::serialized(read(*sibling).back()), projectedHitTest::serialized(added));
	ASSERT_EQ(incoming->getExportedBonusList().size(), nativeExports);
	EXPECT_TRUE(vstd::contains(incoming->getExportedBonusList(), local));
	EXPECT_EQ(local->val, 3);
	EXPECT_EQ(local->turnsRemain, 3);
}
