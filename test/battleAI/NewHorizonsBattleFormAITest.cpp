/*
 * NewHorizonsBattleFormAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license is available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/mapObjects/army/CStackInstance.h"

namespace
{
class BattleFormEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BattleFormEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

CSelector creatureNativeSelector(CreatureID creature)
{
	const BonusSourceID sourceId(creature);
	return CSelector([sourceId](const Bonus * bonus)
	{
		return bonus && bonus->source == BonusSource::CREATURE_ABILITY && bonus->sid == sourceId;
	});
}

CSelector creatureRankSelector(CreatureID creature)
{
	const BonusSourceID sourceId(creature);
	return CSelector([sourceId](const Bonus * bonus)
	{
		return bonus && bonus->source == BonusSource::STACK_EXPERIENCE && bonus->sid == sourceId;
	});
}

CSelector exactSpellBonusSelector(SpellID spell)
{
	const BonusSourceID sourceId(spell);
	return CSelector([sourceId](const Bonus * bonus)
	{
		return bonus && bonus->source == BonusSource::SPELL_EFFECT && bonus->sid == sourceId;
	});
}

class ScopedCreatureBonus final
{
	CCreature * creature;
	Bonus bonus;

public:
	ScopedCreatureBonus(CCreature * creature_, Bonus bonus_)
		: creature(creature_),
		  bonus(std::move(bonus_))
	{
		creature->addNewBonus(std::make_shared<Bonus>(bonus));
	}

	~ScopedCreatureBonus()
	{
		creature->removeBonuses(CSelector([this](const Bonus * candidate)
		{
			return candidate
				&& candidate->source == bonus.source
				&& candidate->sid == bonus.sid
				&& candidate->type == bonus.type
				&& candidate->val == bonus.val;
		}));
	}
};

class RankedStackInstance final : public CStackInstance
{
	int rank;

public:
	RankedStackInstance(CreatureID creature, int32_t count, int rank_)
		: CStackInstance(nullptr, creature, count, true),
		  rank(rank_)
	{
		attachToSource(*creature.toCreature());
	}

	int getExpRank() const override
	{
		return rank;
	}
};

class ScopedStackRank final
{
	CStack * stack;
	const CStackInstance * originalBase;
	RankedStackInstance rankedBase;

public:
	ScopedStackRank(CStack * stack_, CreatureID creature, int rank)
		: stack(stack_),
		  originalBase(stack_->base),
		  rankedBase(creature, stack_->unitBaseAmount(), rank)
	{
		stack->base = &rankedBase;
		stack->nodeHasChanged();
	}

	~ScopedStackRank()
	{
		stack->base = originalBase;
		stack->nodeHasChanged();
	}
};
}

class NewHorizonsBattleFormAITest : public BattleTestFixture
{
protected:
	std::shared_ptr<BattleFormEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	CStack * prepareBattleStack()
	{
		startGame();
		startBattle();
		auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(leftHex), 12);
		auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:orc"), BattleHex(rightHex + 5), 10);
		if(!stack || !enemy)
			return nullptr;
		beginCombat();
		environment = std::make_shared<BattleFormEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		return stack;
	}
};

TEST_F(NewHorizonsBattleFormAITest, AuthoritativeAndDetachedViewsReplaceNestedNativeBonusesWithoutLosingBuffs)
{
	auto * stack = prepareBattleStack();
	ASSERT_NE(stack, nullptr);
	const CreatureID original = creatureByName("core:ogre");
	const CreatureID firstForm = creatureByName("core:ogreMage");
	const CreatureID secondForm = creatureByName("core:demon");

	const Bonus rankGated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::STACK_EXPERIENCE, 73, BonusSourceID(firstForm));
	auto rankGatedBonus = rankGated;
	rankGatedBonus.limiter = std::make_shared<RankRangeLimiter>(0);
	ScopedCreatureBonus rankBonus(const_cast<CCreature *>(firstForm.toCreature()), std::move(rankGatedBonus));

	const SpellID speedSpell(SpellID::HASTE);
	const Bonus spellBuff(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 9, BonusSourceID(speedSpell));
	stack->addNewBonus(std::make_shared<Bonus>(spellBuff));

	const auto stackId = stack->unitId();
	const auto owner = stack->unitOwner();
	const auto side = stack->unitSide();
	const auto slot = stack->unitSlot();
	const auto position = stack->getPosition();
	const auto initiative = stack->getInitiative();
	ASSERT_FALSE(stack->getBonuses(creatureNativeSelector(original))->empty());

	stack->beginBattleForm(firstForm, 2);
	EXPECT_EQ(stack->unitType()->getId(), firstForm);
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(original))->empty());
	EXPECT_FALSE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureRankSelector(firstForm))->empty())
		<< "Rank-limited native abilities must be evaluated against the original stack rank";
	EXPECT_FALSE(stack->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

	stack->beginBattleForm(secondForm, 2);
	EXPECT_EQ(stack->unitType()->getId(), secondForm);
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(original))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_FALSE(stack->getBonuses(creatureNativeSelector(secondForm))->empty());
	EXPECT_FALSE(stack->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

	stack->beginBattleForm(original, 2);
	EXPECT_EQ(stack->unitType()->getId(), original);
	EXPECT_FALSE(stack->getBonuses(creatureNativeSelector(original))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(secondForm))->empty());
	EXPECT_FALSE(stack->getBonuses(exactSpellBonusSelector(speedSpell))->empty());
	EXPECT_EQ(stack->unitId(), stackId);
	EXPECT_EQ(stack->unitOwner(), owner);
	EXPECT_EQ(stack->unitSide(), side);
	EXPECT_EQ(stack->unitSlot(), slot);
	EXPECT_EQ(stack->getPosition(), position);
	EXPECT_EQ(stack->getInitiative(), initiative);

	const auto liveNativeBeforeProjection = stack->getBonuses(creatureNativeSelector(original))->size();
	{
		// Give the real stack a synthetic rank context so nested projections prove
		// that effective native RankRangeLimiters still see its original rank.
		ScopedStackRank rankContext(stack, original, 1);
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
		auto projectedFirst = parent->getForUpdate(stackId);
		const auto initialTreeVersion = projectedFirst->getTreeVersion();
		projectedFirst->beginBattleForm(firstForm, 2);
		EXPECT_NE(projectedFirst->getTreeVersion(), initialTreeVersion);
		EXPECT_TRUE(projectedFirst->getBonuses(creatureNativeSelector(original))->empty());
		EXPECT_FALSE(projectedFirst->getBonuses(creatureNativeSelector(firstForm))->empty());
		EXPECT_FALSE(projectedFirst->getBonuses(creatureRankSelector(firstForm))->empty());
		EXPECT_FALSE(projectedFirst->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

		auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto projectedSecond = child->getForUpdate(stackId);
		projectedSecond->beginBattleForm(secondForm, 2);
		EXPECT_TRUE(projectedSecond->getBonuses(creatureNativeSelector(original))->empty());
		EXPECT_TRUE(projectedSecond->getBonuses(creatureNativeSelector(firstForm))->empty());
		EXPECT_TRUE(projectedSecond->getBonuses(creatureRankSelector(firstForm))->empty());
		EXPECT_FALSE(projectedSecond->getBonuses(creatureNativeSelector(secondForm))->empty());
		EXPECT_FALSE(projectedSecond->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

		auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), child);
		auto projectedOriginal = grandchild->getForUpdate(stackId);
		projectedOriginal->beginBattleForm(original, 2);
		EXPECT_FALSE(projectedOriginal->getBonuses(creatureNativeSelector(original))->empty());
		EXPECT_TRUE(projectedOriginal->getBonuses(creatureNativeSelector(firstForm))->empty());
		EXPECT_TRUE(projectedOriginal->getBonuses(creatureRankSelector(firstForm))->empty());
		EXPECT_TRUE(projectedOriginal->getBonuses(creatureNativeSelector(secondForm))->empty());
		EXPECT_FALSE(projectedOriginal->getBonuses(exactSpellBonusSelector(speedSpell))->empty());
		EXPECT_EQ(projectedOriginal->unitId(), stackId);
		EXPECT_EQ(projectedOriginal->unitOwner(), owner);
		EXPECT_EQ(projectedOriginal->unitSide(), side);
		EXPECT_EQ(projectedOriginal->unitSlot(), slot);
		EXPECT_EQ(projectedOriginal->getPosition(), position);
		EXPECT_EQ(projectedOriginal->getInitiative(), initiative);
	}

	EXPECT_EQ(stack->unitType()->getId(), original);
	EXPECT_EQ(stack->getBonuses(creatureNativeSelector(original))->size(), liveNativeBeforeProjection);
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(secondForm))->empty());
	stack->endBattleForm();
}
