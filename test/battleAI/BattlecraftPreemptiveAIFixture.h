/*
 * BattlecraftPreemptiveAIFixture.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../server/battles/BattleTestFixture.h"

#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/CStack.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace battlecraftTest
{
constexpr auto battlecraftSkill = "new-horizons:battlecraft";
constexpr auto basicPerk = "new-horizons:battlecraft.entrench";
constexpr auto preemptivePerk = "new-horizons:battlecraft.preEmptiveStrike";

class PreemptiveAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PreemptiveAIEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PreemptiveAICallback final : public CBattleCallback
{
public:
	explicit PreemptiveAICallback(PlayerColor player)
		: CBattleCallback(player, nullptr)
	{}

	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
};

class PreemptiveFixture : public BattleTestFixture
{
protected:
	std::shared_ptr<PreemptiveAIEnvironment> environment;
	std::shared_ptr<PreemptiveAICallback> callback;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const std::string skillId(battlecraftSkill);
		const std::string requestedPerk(perkId);
		const auto rankLookup = [hero](const std::string & id)
		{
			return hero->getPerkSkillRank(id);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId != skillId
					|| offers[choice].selection.perkId != requestedPerk)
					continue;
				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(skillId, requestedPerk));
				return;
			}
		}
		FAIL() << "The active perk was not legally offered: " << requestedPerk;
	}

	void selectPreemptiveStrike(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(battlecraftSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, basicPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, preemptivePerk);
		ASSERT_TRUE(hero->hasActivePerk(battlecraftSkill, preemptivePerk));
	}

	void removeOtherStacks(std::initializer_list<CStack *> kept)
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(!std::ranges::any_of(kept, [unit](const CStack * retained)
				{ return retained && retained->unitId() == unit->unitId(); }))
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	bool defend(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), BattleAction::makeDefend(stack));
	}

	void initializeAI(PlayerColor player)
	{
		callback = std::make_shared<PreemptiveAICallback>(player);
		callback->onBattleStarted(battle());
		environment = std::make_shared<PreemptiveAIEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> simulation() const
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	}

	BattleAction choose(CStack * stack)
	{
		const auto player = battle()->sideToPlayer(stack->unitSide());
		BattleEvaluator evaluator(environment, callback, stack, player, BattleID(0),
			stack->unitSide(), 1.0f, 2);
		return evaluator.selectStackAction(stack);
	}
};

}
