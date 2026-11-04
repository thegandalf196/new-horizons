/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroSpellOutcomeValue.h"
#include "../../lib/spells/NewHorizonsBlink.h"
#include "../../lib/constants/EntityIdentifiers.h"
class CBattleInfoCallback;
class DamageCache;
class Environment;
namespace blinkPositionValue
{
float expected(const battle::Unit *, const newHorizonsBlink::Preview &, PlayerColor,
	const CBattleInfoCallback &, const std::shared_ptr<CBattleInfoCallback> &, DamageCache &,
	const Environment *, float *, const heroSpellProjection::OutcomeValue &);
}
