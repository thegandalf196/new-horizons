/*
 * PreparedBattleWorldAccess.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBattleWorldCommitState.h"
#include <stdexcept>

namespace
{
std::unique_ptr<NativeBonusGraphPreparation> takePreparation(
	std::unique_ptr<NativeBonusGraphPreparation> & source)
{
	if(!source || !source->activeOnThisThread())
		throw std::invalid_argument("Prepared world requires the actual active native preparation scope");
	// A wrong/null source is refused without moving or destroying its owner.
	// A valid scope is transferred continuously: no unlock/relock or new phase.
	return std::move(source);
}
}

battle::PreparedBattleWorldCommit::PreparedBattleWorldCommit(std::shared_ptr<CGameState> world,
	std::unique_lock<std::shared_mutex> access, const BattleUnitsChanged & packet)
	: impl(std::make_unique<Data>(std::move(world), std::move(access), packet,
		std::make_unique<NativeBonusGraphPreparation>())) {}

battle::PreparedBattleWorldCommit::PreparedBattleWorldCommit(std::shared_ptr<CGameState> world,
	std::unique_lock<std::shared_mutex> access, const BattleUnitsChanged & packet,
	std::unique_ptr<NativeBonusGraphPreparation> & preparation)
	: impl(std::make_unique<Data>(std::move(world), std::move(access), packet,
		takePreparation(preparation))) {}
