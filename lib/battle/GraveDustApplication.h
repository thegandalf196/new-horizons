/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
namespace battle { class Unit; }
namespace newHorizonsGraveDust
{
/// Group transition only. The caller must establish an eligible primary-shot/body-loss receipt.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan prepareApplication(const battle::Unit & recipient,
	int32_t reductionBasisPoints, PlayerColor sourcePlayer);
}
