/*
 * NewHorizonsRegenerationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
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
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
SpellID regenerationSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
}

class NewHorizonsRegenerationBattleTest : public BattleTestFixture
{
protected:
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
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const int spellPower = 40, const int natureRank = MasteryLevel::BASIC,
		const bool herbalist = false)
	{
		startGame();
		const auto natureSkill = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
		ASSERT_GE(natureSkill, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(natureSkill), natureRank, ChangeValueMode::ABSOLUTE);
		if(herbalist)
		{
			const auto skillId = std::string(newHorizonsMagic::NATURE_MAGIC_SKILL);
			const auto perkId = std::string(newHorizonsMagic::NATURE_HERBALIST);
			attackerSideHero->applyPerkSelection({skillId, perkId});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skillId, perkId));
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(regenerationSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		removeDeployedUnits();
		target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1);
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

	void injure(CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		state->damage(amount);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	int64_t heal(CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		const int64_t requested = amount;
		const int64_t restored = state->heal(amount, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = restored;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
		EXPECT_LE(restored, requested);
		return restored;
	}

	bool advanceUntilNextActivation(const CStack * stack)
	{
		for(int attempt = 0; attempt < 32; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			const auto player = battle()->sideToPlayer(active->unitSide());
			const auto action = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action))
				return false;
			if(battle()->battleActiveUnit() == stack)
				return true;
		}
		return false;
	}

	bool castRegeneration(const CStack * stack)
	{
		return castOn(attackerSideHero, regenerationSpell(), stack);
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
};
}

TEST_F(NewHorizonsRegenerationBattleTest, RealCastSnapshotsSavedSchoolRankAndHerbalistRate)
{
	prepare(40, MasteryLevel::BASIC, true);
	ASSERT_TRUE(castRegeneration(target));
	const SpellID spell = regenerationSpell();
	const auto markerSelector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::HP_REGENERATION));
	ASSERT_TRUE(target->hasBonus(markerSelector));
	EXPECT_EQ(target->valOfBonuses(BonusType::HP_REGENERATION), 0)
		<< "the legacy per-turn passive path must not heal in addition to the activation effect";
	EXPECT_EQ(target->regenerationRateMillionths, 419'000);
	EXPECT_EQ(target->regenerationPendingMicroHealth, 0);
}

TEST_F(NewHorizonsRegenerationBattleTest, BasicSchoolAndSpellcraftScaleOnlyTheSpellPowerRateTerm)
{
	prepare(40, MasteryLevel::BASIC, false);
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castRegeneration(target));

	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, regenerationSpell()), 12650);
	EXPECT_EQ(target->regenerationRateMillionths, 325'900)
		<< "The 250,000 base rate remains fixed; only 15 × Spell Power uses 126.5%";
}

TEST_F(NewHorizonsRegenerationBattleTest, RecastRetainsMarksAndNextActivationHealsOnlyCurrentWounds)
{
	prepare(40, MasteryLevel::BASIC, true);
	ASSERT_TRUE(castRegeneration(target));
	// A hero receives only one ordinary spell action per round. Cross the round
	// boundary before testing a recast, with no pending marks yet, so the target
	// can take its intervening activation without consuming the marks under test.
	endRound();
	const int32_t livingCountBeforeDamage = target->getCount();
	for(int hit = 0; hit < 4; ++hit)
		injure(target, 1);
	EXPECT_EQ(target->regenerationPendingMicroHealth, INT64_C(1676000));
	EXPECT_EQ(target->regenerationProjectedHeal(), 1);

	ASSERT_TRUE(castRegeneration(target));
	EXPECT_EQ(target->regenerationPendingMicroHealth, INT64_C(1676000))
		<< "refreshing the duration must retain already-marked health without duplicating it";
	const auto markerSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(regenerationSpell())).And(Selector::type()(BonusType::HP_REGENERATION));
	EXPECT_EQ(target->getBonuses(markerSelector)->size(), 1u);

	EXPECT_EQ(heal(target, 3), 3);
	EXPECT_EQ(target->getFirstHPleft(), target->getMaxHealth() - 1);
	EXPECT_EQ(target->regenerationProjectedHeal(), 1)
		<< "pending marks larger than the remaining wound cannot over-heal the survivor";
	const int64_t availableBeforeActivation = target->getAvailableHealth();
	ASSERT_TRUE(advanceUntilNextActivation(target));
	EXPECT_EQ(target->getAvailableHealth(), availableBeforeActivation + 1);
	EXPECT_EQ(target->getCount(), livingCountBeforeDamage);
	EXPECT_EQ(target->regenerationPendingMicroHealth, 0);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Regeneration restores") != std::string::npos;
	}));
}

TEST_F(NewHorizonsRegenerationBattleTest, CasualtiesAreNotMarkedOrResurrected)
{
	prepare();
	ASSERT_TRUE(castRegeneration(target));
	const int32_t initialCount = target->getCount();
	const int64_t maximumHealth = target->getMaxHealth();
	injure(target, maximumHealth + 2);
	ASSERT_EQ(target->getCount(), initialCount - 1);
	ASSERT_EQ(target->getFirstHPleft(), maximumHealth - 2);
	EXPECT_EQ(target->regenerationPendingMicroHealth, INT64_C(638000));
	EXPECT_EQ(target->regenerationProjectedHeal(), 0);

	injure(target, 1);
	injure(target, 1);
	EXPECT_EQ(target->regenerationPendingMicroHealth, INT64_C(1276000));
	EXPECT_EQ(target->regenerationProjectedHeal(), 1);
	ASSERT_TRUE(advanceUntilNextActivation(target));
	EXPECT_EQ(target->getCount(), initialCount - 1);
	EXPECT_EQ(target->regenerationPendingMicroHealth, 0);
}

TEST_F(NewHorizonsRegenerationBattleTest, DiscardsMarksWhenMarkedTopCreatureDiesAfterEffectExpires)
{
	prepare();
	ASSERT_TRUE(castRegeneration(target));
	const int32_t initialCount = target->getCount();
	const int64_t maximumHealth = target->getMaxHealth();
	ASSERT_GE(maximumHealth, 3);

	// Mark the old top creature's wounds, then let the effect expire before a
	// later hit kills it and spills one point of damage onto the next creature.
	injure(target, maximumHealth - 2);
	ASSERT_GT(target->regenerationPendingMicroHealth, 0);
	const auto markerSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(regenerationSpell())).And(Selector::type()(BonusType::HP_REGENERATION));
	target->removeBonusesRecursive(markerSelector);
	ASSERT_FALSE(target->hasBonus(markerSelector));

	injure(target, 3);
	ASSERT_EQ(target->getCount(), initialCount - 1);
	EXPECT_EQ(target->getFirstHPleft(), maximumHealth - 1);
	EXPECT_EQ(target->regenerationPendingMicroHealth, 0)
		<< "marks on the dead top creature must not transfer to the next survivor";
	EXPECT_EQ(target->regenerationProjectedHeal(), 0);

	const int64_t survivingHealth = target->getAvailableHealth();
	ASSERT_TRUE(advanceUntilNextActivation(target));
	EXPECT_EQ(target->getAvailableHealth(), survivingHealth);
	EXPECT_EQ(target->getCount(), initialCount - 1)
		<< "Regeneration must not resurrect the casualty or heal unmarked survivor damage";
}

TEST_F(NewHorizonsRegenerationBattleTest, RejectsEnemyAndUndeadStacks)
{
	prepare();
	auto * undead = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(4, 4), 3);
	ASSERT_NE(undead, nullptr);
	EXPECT_FALSE(castRegeneration(enemy));
	EXPECT_FALSE(castRegeneration(undead));
}

TEST_F(NewHorizonsRegenerationBattleTest, StateRoundTripPreservesFixedPointAccumulator)
{
	prepare();
	ASSERT_TRUE(castRegeneration(target));
	injure(target, 3);
	const auto saved = target->save();
	auto restored = target->acquireState();
	restored->load(saved);
	EXPECT_EQ(restored->regenerationRateMillionths, 319'000);
	EXPECT_EQ(restored->regenerationPendingMicroHealth, INT64_C(957000));
	EXPECT_EQ(restored->regenerationProjectedHeal(), 0);
}
