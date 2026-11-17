/*
 * OrdinaryEffectLifetimeAITest.cpp, part of VCMI engine
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
#include "../server/battles/HeroCommandFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/gameState/CGameState.h"
#include <vcmi/Environment.h>
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"

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

class OrdinaryEffectLifetimeEnvironment final : public ::Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit OrdinaryEffectLifetimeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class OrdinaryEffectLifetimeAITest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	std::shared_ptr<OrdinaryEffectLifetimeEnvironment> environment;

	void prepare()
	{
		useRevisedOrders = true;
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_FALSE(battle()->stacks.empty());
		for(const auto & stack : battle()->stacks)
			if(stack->unitSide() == BattleSide::DEFENDER && !stack->hasBonusOfType(BonusType::SIEGE_WEAPON))
			{
				target = stack.get();
				break;
			}
		ASSERT_NE(target, nullptr);
		environment = std::make_shared<OrdinaryEffectLifetimeEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> projection()
	{
		return std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
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

TEST_F(OrdinaryEffectLifetimeAITest, UnlockedPlagueDoesNotUseRoundClockOrApplyATick)
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

TEST_F(OrdinaryEffectLifetimeAITest, InheritedRawAliasesAgeOnceWithoutChangingSourceLocalIdentity)
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
