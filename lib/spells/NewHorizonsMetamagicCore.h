/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <functional>
#include <optional>
#include <cstdint>
class CSpell;
namespace spells { class Mechanics; }
namespace battle { class Unit; }
namespace newHorizonsMetamagic
{
/// Geometric classification, independent of how many creatures happen to be hit.
DLL_LINKAGE std::optional<bool> singleRecipientEffectPayload(const spells::Mechanics &);
DLL_LINKAGE bool singleTargetSpell(const spells::Mechanics & mechanics);
DLL_LINKAGE bool empoweredDamage(const spells::Mechanics & mechanics);
DLL_LINKAGE int debuffRetryChance(const spells::Mechanics & mechanics);
/// Called only after a genuine normal resist, never after immunity rejection.
/// Returns final resistance. At most one chance draw and one ordinary check.
DLL_LINKAGE bool retryResistance(bool resisted, int retryPercent, int resistancePercent,
	const std::function<int()> & sample);
DLL_LINKAGE bool explicitlyIrresistible(const spells::Mechanics &);
DLL_LINKAGE bool hasResistibleCreatureAffliction(const spells::Mechanics &);
DLL_LINKAGE int normalResistanceBasisPoints(const spells::Mechanics &, const battle::Unit *);
DLL_LINKAGE bool retryResistanceBasisPoints(bool resisted, int retryPercent, int resistanceBP,
	const std::function<int(int)> & sample);
DLL_LINKAGE double resistanceFailureProbabilityBasisPoints(int resistanceBP, int retryPercent, double initialFailureProbability);
DLL_LINKAGE int normalResistancePercent(const spells::Mechanics &, const battle::Unit *);
DLL_LINKAGE int64_t retainedFriendlyDamage(int64_t damage, int retainedPercent);
DLL_LINKAGE double resistanceFailureProbability(int resistancePercent, int retryPercent);
DLL_LINKAGE double resistanceFailureProbability(int resistancePercent, int retryPercent, double initialFailureProbability);
DLL_LINKAGE double effectiveResistanceFailureProbability(const spells::Mechanics &, const battle::Unit *);
/// Captured owner scalar for own area/chain/global spells and persistent fields.
DLL_LINKAGE int friendlyDamagePercent(const spells::Mechanics & mechanics);
DLL_LINKAGE int recipientDamagePercent(const spells::Mechanics & mechanics, const battle::Unit * recipient);
}
