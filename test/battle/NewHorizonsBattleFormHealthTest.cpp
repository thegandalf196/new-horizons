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
		return result;
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

	const JsonNode saved = state->save();
	auto restored = liveStack.acquireState();
	ASSERT_TRUE(restored);
	restored->load(saved);
	EXPECT_TRUE(restored->hasBattleForm());
	EXPECT_EQ(restored->getMaxHealth(), formCreature->getMaxHealth());
	EXPECT_EQ(restored->health.getCreatureHealthAvailable(), originalHP);
	EXPECT_FALSE(restored->getAllBonuses(CSelector(nativeForForm))->empty());

	restored->endBattleForm();
	EXPECT_FALSE(restored->hasBattleForm());
	EXPECT_EQ(restored->battleFormCreature(), sourceCreatureID);
	EXPECT_EQ(restored->getMaxHealth(), sourceCreature->getMaxHealth());
	EXPECT_EQ(restored->health.getCreatureHealthAvailable(), originalHP);
	EXPECT_FALSE(restored->getAllBonuses(CSelector(nativeForSource))->empty());
	EXPECT_TRUE(restored->getAllBonuses(CSelector(nativeForForm))->empty());
	EXPECT_EQ(liveStack.unitType(), sourceCreature);
}
