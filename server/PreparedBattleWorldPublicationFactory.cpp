/*
 * PreparedBattleWorldPublicationFactory.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CVCMIServer.h"
#include "CGameHandler.h"
#include "PreparedBattleWorldPublication.h"
#include "../lib/bonuses/NativeBonusGraphAdmission.h"
#include <stdexcept>

std::unique_ptr<PreparedBattleWorldPublication> CVCMIServer::prepareBattleWorldPublication(
	BattleUnitsChanged & packet)
{
	if(!networkHandler || !networkHandler->getContext().get_executor().running_in_this_thread())
		throw std::logic_error("Prepared world publication requires its actual publisher context");
	// Short check retires before state acquisition. Reentrant callback refusal
	// must precede taking a nonrecursive state mutex held by the existing owner.
	// This is not a permit: the real scope rechecks admission after state locking.
	NativeBonusGraphPreparation::checkEntry();
	if(!gh || !gh->gs)
		throw std::invalid_argument("Prepared publication requires its retained authoritative world");
	return std::unique_ptr<PreparedBattleWorldPublication>(new PreparedBattleWorldPublication(*this, packet));
}
