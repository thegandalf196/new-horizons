/*
 * FrozenAttackStageFixture.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "BattleTestFixture.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class FrozenAttackStageFixture : public BattleTestFixture
{
protected:
	CStack * ice = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		startGame();
		startBattle();
		BattleNextRound round;
		round.battleID = BattleID(0);
		gameHandler->sendAndApply(round);
		ice = addStack(BattleSide::ATTACKER, creatureByName("core:iceElemental"), BattleHex(leftHex), 5);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 500);
		for(const auto * stack : {ice, target})
		{
			SetStackEffect controls;
			controls.battleID = BattleID(0);
			controls.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{
				Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()),
				Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()),
				Bonus(BonusDuration::ONE_BATTLE, BonusType::ALWAYS_MAXIMUM_DAMAGE, BonusSource::OTHER, 1, BonusSourceID())});
			gameHandler->sendAndApply(controls);
		}
	}

	void freezeTarget()
	{
		SetStackEffect frozen;
		frozen.battleID = BattleID(0);
		frozen.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{
			newHorizonsFrozen::makeFrozenMarker(BonusSourceID(ice->creatureId()), battle()->getRound())});
		gameHandler->sendAndApply(frozen);
	}

	BattleStackAttacked pendingHit(uint32_t sourceID, uint32_t flags = 0) const
	{
		BattleStackAttacked hit;
		hit.attackerID = sourceID;
		hit.stackAttacked = target->unitId();
		hit.flags = flags;
		return hit;
	}
};
