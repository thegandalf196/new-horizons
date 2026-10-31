/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "GraveDustEffectChanges.h"
class BattleInfo;
class CStack;
namespace newHorizonsGraveDust
{
/// Native-authoritative synchronous entry. No player/projected graph import.
DLL_LINKAGE void validateNativeMutation(const BattleInfo & battle, const CStack & unit,
	const std::vector<Bonus> & records, EffectOperation operation);
DLL_LINKAGE void validateNativeEffectChanges(const BattleInfo & battle, const SetStackEffect & effects);
}
