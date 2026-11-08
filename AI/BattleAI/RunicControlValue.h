/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "RunicReflectionValue.h"
class StackWithBonuses;
namespace runicReflectionAI
{
/// Existing intrinsic forced-action pool, nominal hits only. No AP/reactions,
/// world creation or continuation. Original ordinary pressure is removed once.
float berserkValue(const battle::Unit &, StackWithBonuses &, PlayerColor,
	const std::shared_ptr<HypotheticBattle> &, Weights);
}
