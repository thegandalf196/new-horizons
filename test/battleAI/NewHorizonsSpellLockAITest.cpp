/*
 * NewHorizonsSpellLockAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "../mock/mock_spells_Mechanics.h"
#include "AI/BattleAI/SpellTargetsEvaluator.h"
#include "lib/GameLibrary.h"
#include "lib/modding/CModHandler.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/NewHorizonsSorcery.h"
#include "../server/battles/HeroCommandFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/gameState/CGameState.h"
#include <vcmi/Environment.h>

namespace
{
using namespace testing;

SpellID spellId(const char * key)
{
	return SpellID(SpellID::decode(key));
}

std::shared_ptr<Bonus> spellBonus(SpellID source, BonusType type, int32_t value = 1)
{
	auto result = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
		value, BonusSourceID(source));
	result->turnsRemain = 2;
	return result;
}

using SpellLockStackMock = test::battle::UnitFake;
}

class NewHorizonsSpellLockAITest : public ::testing::Test
{
protected:
	test::battle::BattleFake battle;
	spells::MechanicsMock mechanics;
	SpellLockStackMock friendly;
	SpellLockStackMock enemy;
	SpellID spell;

	void SetUp() override
	{
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		spell = spellId(newHorizonsSorcery::SPELL_LOCK_SPELL);
		ASSERT_TRUE(spell.hasValue());
		ASSERT_NE(spell.toSpell(), nullptr);

		ON_CALL(friendly, unitSide()).WillByDefault(Return(BattleSide::ATTACKER));
		ON_CALL(enemy, unitSide()).WillByDefault(Return(BattleSide::DEFENDER));
		friendly.redirectBonusesToFake();
		enemy.redirectBonusesToFake();
		for(auto * stack : {&friendly, &enemy})
		{
			ON_CALL(*stack, alive()).WillByDefault(Return(true));
			ON_CALL(*stack, isInvincible()).WillByDefault(Return(false));
			ON_CALL(*stack, getAvailableHealth()).WillByDefault(Return(100));
			ON_CALL(*stack, isValidTarget(_)).WillByDefault(Return(true));
			ON_CALL(*stack, canCast()).WillByDefault(Return(false));
			ON_CALL(*stack, isGhost()).WillByDefault(Return(false));
			ON_CALL(*stack, isHypnotized()).WillByDefault(Return(false));
			ON_CALL(*stack, creatureIndex()).WillByDefault(Return(0));
		}

		ON_CALL(battle, getUnitsIf(_)).WillByDefault([&](const battle::UnitFilter & filter)
		{
			::battle::Units result;
			for(auto * unit : {&friendly, &enemy})
				if(filter(unit))
					result.push_back(unit);
			return result;
		});
		ON_CALL(battle, getSidePlayer(BattleSide::ATTACKER)).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(battle, getSidePlayer(BattleSide::DEFENDER)).WillByDefault(Return(PlayerColor(1)));

		ON_CALL(mechanics, battle()).WillByDefault(Return(&battle));
		ON_CALL(mechanics, getSpell()).WillByDefault(Return(spell.toSpell()));
		ON_CALL(mechanics, getSpellId()).WillByDefault(Return(spell));
		ON_CALL(mechanics, getCasterColor()).WillByDefault(Return(PlayerColor(0)));
		ON_CALL(mechanics, getTargetTypes()).WillByDefault(Return(
			std::vector<spells::AimType>{spells::AimType::CREATURE}));
		ON_CALL(mechanics, getEffectDuration()).WillByDefault(Return(3));
		ON_CALL(mechanics, canBeCastAt(_, _)).WillByDefault(Return(true));
		ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	}
};

TEST_F(NewHorizonsSpellLockAITest, TargetEnumerationSkipsAlreadyLockedStacks)
{
	enemy.addNewBonus(spellBonus(spell, BonusType::MAGIC_RESISTANCE, 100));
	const auto targets = SpellTargetEvaluator::getViableTargets(&mechanics);
	ASSERT_EQ(targets.size(), 1u);
	ASSERT_EQ(targets.front().size(), 1u);
	EXPECT_EQ(targets.front().front().unitValue, &friendly);
}

TEST_F(NewHorizonsSpellLockAITest, ValuesCleansingOrPreservingRelevantEffects)
{
	const auto slow = spellId("core:slow");
	const auto haste = spellId("core:haste");
	friendly.addNewBonus(spellBonus(slow, BonusType::STACKS_SPEED, -3));
	enemy.addNewBonus(spellBonus(haste, BonusType::STACKS_SPEED, 3));

	const auto friendlyValue = SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&friendly)});
	const auto enemyValue = SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&enemy)});
	EXPECT_GT(friendlyValue, 0.0f);
	EXPECT_GT(enemyValue, 0.0f);

	friendly.addNewBonus(spellBonus(spellId("core:deathCloud"), BonusType::STACKS_SPEED, -3));
	EXPECT_FLOAT_EQ(SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&friendly)}), friendlyValue);

	enemy.addNewBonus(spellBonus(spell, BonusType::MAGIC_RESISTANCE, 100));
	EXPECT_EQ(SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&enemy)}), 0.0f);
}

namespace
{
class SpellLockLifetimeEnvironment final : public ::Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SpellLockLifetimeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class SpellLockQueryCountingStack final : public StackWithBonuses
{
	Bonus resistanceProbe;
	Bonus polarityProbe;
	Bonus unrelatedProbe;

public:
	mutable unsigned resistanceQueries = 0;

	SpellLockQueryCountingStack(const HypotheticBattle * owner, const battle::Unit * source)
		: StackWithBonuses(owner, source),
		resistanceProbe(*spellBonus(spellId(newHorizonsSorcery::SPELL_LOCK_SPELL), BonusType::MAGIC_RESISTANCE, 100)),
		polarityProbe(*spellBonus(spellId(newHorizonsSorcery::SPELL_LOCK_SPELL), BonusType::NONE)),
		unrelatedProbe(*spellBonus(spellId("core:haste"), BonusType::STACKS_SPEED))
	{}

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override
	{
		// Count the resistance-only query, not the full snapshot or polarity query.
		if(selector(&resistanceProbe) && !selector(&polarityProbe) && !selector(&unrelatedProbe))
			++resistanceQueries;
		return StackWithBonuses::getAllBonuses(selector, cachingStr);
	}
};

class NewHorizonsSpellLockLifetimeAITest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	std::shared_ptr<SpellLockLifetimeEnvironment> environment;

	void prepare()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		startBattle();
		beginCombat();
		ASSERT_FALSE(battle()->stacks.empty());
		for(const auto & stack : battle()->stacks)
			if(stack->unitSide() == BattleSide::DEFENDER && !stack->hasBonusOfType(BonusType::SIEGE_WEAPON))
			{
				target = stack.get();
				break;
			}
		ASSERT_NE(target, nullptr);
		environment = std::make_shared<SpellLockLifetimeEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> projection()
	{
		return std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	}

	void addSeal(bool beneficial, int turns = 1)
	{
		for(const auto type : {BonusType::MAGIC_RESISTANCE, BonusType::NONE})
		{
			auto bonus = spellBonus(spellId(newHorizonsSorcery::SPELL_LOCK_SPELL), type,
				type == BonusType::MAGIC_RESISTANCE ? 100 : (beneficial ? 1 : -1));
			bonus->turnsRemain = turns;
			target->addNewBonus(bonus);
		}
	}

	std::shared_ptr<Bonus> addTimedSpell(const char * key, BonusType type, int turns = 2)
	{
		auto bonus = spellBonus(spellId(key), type);
		bonus->turnsRemain = turns;
		target->addNewBonus(bonus);
		return bonus;
	}

	std::shared_ptr<Bonus> addPlague()
	{
		auto bonus = spellBonus(spellId("new-horizons:plague"), BonusType::COMBAT_EVENT_TRIGGER, 105);
		bonus->turnsRemain = 3;
		JsonNode parameters;
		parameters["casterSide"].Integer() = static_cast<int32_t>(BattleSide::ATTACKER);
		parameters["spreadAttempts"].Integer() = 0;
		parameters["lastProcessedRound"].Integer() = battle()->battleGetRound() - 1;
		bonus->parameters = std::make_shared<BonusParameters>(parameters);
		target->addNewBonus(bonus);
		return bonus;
	}

	int turns(const IBonusBearer & bearer, const char * key, BonusType type) const
	{
		const auto bonuses = bearer.getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(spellId(key))).And(Selector::type()(type)));
		return bonuses->empty() ? 0 : bonuses->front()->turnsRemain;
	}

	std::vector<Bonus> localEffects(const StackWithBonuses & unit, const char * key) const
	{
		std::vector<Bonus> result;
		for(const auto & bonus : unit.localSpellEffects())
			if(bonus.sid == BonusSourceID(spellId(key)))
				result.push_back(bonus);
		return result;
	}
};
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, RoundAgingQueriesResistanceOnlyWhenPolarityIsPresent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto haste = addTimedSpell("core:haste", BonusType::STACKS_SPEED);
	const auto slow = addTimedSpell("core:slow", BonusType::STACKS_SPEED);
	const auto liveState = target->acquireState()->save();
	const auto lock = spellId(newHorizonsSorcery::SPELL_LOCK_SPELL);
	for(unsigned mode = 0; mode < 5; ++mode)
	{
		SCOPED_TRACE(mode); // Absent, resistance-only, friendly, hostile, polarity-only.
		auto branch = projection();
		SpellLockQueryCountingStack unit(branch.get(), static_cast<const battle::Unit *>(target));
		if(mode >= 1 && mode <= 3)
			unit.addUnitBonus({*spellBonus(lock, BonusType::MAGIC_RESISTANCE, 100)});
		if(mode >= 2)
			unit.addUnitBonus({*spellBonus(lock, BonusType::NONE, mode == 3 ? 0 : 1)});
		const auto version = unit.getTreeVersion();
		unit.resistanceQueries = 0;
		unit.advanceTimedRound();
		EXPECT_EQ(unit.resistanceQueries, mode >= 2 ? 1u : 0u);
		EXPECT_EQ(unit.getTreeVersion(), version + 1);
		EXPECT_EQ(turns(unit, "core:haste", BonusType::STACKS_SPEED), mode == 2 ? 2 : 1);
		EXPECT_EQ(turns(unit, "core:slow", BonusType::STACKS_SPEED), mode == 3 ? 2 : 1);
		const auto localHaste = localEffects(unit, "core:haste");
		const auto localSlow = localEffects(unit, "core:slow");
		ASSERT_EQ(localHaste.size(), 1u);
		ASSERT_EQ(localSlow.size(), 1u);
		EXPECT_EQ(localHaste.front().turnsRemain, mode == 2 ? 2 : 1);
		EXPECT_EQ(localSlow.front().turnsRemain, mode == 3 ? 2 : 1);
		EXPECT_EQ(target->acquireState()->save(), liveState);
		EXPECT_EQ(haste->turnsRemain, 2);
		EXPECT_EQ(slow->turnsRemain, 2);
	}
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, EnemySealFinalRoundPreservesSlowThenUnlockedRoundAgesIt)
{
	prepare();
	addTimedSpell("core:slow", BonusType::STACKS_SPEED);
	addSeal(false);
	auto branch = projection();
	branch->nextRound();
	const auto unit = branch->getForUpdate(target->unitId());
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*unit));
	EXPECT_EQ(turns(*unit, "core:slow", BonusType::STACKS_SPEED), 2);
	EXPECT_EQ(localEffects(*unit, "core:slow").size(), 1u);
	branch->nextRound();
	EXPECT_EQ(turns(*unit, "core:slow", BonusType::STACKS_SPEED), 1);
	EXPECT_EQ(turns(*target, "core:slow", BonusType::STACKS_SPEED), 2);
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*target));
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, FriendlySealPausesHasteButNotTimedNonmagicalCommand)
{
	prepare();
	const auto haste = addTimedSpell("core:haste", BonusType::STACKS_SPEED);
	addSeal(true);
	auto command = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::HERO_COMMAND, 1, BonusSourceID());
	command->turnsRemain = 2;
	auto branch = projection();
	const auto unit = branch->getForUpdate(target->unitId());
	Bonus refreshed(*haste);
	refreshed.turnsRemain = 4;
	unit->updateUnitBonus({refreshed});
	unit->addUnitBonus({*command});
	branch->nextRound();
	EXPECT_EQ(turns(*unit, "core:haste", BonusType::STACKS_SPEED), 4);
	const auto local = localEffects(*unit, "core:haste");
	ASSERT_EQ(local.size(), 1u);
	EXPECT_EQ(local.front().turnsRemain, 4);
	const auto commands = unit->getBonuses(Selector::sourceType()(BonusSource::HERO_COMMAND)
		.And(Selector::type()(BonusType::STACKS_SPEED)));
	ASSERT_EQ(commands->size(), 1u);
	EXPECT_EQ(commands->front()->turnsRemain, 1);
	EXPECT_EQ(command->turnsRemain, 2);
	EXPECT_EQ(haste->turnsRemain, 2);
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*unit));
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, UnlockedPlagueDoesNotUseRoundClockOrApplyATick)
{
	prepare();
	const auto marker = addPlague();
	const auto parameters = marker->parameters->toCustom<JsonNode>();
	const auto health = target->getAvailableHealth();
	auto branch = projection();
	branch->nextRound();
	branch->nextRound();
	const auto unit = branch->getForUpdate(target->unitId());
	EXPECT_EQ(turns(*unit, "new-horizons:plague", BonusType::COMBAT_EVENT_TRIGGER), 3);
	EXPECT_EQ(unit->getAvailableHealth(), health);
	const auto local = localEffects(*unit, "new-horizons:plague");
	ASSERT_EQ(local.size(), 1u);
	ASSERT_NE(local.front().parameters, nullptr);
	EXPECT_EQ(local.front().parameters->toCustom<JsonNode>(), parameters);
	EXPECT_EQ(marker->turnsRemain, 3);
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, LockedPlagueRetainsActivationReceiptWhileSealExpires)
{
	prepare();
	const auto marker = addPlague();
	addSeal(false);
	auto branch = projection();
	branch->nextRound();
	const auto unit = branch->getForUpdate(target->unitId());
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*unit));
	EXPECT_EQ(turns(*unit, "new-horizons:plague", BonusType::COMBAT_EVENT_TRIGGER), 3);
	const auto local = localEffects(*unit, "new-horizons:plague");
	ASSERT_EQ(local.size(), 1u);
	EXPECT_EQ(local.front().parameters->toCustom<JsonNode>(), marker->parameters->toCustom<JsonNode>());
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*target));
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, InheritedRawAliasesAgeOnceWithoutChangingSourceLocalIdentity)
{
	prepare();
	const auto shared = addTimedSpell("core:haste", BonusType::STACKS_SPEED);
	// The same physical Bonus is both inherited and source-local. Raw views must
	// reconnect aliases to one branch clone, not decrement each list occurrence.
	ASSERT_EQ(target->unitSide(), BattleSide::DEFENDER);
	defenderSideHero->addNewBonus(shared);
	auto branch = projection();
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellId("core:haste")))
		.And(Selector::type()(BonusType::STACKS_SPEED));
	const auto before = branch->getForUpdate(target->unitId())->getUnstackedBonuses(selector);
	ASSERT_EQ(before->size(), 2u);
	EXPECT_EQ((*before)[0].get(), (*before)[1].get());
	branch->nextRound();
	const auto unit = branch->getForUpdate(target->unitId());
	const auto raw = unit->getUnstackedBonuses(selector);
	ASSERT_EQ(raw->size(), 2u);
	EXPECT_EQ((*raw)[0].get(), (*raw)[1].get());
	EXPECT_EQ((*raw)[0]->turnsRemain, 1);
	const auto local = localEffects(*unit, "core:haste");
	ASSERT_EQ(local.size(), 1u);
	EXPECT_EQ(local.front().turnsRemain, 1);
	EXPECT_EQ(shared->turnsRemain, 2);
}

TEST_F(NewHorizonsSpellLockLifetimeAITest, LazyChildAndSiblingKeepTimerSnapshotsWhenParentChanges)
{
	prepare();
	addTimedSpell("core:slow", BonusType::STACKS_SPEED);
	addSeal(false);
	auto parent = projection();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	// No explicit child getForUpdate before parent mutation: construction must
	// freeze only the relevant effect-bearing units, even when they are inactive.
	parent->nextRound();
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*parent->getForUpdate(target->unitId())));
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*child->getForUpdate(target->unitId())));
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*sibling->getForUpdate(target->unitId())));
	child->nextRound();
	parent->nextRound();
	EXPECT_EQ(turns(*parent->getForUpdate(target->unitId()), "core:slow", BonusType::STACKS_SPEED), 1);
	EXPECT_EQ(turns(*child->getForUpdate(target->unitId()), "core:slow", BonusType::STACKS_SPEED), 2);
	EXPECT_EQ(turns(*sibling->getForUpdate(target->unitId()), "core:slow", BonusType::STACKS_SPEED), 2);
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*target));
	EXPECT_EQ(turns(*target, "core:slow", BonusType::STACKS_SPEED), 2);
	// Existing unsealed timed magic also snapshots the ABSENCE of a seal.
	auto unsealed = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto lock = spellId(newHorizonsSorcery::SPELL_LOCK_SPELL);
	parent->getForUpdate(target->unitId())->addUnitBonus({
		*spellBonus(lock, BonusType::MAGIC_RESISTANCE, 100), *spellBonus(lock, BonusType::NONE, -1)});
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*unsealed->getForUpdate(target->unitId())));
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*parent->getForUpdate(target->unitId())));
}

TEST_F(NewHorizonsSpellLockAITest, DoesNotValueAnUnbuffedEnemyCaster)
{
	ON_CALL(enemy, canCast()).WillByDefault(Return(true));
	EXPECT_EQ(SpellTargetEvaluator::spellLockPlacementValue(
		&mechanics, spells::Target{spells::Destination(&enemy)}), 0.0f);
}
