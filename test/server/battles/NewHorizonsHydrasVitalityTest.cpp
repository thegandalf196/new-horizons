/*
 * NewHorizonsHydrasVitalityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
SpellID hydrasVitalitySpell()
{
	static const SpellID spell(SpellID::decode("new-horizons:hydrasVitality"));
	return spell;
}

class NewHorizonsHydrasVitalityBattleTest : public BattleTestFixture
{
protected:
	int32_t savedMagicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * target = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(savedMagicVersion != newHorizonsMagic::CURRENT_RULESET_VERSION)
		{
			rules["rulesetVersion"].Integer() = savedMagicVersion;
			rules.Struct().erase("schoolRankPowerCoefficientPercent");
			rules.Struct().erase("spellcraftEfficiencyPercent");
			if(savedMagicVersion < newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
				rules["spells"]["core:quicksand"].Struct().erase("selectedPlacement");
			for(auto & [identity, row] : rules["spells"].Struct())
			{
				(void)identity;
				if(row.Struct().contains("variant"))
				{
					row.Struct().erase("variant");
					row["active"].Bool() = false;
				}
			}
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int32_t count = 100, const std::string & creature = "core:pikeman",
		int32_t spellPower = 100, int32_t natureRank = MasteryLevel::BASIC)
	{
		startGame();
		const auto natureSkill = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
		ASSERT_GE(natureSkill, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(natureSkill), natureRank, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(hydrasVitalitySpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		removeDeployedUnits();
		target = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), count);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	bool castAtHex(const BattleHex & hex)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = hydrasVitalitySpell();
		action.aimToHex(hex);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castAtUnit(const CStack * unit)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = hydrasVitalitySpell();
		action.aimToUnit(unit);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void applyState(const CStack * stack, std::shared_ptr<battle::CUnitState> state, int64_t healthDelta = 0)
	{
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healthDelta;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	int64_t injure(const CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		state->damage(amount);
		const int64_t applied = amount;
		applyState(stack, state, -applied);
		return applied;
	}

	int64_t heal(const CStack * stack, int64_t amount, EHealLevel level = EHealLevel::HEAL,
		EHealPower power = EHealPower::PERMANENT)
	{
		auto state = stack->acquireState();
		const int64_t requested = amount;
		const int64_t healed = state->heal(amount, level, power).healedHealthPoints;
		applyState(stack, state, healed);
		EXPECT_LE(healed, requested);
		return healed;
	}

	bool advanceUntilNextActivation(const CStack * stack)
	{
		for(int attempt = 0; attempt < 32; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			const auto action = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), action))
				return false;
			if(battle()->battleActiveUnit() == stack)
				return true;
		}
		return false;
	}

	bool hasHydrasVitality(const CStack * stack) const
	{
		const auto source = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(hydrasVitalitySpell()));
		return stack->hasBonus(source.And(Selector::type()(BonusType::STACK_HEALTH)))
			&& stack->hasBonus(source.And(Selector::type()(BonusType::HP_REGENERATION)));
	}
};
}

TEST_F(NewHorizonsHydrasVitalityBattleTest, RawHexWideTailCastDoesNotGrantImmediateHealthAndStateRoundTrips)
{
	prepare(4, "core:basilisk");
	ASSERT_TRUE(target->doubleWide());
	const auto footprint = target->getHexes();
	ASSERT_EQ(footprint.size(), 2u);
	const int32_t originalMaximum = static_cast<int32_t>(target->getMaxHealth());
	const int64_t healthBeforeCast = target->getAvailableHealth();
	const int32_t countBeforeCast = target->getCount();

	ASSERT_TRUE(castAtHex(footprint[1])) << "the raw tail hex must resolve to its one living stack";
	EXPECT_GT(target->getMaxHealth(), originalMaximum);
	EXPECT_EQ(target->getCount(), countBeforeCast);
	EXPECT_EQ(target->getAvailableHealth(), healthBeforeCast)
		<< "raising capacity is not cast-time healing";
	ASSERT_TRUE(hasHydrasVitality(target));

	auto copied = target->acquireState();
	EXPECT_GT(copied->capacityRegenerationProjectedHeal(), 0);
	auto restored = target->acquireState();
	restored->load(copied->save());
	EXPECT_EQ(restored->getCount(), copied->getCount());
	EXPECT_EQ(restored->getAvailableHealth(), copied->getAvailableHealth());
	EXPECT_EQ(restored->capacityRegenerationProjectedHeal(), copied->capacityRegenerationProjectedHeal());
}

TEST_F(NewHorizonsHydrasVitalityBattleTest, ActivationHealsEachSurvivorAndOrdinaryHealNeverRestoresCasualties)
{
	prepare();
	const int32_t countBeforeCast = target->getCount();
	const int64_t healthBeforeCast = target->getAvailableHealth();
	ASSERT_TRUE(castAtUnit(target));
	EXPECT_EQ(target->getCount(), countBeforeCast);
	EXPECT_EQ(target->getAvailableHealth(), healthBeforeCast);

	auto projected = target->acquireState();
	const int64_t expectedActivationHeal = projected->capacityRegenerationProjectedHeal();
	ASSERT_GT(expectedActivationHeal, 0);
	ASSERT_TRUE(advanceUntilNextActivation(target));
	EXPECT_EQ(target->getAvailableHealth(), healthBeforeCast + expectedActivationHeal);
	EXPECT_EQ(target->getCount(), countBeforeCast);

	const int64_t damage = static_cast<int64_t>(target->getMaxHealth()) * 2 + 1;
	ASSERT_EQ(injure(target, damage), damage);
	const int32_t survivorCount = target->getCount();
	ASSERT_LT(survivorCount, countBeforeCast);
	const int64_t healthAfterDamage = target->getAvailableHealth();
	const int64_t healed = heal(target, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(healed, static_cast<int64_t>(survivorCount) * target->getMaxHealth() - healthAfterDamage);
	EXPECT_EQ(target->getCount(), survivorCount)
		<< "ordinary healing fills existing survivor wounds but never creates creatures";
	EXPECT_EQ(target->getAvailableHealth(), static_cast<int64_t>(survivorCount) * target->getMaxHealth());
}

TEST_F(NewHorizonsHydrasVitalityBattleTest, TemporaryResurrectionCleanupRemovesExactlyTheResurrectedCount)
{
	prepare(4);
	const int32_t initialCount = target->getCount();
	ASSERT_EQ(injure(target, 29), 29);
	const int32_t countBeforeResurrection = target->getCount();
	ASSERT_EQ(countBeforeResurrection, initialCount - 2);

	// At the ordinary 10 HP capacity, healing 22 restores two bodies but leaves
	// their aggregate at 33 HP. Hydra's Vitality then makes the maximum larger
	// without changing that exact survivor health. Removing two temporary bodies
	// by `2 * current maximum` used to spill through the 3 HP front and kill a
	// third creature; exact-body cleanup removes 3 + 10 HP and leaves two bodies.
	ASSERT_EQ(heal(target, 22, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE), 22);
	ASSERT_EQ(target->getCount(), initialCount);
	ASSERT_EQ(target->getAvailableHealth(), 33);
	ASSERT_EQ(target->health.getResurrected(), 2);
	ASSERT_TRUE(castOn(attackerSideHero, hydrasVitalitySpell(), target));
	EXPECT_EQ(target->getAvailableHealth(), 33);
	EXPECT_GT(target->getMaxHealth(), 10u);
	auto cleanup = target->acquireState();
	cleanup->health.takeResurrected();
	applyState(target, cleanup);
	EXPECT_EQ(target->getCount(), countBeforeResurrection);
	EXPECT_EQ(target->health.getResurrected(), 0);
	EXPECT_EQ(target->getAvailableHealth(), 20);
}

TEST_F(NewHorizonsHydrasVitalityBattleTest, RecastDoesNotCompoundAndExpiryClampsSurvivorsToOriginalCapacity)
{
	prepare();
	const int32_t originalMaximum = static_cast<int32_t>(target->getMaxHealth());
	ASSERT_TRUE(castOn(attackerSideHero, hydrasVitalitySpell(), target));
	const int32_t enhancedMaximum = static_cast<int32_t>(target->getMaxHealth());
	ASSERT_GT(enhancedMaximum, originalMaximum);

	// A genuine activation may heal between casts, and an ordinary hero can
	// refresh only in a later round. Compare recast preservation to that snapshot.
	endRound();
	const int64_t healthBeforeRecast = target->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, hydrasVitalitySpell(), target));
	EXPECT_EQ(target->getMaxHealth(), enhancedMaximum);
	EXPECT_EQ(target->getAvailableHealth(), healthBeforeRecast);
	EXPECT_TRUE(hasHydrasVitality(target));

	for(int round = 0; round < 5 && hasHydrasVitality(target); ++round)
		endRound();
	EXPECT_FALSE(hasHydrasVitality(target));
	EXPECT_EQ(target->getMaxHealth(), originalMaximum);
	EXPECT_EQ(target->getCount(), 100);
	EXPECT_EQ(target->getAvailableHealth(), static_cast<int64_t>(target->getCount()) * originalMaximum)
		<< "expiry clamps each survivor to its original per-creature maximum without casualties";
	EXPECT_FALSE(target->health.isCapacityHealthTracking());
}

TEST_F(NewHorizonsHydrasVitalityBattleTest, SavedV2RulesRejectTheSpellWithoutSpendingMana)
{
	savedMagicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION - 1;
	prepare(1);
	const int32_t manaBefore = attackerSideHero->getManaAvailable();
	const int32_t countBefore = target->getCount();
	EXPECT_FALSE(castAtUnit(target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(target->getCount(), countBefore);
	EXPECT_FALSE(hasHydrasVitality(target));
}

TEST_F(NewHorizonsHydrasVitalityBattleTest, EnhancedMaximumOverflowIsRejectedBeforeManaCost)
{
	prepare(1);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 1'800'000'000, BonusSourceID()));
	ASSERT_GT(target->getMaxHealth(), 1'700'000'000u);
	const int32_t manaBefore = attackerSideHero->getManaAvailable();
	const int32_t countBefore = target->getCount();
	EXPECT_FALSE(castAtUnit(target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(target->getCount(), countBefore);
	EXPECT_FALSE(hasHydrasVitality(target));
}
