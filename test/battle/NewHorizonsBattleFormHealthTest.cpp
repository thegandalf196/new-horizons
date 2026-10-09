/*
 * NewHorizonsBattleFormHealthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/AccessibilityInfo.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/JsonSerializeFormat.h"

namespace
{
using namespace ::testing;
using namespace battle;

const CreatureID SOURCE_CREATURE(0);
const CreatureID SMALL_FORM(1);
const CreatureID LARGE_FORM(2);
constexpr int32_t STACK_SIZE = 3;

class FormHealthBonusBearer final : public BonusBearerMock
{
public:
	const CUnitState * state = nullptr;
	bool timeStopped = false;
	int32_t treeVersion = 1;

	void setTimeStopped(bool value)
	{
		if(timeStopped != value)
		{
			timeStopped = value;
			++treeVersion;
		}
	}

	int32_t getTreeVersion() const override { return treeVersion; }

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		auto result = std::make_shared<BonusList>();
		const CreatureID effectiveCreature = state ? state->battleFormCreature() : SOURCE_CREATURE;
		const int32_t maximumHealth = effectiveCreature == SMALL_FORM ? 50
			: effectiveCreature == LARGE_FORM ? 200
			: 100;
		auto healthBonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, maximumHealth, BonusSourceID());
		if(selector && selector(healthBonus.get()))
			result->push_back(std::move(healthBonus));

		const auto addInitiativeBonus = [&](const CreatureID creature, const int32_t initiative)
		{
			auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY,
				initiative, BonusSourceID(creature));
			if(selector && isBattleFormNativeBonus(bonus.get(), effectiveCreature) && selector(bonus.get()))
				result->push_back(std::move(bonus));
		};
		addInitiativeBonus(SOURCE_CREATURE, 17);
		addInitiativeBonus(SMALL_FORM, 9);
		addInitiativeBonus(LARGE_FORM, 9);

		if(timeStopped)
		{
			auto timeStopBonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::TIME_STOP, BonusSource::SPELL_EFFECT, 1, BonusSourceID());
			if(selector && selector(timeStopBonus.get()))
				result->push_back(std::move(timeStopBonus));
		}
		return result;
	}

	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		// This fixture constructs its source list without stacking.
		return getAllBonuses(selector, {});
	}
};

class FormTestUnitState final : public CUnitState
{
public:
	FormTestUnitState(const IUnitInfo * unit_, const IBonusBearer * bonus_)
		: unit(unit_), bonus(bonus_)
	{}

	FormTestUnitState & operator=(const CUnitState & other)
	{
		CUnitState::operator=(other);
		return *this;
	}

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = {}) const override
	{
		return bonus->getAllBonuses(selector, cachingStr);
	}

	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		return bonus->getUnstackedBonuses(selector);
	}

	int32_t getTreeVersion() const override
	{
		return bonus->getTreeVersion() + getBattleFormViewRevision();
	}

	uint32_t unitId() const override { return unit->unitId(); }
	BattleSide unitSide() const override { return unit->unitSide(); }
	const CCreature * unitType() const override
	{
		return hasBattleFormState() ? battleFormCreature().toCreature() : unit->unitType();
	}
	PlayerColor unitOwner() const override { return unit->unitOwner(); }
	SlotID unitSlot() const override { return unit->unitSlot(); }
	int32_t unitBaseAmount() const override { return unit->unitBaseAmount(); }
	void spendMana(ServerCallback *, const int) const override {}

private:
	const IUnitInfo * unit;
	const IBonusBearer * bonus;
};

void configureUnitInfo(UnitInfoMock & info, const CCreature * creature)
{
	EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(STACK_SIZE));
	EXPECT_CALL(info, unitType()).WillRepeatedly(Return(creature));
	EXPECT_CALL(info, unitId()).WillRepeatedly(Return(17));
	EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
	EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor::NEUTRAL));
	EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
}
}

TEST(NewHorizonsBattleFormHealthTest, PreservesHealthProvenanceAcrossRepartitionCopySaveAndExpiry)
{
	UnitEnvironmentMock environment;
	UnitInfoMock sourceInfo;
	FormHealthBonusBearer sourceBonuses;
	FormTestUnitState source(&sourceInfo, &sourceBonuses);
	configureUnitInfo(sourceInfo, SOURCE_CREATURE.toCreature());
	sourceBonuses.state = &source;
	source.localInit(&environment);

	int64_t damage = 37;
	source.damage(damage);
	ASSERT_EQ(damage, 37);
	ASSERT_EQ(source.health.getCreatureHealthAvailable(), 263);
	source.health.addTemporaryHitPoints(25);
	const auto originalUnitID = source.unitId();

	source.beginBattleForm(SMALL_FORM, 3);
	ASSERT_TRUE(source.hasBattleForm());
	EXPECT_EQ(source.unitId(), originalUnitID);
	EXPECT_EQ(source.battleFormOriginalCreature(), SOURCE_CREATURE);
	EXPECT_EQ(source.battleFormCreature(), SMALL_FORM);
	EXPECT_EQ(source.getMaxHealth(), 50);
	EXPECT_EQ(source.getCount(), 6);
	EXPECT_EQ(source.getFirstHPleft(), 13);
	EXPECT_EQ(source.health.getCreatureHealthAvailable(), 263);
	EXPECT_EQ(source.getAvailableHealth(), 288);
	EXPECT_EQ(source.getTotalHealth(), 300);

	const JsonNode saved = source.save();
	UnitInfoMock restoredInfo;
	FormHealthBonusBearer restoredBonuses;
	FormTestUnitState restored(&restoredInfo, &restoredBonuses);
	configureUnitInfo(restoredInfo, SOURCE_CREATURE.toCreature());
	restoredBonuses.state = &restored;
	restored.localInit(&environment);
	restored.load(saved);
	ASSERT_TRUE(restored.hasBattleForm());
	EXPECT_EQ(restored.battleFormCreature(), SMALL_FORM);
	EXPECT_EQ(restored.battleFormOriginalCreature(), SOURCE_CREATURE);
	EXPECT_EQ(restored.getMaxHealth(), 50);
	EXPECT_EQ(restored.getCount(), 6);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 263);
	EXPECT_EQ(restored.getAvailableHealth(), 288);

	// A detached wrapper can itself be copied and expire without falling back
	// to the intermediate bearer species.
	UnitInfoMock intermediateInfo;
	FormHealthBonusBearer intermediateBonuses;
	FormTestUnitState intermediate(&intermediateInfo, &intermediateBonuses);
	configureUnitInfo(intermediateInfo, SMALL_FORM.toCreature());
	intermediateBonuses.state = &intermediate;
	intermediate.localInit(&environment);
	intermediate.operator=(static_cast<const CUnitState &>(restored));
	ASSERT_TRUE(intermediate.hasBattleForm());
	EXPECT_EQ(intermediate.battleFormOriginalCreature(), SOURCE_CREATURE);
	intermediate.endBattleForm();
	EXPECT_FALSE(intermediate.hasBattleForm());
	EXPECT_EQ(intermediate.battleFormCreature(), SOURCE_CREATURE);
	EXPECT_EQ(intermediate.unitType(), SOURCE_CREATURE.toCreature());
	EXPECT_EQ(intermediate.health.getCreatureHealthAvailable(), 263);

	// Both growth and shrinkage of unit count derive only from creature HP;
	// temporary HP remains a separate pool and does not affect either count.
	restored.beginBattleForm(LARGE_FORM, 3);
	EXPECT_EQ(restored.getMaxHealth(), 200);
	EXPECT_EQ(restored.getCount(), 2);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 263);
	EXPECT_EQ(restored.health.getTemporaryHitPoints(), 25);
	restored.beginBattleForm(SMALL_FORM, 3);
	EXPECT_EQ(restored.getCount(), 6);
	EXPECT_EQ(restored.health.getTemporaryHitPoints(), 25);

	// A transformed-count casualty is not an original-species unusable corpse.
	damage = 39; // 25 temporary HP plus 14 creature HP
	restored.damage(damage, true);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 249);
	EXPECT_EQ(restored.getKilled(), 0);
	EXPECT_EQ(restored.getUnusableRemains(), 0);
	int64_t healing = 51;
	restored.heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(healing, 51);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 300);

	restored.health.addTemporaryHitPoints(25);
	damage = 10;
	restored.damage(damage);
	EXPECT_EQ(damage, 10);
	EXPECT_EQ(restored.health.getTemporaryHitPoints(), 15);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 300);
	damage = 30;
	restored.damage(damage);
	EXPECT_EQ(restored.health.getTemporaryHitPoints(), 0);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 285);

	// Source-species casualty provenance, including one-battle restoration,
	// remains independent from the transformed count and no-remains wounds.
	damage = 150;
	restored.damage(damage);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 135);
	EXPECT_EQ(restored.getKilled(), 1);

	healing = 100;
	restored.heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(healing, 100);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 235);
	EXPECT_EQ(restored.getKilled(), 1);

	damage = 100;
	restored.damage(damage, true);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 135);
	EXPECT_EQ(restored.getUnusableRemains(), 1);
	// The destroyed temporary resurrection is no longer alive to remove at
	// battle end; it does not create a second permanent source casualty.
	EXPECT_EQ(restored.getKilled(), 1);

	healing = 1000;
	restored.heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(healing, 65);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 200);
	EXPECT_EQ(restored.getUnusableRemains(), 1);
	EXPECT_EQ(restored.getKilled(), 1);

	restored.afterNewRound();
	EXPECT_TRUE(restored.hasBattleForm());
	restored.afterNewRound();
	EXPECT_TRUE(restored.hasBattleForm());
	restored.afterNewRound();
	EXPECT_FALSE(restored.hasBattleForm());
	EXPECT_TRUE(restored.hasBattleFormState()); // stable source identity is retained
	EXPECT_EQ(restored.battleFormCreature(), SOURCE_CREATURE);
	EXPECT_EQ(restored.getMaxHealth(), 100);
	EXPECT_EQ(restored.getCount(), 2);
	EXPECT_EQ(restored.getFirstHPleft(), 100);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), 200);
	EXPECT_EQ(restored.getAvailableHealth(), 200);
	EXPECT_EQ(restored.getKilled(), 1);
	EXPECT_EQ(restored.getUnusableRemains(), 1);
}

TEST(NewHorizonsBattleFormHealthTest, AcquiredCStackStateUsesReplacementNativeBonusesAndRoundTrips)
{
	const CreatureID sourceCreatureID = CreatureID::decode("core:angel");
	const CreatureID formCreatureID = CreatureID::decode("core:imp");
	const CCreature * sourceCreature = sourceCreatureID.toCreature();
	const CCreature * formCreature = formCreatureID.toCreature();
	ASSERT_NE(sourceCreature, nullptr);
	ASSERT_NE(formCreature, nullptr);
	ASSERT_NE(sourceCreatureID, formCreatureID);

	CStackBasicDescriptor descriptor(sourceCreatureID, STACK_SIZE);
	CStack liveStack(&descriptor, PlayerColor::NEUTRAL, 17, BattleSide::ATTACKER, SlotID(0), true);
	liveStack.attachToSource(*sourceCreature);
	liveStack.health.init();
	auto state = liveStack.acquireState();
	ASSERT_TRUE(state);
	ASSERT_EQ(state->getMaxHealth(), sourceCreature->getMaxHealth());
	const uint32_t originalUnitID = state->unitId();

	int64_t damage = 1;
	state->damage(damage);
	ASSERT_EQ(state->health.getCreatureHealthAvailable(), static_cast<int64_t>(sourceCreature->getMaxHealth()) * STACK_SIZE - 1);
	const int64_t originalHP = state->health.getCreatureHealthAvailable();
	state->beginBattleForm(formCreatureID, 2);
	EXPECT_EQ(state->unitId(), originalUnitID);
	EXPECT_EQ(state->battleFormOriginalCreature(), sourceCreatureID);
	EXPECT_EQ(state->getMaxHealth(), formCreature->getMaxHealth());
	EXPECT_EQ(state->health.getCreatureHealthAvailable(), originalHP);
	EXPECT_EQ(state->getCount(), (originalHP + formCreature->getMaxHealth() - 1) / formCreature->getMaxHealth());

	const auto nativeForForm = [formCreatureID](const Bonus * bonus)
	{
		return isBattleFormNativeBonus(bonus, formCreatureID);
	};
	const auto nativeForSource = [sourceCreatureID](const Bonus * bonus)
	{
		return isBattleFormNativeBonus(bonus, sourceCreatureID);
	};
	EXPECT_FALSE(state->getAllBonuses(CSelector(nativeForForm))->empty());
	EXPECT_TRUE(state->getAllBonuses(CSelector(nativeForSource))->empty());
	EXPECT_FALSE(state->getUnstackedBonuses(CSelector(nativeForForm))->empty());
	EXPECT_TRUE(state->getUnstackedBonuses(CSelector(nativeForSource))->empty());

	const JsonNode saved = state->save();
	auto restored = liveStack.acquireState();
	ASSERT_TRUE(restored);
	restored->load(saved);
	EXPECT_TRUE(restored->hasBattleForm());
	EXPECT_EQ(restored->getMaxHealth(), formCreature->getMaxHealth());
	EXPECT_EQ(restored->health.getCreatureHealthAvailable(), originalHP);
	EXPECT_FALSE(restored->getAllBonuses(CSelector(nativeForForm))->empty());
	EXPECT_FALSE(restored->getUnstackedBonuses(CSelector(nativeForForm))->empty());

	restored->endBattleForm();
	EXPECT_FALSE(restored->hasBattleForm());
	EXPECT_EQ(restored->battleFormCreature(), sourceCreatureID);
	EXPECT_EQ(restored->getMaxHealth(), sourceCreature->getMaxHealth());
	EXPECT_EQ(restored->health.getCreatureHealthAvailable(), originalHP);
	EXPECT_FALSE(restored->getAllBonuses(CSelector(nativeForSource))->empty());
	EXPECT_TRUE(restored->getAllBonuses(CSelector(nativeForForm))->empty());
	EXPECT_FALSE(restored->getUnstackedBonuses(CSelector(nativeForSource))->empty());
	EXPECT_TRUE(restored->getUnstackedBonuses(CSelector(nativeForForm))->empty());
	EXPECT_EQ(liveStack.unitType(), sourceCreature);
}

TEST(NewHorizonsBattleFormHealthTest, ReversionRelocatesOriginalFootprintWithoutChangingHealthOrLiveStack)
{
	const CreatureID original = CreatureID::decode("core:archangel");
	const CreatureID replacement = CreatureID::decode("core:imp");
	ASSERT_TRUE(original.toCreature()->isDoubleWide());
	ASSERT_FALSE(replacement.toCreature()->isDoubleWide());
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		CStackBasicDescriptor descriptor(original, STACK_SIZE);
		CStack live(&descriptor, PlayerColor::NEUTRAL, 17, side, SlotID(0), true);
		live.attachToSource(*original.toCreature());
		live.health.init();
		const BattleHex anchor(8, 5);
		live.setPosition(anchor);
		auto state = live.acquireState();
		int64_t damage = 1;
		state->damage(damage);
		const auto hp = state->health.getCreatureHealthAvailable();
		state->beginBattleForm(replacement, 2);
		AccessibilityInfo field;
		field.fill(EAccessibility::ACCESSIBLE);
		const auto blocked = battle::Unit::occupiedHex(anchor, true, side);
		field[blocked.toInt()] = EAccessibility::ALIVE_STACK;
		const auto expected = field.nearestLegalPosition(anchor, true, side);
		ASSERT_TRUE(expected);
		ASSERT_NE(*expected, anchor);
		ASSERT_TRUE(battle::endBattleFormAtNearestLegalPosition(*state, field));
		EXPECT_FALSE(state->hasBattleForm());
		EXPECT_EQ(state->getPosition(), *expected);
		EXPECT_TRUE(field.accessible(state->getPosition(), true, side));
		EXPECT_EQ(state->health.getCreatureHealthAvailable(), hp);
		EXPECT_EQ(state->getCount(), STACK_SIZE);
		EXPECT_EQ(live.getPosition(), anchor);
		EXPECT_FALSE(live.hasBattleForm());
		EXPECT_EQ(field[blocked.toInt()], EAccessibility::ALIVE_STACK);
	}
}

TEST(NewHorizonsBattleFormHealthTest, ImpossibleReversionDoesNotMutateTheStack)
{
	const CreatureID original = CreatureID::decode("core:archangel");
	const CreatureID replacement = CreatureID::decode("core:imp");
	CStackBasicDescriptor descriptor(original, STACK_SIZE);
	CStack live(&descriptor, PlayerColor::NEUTRAL, 17, BattleSide::ATTACKER, SlotID(0), true);
	live.attachToSource(*original.toCreature());
	live.health.init();
	live.setPosition(BattleHex(8, 5));
	auto state = live.acquireState();
	state->beginBattleForm(replacement, 2);
	auto before = state->save();
	AccessibilityInfo field;
	field.fill(EAccessibility::OBSTACLE);
	EXPECT_FALSE(battle::endBattleFormAtNearestLegalPosition(*state, field));
	EXPECT_TRUE(state->isBattleFormRestorationPending());
	EXPECT_EQ(state->getBattleFormRoundsRemaining(), 1);
	before["state"]["battleFormRestorationPending"].Bool() = true;
	before["state"]["battleFormRoundsRemaining"].Integer() = 1;
	EXPECT_EQ(state->save(), before);
}

TEST(NewHorizonsBattleFormHealthTest, TimeStopPausesBattleFormExpiryUntilStasisEnds)
{
	UnitEnvironmentMock environment;
	UnitInfoMock info;
	FormHealthBonusBearer bonuses;
	FormTestUnitState state(&info, &bonuses);
	configureUnitInfo(info, SOURCE_CREATURE.toCreature());
	bonuses.state = &state;
	state.localInit(&environment);
	state.beginBattleForm(SMALL_FORM, 2);
	ASSERT_TRUE(state.hasBattleForm());
	EXPECT_EQ(state.getInitiative(), 17);

	bonuses.setTimeStopped(true);
	ASSERT_TRUE(state.isTimeStopped());
	state.afterNewRound();
	EXPECT_TRUE(state.hasBattleForm());
	EXPECT_EQ(state.save()["state"]["battleFormRoundsRemaining"].Integer(), 2);
	EXPECT_EQ(state.getInitiative(), 9);

	bonuses.setTimeStopped(false);
	ASSERT_FALSE(state.isTimeStopped());
	state.afterNewRound();
	EXPECT_TRUE(state.hasBattleForm());
	EXPECT_EQ(state.save()["state"]["battleFormRoundsRemaining"].Integer(), 1);
	EXPECT_EQ(state.getInitiative(), 9);
	state.afterNewRound();
	EXPECT_FALSE(state.hasBattleForm());
	EXPECT_EQ(state.getInitiative(), 17);
	EXPECT_EQ(state.health.getCreatureHealthAvailable(), static_cast<int64_t>(STACK_SIZE) * 100);
}

TEST(NewHorizonsBattleFormHealthTest, CloneKeepsOneHitDeathAndClearsFormProvenanceBeforeGhosting)
{
	UnitEnvironmentMock environment;
	UnitInfoMock info;
	FormHealthBonusBearer bonuses;
	FormTestUnitState clone(&info, &bonuses);
	configureUnitInfo(info, SOURCE_CREATURE.toCreature());
	bonuses.state = &clone;
	clone.localInit(&environment);
	clone.cloned = true;
	clone.beginBattleForm(SMALL_FORM, 2);
	ASSERT_TRUE(clone.isClone());
	ASSERT_TRUE(clone.hasBattleForm());

	const auto saved = clone.save();
	UnitInfoMock restoredInfo;
	FormHealthBonusBearer restoredBonuses;
	FormTestUnitState restored(&restoredInfo, &restoredBonuses);
	configureUnitInfo(restoredInfo, SOURCE_CREATURE.toCreature());
	restoredBonuses.state = &restored;
	restored.localInit(&environment);
	restored.load(saved);
	ASSERT_TRUE(restored.isClone());
	ASSERT_TRUE(restored.hasBattleForm());

	const int64_t sourceHealth = restored.health.getCreatureHealthAvailable();
	restored.beginBattleForm(LARGE_FORM, 2);
	ASSERT_TRUE(restored.hasBattleForm());
	EXPECT_EQ(restored.battleFormCreature(), LARGE_FORM);
	const auto recast = restored.save();
	restored.load(recast);
	ASSERT_TRUE(restored.isClone());
	ASSERT_TRUE(restored.hasBattleForm());
	EXPECT_EQ(restored.battleFormCreature(), LARGE_FORM);
	restored.endBattleForm();
	EXPECT_FALSE(restored.hasBattleForm());
	EXPECT_TRUE(restored.isClone());
	EXPECT_EQ(restored.battleFormCreature(), SOURCE_CREATURE);
	EXPECT_EQ(restored.health.getCreatureHealthAvailable(), sourceHealth);
	restored.beginBattleForm(SMALL_FORM, 2);
	ASSERT_TRUE(restored.hasBattleForm());

	int64_t damage = 1;
	restored.damage(damage);
	EXPECT_EQ(damage, 0);
	EXPECT_FALSE(restored.alive());
	EXPECT_TRUE(restored.isClone());
	EXPECT_FALSE(restored.hasBattleForm());
	EXPECT_FALSE(restored.health.isBattleFormProvenance());
	EXPECT_EQ(restored.battleFormCreature(), SOURCE_CREATURE);
	EXPECT_EQ(restored.getKilled(), STACK_SIZE);

	EXPECT_NO_THROW(restored.endBattleForm());
	EXPECT_NO_THROW(restored.makeGhost());
	EXPECT_NO_THROW(restored.onRemoved());
	EXPECT_TRUE(restored.isGhost());
	EXPECT_FALSE(restored.hasBattleForm());
	EXPECT_EQ(restored.getKilled(), STACK_SIZE);

	const auto deadClone = restored.save();
	UnitInfoMock ghostInfo;
	FormHealthBonusBearer ghostBonuses;
	FormTestUnitState ghost(&ghostInfo, &ghostBonuses);
	configureUnitInfo(ghostInfo, SOURCE_CREATURE.toCreature());
	ghostBonuses.state = &ghost;
	ghost.localInit(&environment);
	EXPECT_NO_THROW(ghost.load(deadClone));
	EXPECT_TRUE(ghost.isClone());
	EXPECT_TRUE(ghost.isGhost());
	EXPECT_FALSE(ghost.hasBattleForm());
}
