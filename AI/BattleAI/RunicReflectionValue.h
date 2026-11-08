/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/Destination.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <vcmi/spells/Magic.h>
#include <memory>
class HypotheticBattle;
namespace spells { class Mechanics; namespace effects { class Effect; } }
namespace battle { class Unit; }
namespace runicReflectionAI
{
struct Weights
{
	float positive = 1;
	float negative = 1;
	float collateral = 1;
	bool healthUnits = false; // Existing damaging creature-cast lane uses raw HP.
};
struct Estimate
{
	float value = 0;
	bool partial = false;
};
/// PRE-cast, nominal recipient-share expectation. No cast, RNG, action search
/// or world clone. One native reflected destination/share, not another area.
Estimate expectedValue(const spells::Mechanics &, const spells::Target &,
	PlayerColor reference, const std::shared_ptr<HypotheticBattle> &, Weights);
/// Existing late-spill kernels supply their already resolved raw share budget.
float expectedRawDamageShare(const spells::Mechanics &, const spells::effects::Effect &,
	const battle::Unit & reflector, int64_t rawDamage, PlayerColor reference,
	const std::shared_ptr<HypotheticBattle> &, Weights);
}
