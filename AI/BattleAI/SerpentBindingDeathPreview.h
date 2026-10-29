/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
class HypotheticBattle;
class CBattleInfoCallback;
class AttackPossibility;
namespace battle { class CUnitState; }
namespace serpentBindingAI
{
/// Action-local actual-death receipt; source markers remain the lifetime authority.
void observeDeath(bool mayHaveBindings, const std::shared_ptr<HypotheticBattle> & preview,
	const battle::CUnitState & afterDamage, bool wasAlive, std::vector<uint32_t> & deferredSources);
/// Called only after this whole candidate's existing unit/score/RNG work is complete.
void completeDeathPreview(const std::shared_ptr<CBattleInfoCallback> & parent,
	const HypotheticBattle & model, AttackPossibility & candidate, const std::vector<uint32_t> & deferredSources,
	const std::shared_ptr<HypotheticBattle> & scratchPreview);
}
