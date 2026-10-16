/*
 * HeroQueueSelectorFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace heroQueueTest
{
template<typename T> std::string wire(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	for(const auto byte : memory.extractBuffer())
	{
		const auto number = std::to_integer<unsigned int>(byte);
		result.push_back(digits[number >> 4]);
		result.push_back(digits[number & 15]);
	}
	return result;
}

class QueueEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit QueueEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override;
	const BattleCb * battle(const BattleID & id) const override;
	const GameCb * game() const override;
};

class QueueCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> heroRequests, unitRequests;
	QueueCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { heroRequests.push_back(action); }
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { unitRequests.push_back(action); }
};

class HeroQueueSelectorTest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * actor = nullptr;
	CStack * waiter = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<QueueEnvironment> environment;
	std::shared_ptr<QueueCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> live;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;

	void mapLoaded(CMap * map) override;
	void world();
	void selectPerk(CGHeroInstance * hero, const std::string & skill,
		const std::string & perk, MasteryLevel::Type rank);
	void book(CGHeroInstance * hero, SpellID spell, const std::string & skill);
	void arena();
	void activate(const CStack * unit);
	void effect(const CStack * unit, const Bonus & bonus);
	void freeze(const CStack * unit);
	void injure(CStack * unit, int64_t remainingHealth);
	void spendEnemyAllowance();
	bool cast(CGHeroInstance * hero, SpellID spell, const CStack * target);
	void infectedForfeit(bool lethal);
	std::vector<std::vector<uint32_t>> queue() const;
	void expectBeforeEnemy(const CStack * unit);
	void characterize(bool requireSubmittedRequest);
	std::string state(const CBattleInfoCallback & view) const;
	std::string observation();
};
}
