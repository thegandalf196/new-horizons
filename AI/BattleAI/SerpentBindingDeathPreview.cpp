/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "SerpentBindingDeathPreview.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
namespace serpentBindingAI
{
void observeDeath(bool mayHaveBindings, const std::shared_ptr<HypotheticBattle> & preview,
	const battle::CUnitState & afterDamage, bool wasAlive, std::vector<uint32_t> & deferredSources)
{
	if(!mayHaveBindings || !wasAlive || afterDamage.alive())
		return;
	if(!vstd::contains(deferredSources, afterDamage.unitId()))
		deferredSources.push_back(afterDamage.unitId());
	if(afterDamage.cloneID >= 0 && !vstd::contains(deferredSources, static_cast<uint32_t>(afterDamage.cloneID)))
		deferredSources.push_back(static_cast<uint32_t>(afterDamage.cloneID));
	if(preview)
		preview->releaseSerpentBindingsForDeath(afterDamage);
}
void completeDeathPreview(const std::shared_ptr<CBattleInfoCallback> & parent,
	const HypotheticBattle & model, AttackPossibility & candidate, const std::vector<uint32_t> & deferredSources,
	const std::shared_ptr<HypotheticBattle> & scratchPreview)
{
	if(candidate.effectPreview || deferredSources.empty() || !candidate.attackerState)
		return;
	auto preview = scratchPreview ? scratchPreview : std::make_shared<HypotheticBattle>(model.env, parent);
	// Match the existing selected collection order: recipients, then actor last.
	// No damage, score, Fortune, RNG, Hero resource or cache operation is replayed.
	for(const auto & unit : candidate.affectedUnits)
		preview->updateUnit(unit->unitId(), unit->save(), 0);
	preview->updateUnit(candidate.attackerState->unitId(), candidate.attackerState->save(), 0);
	for(const auto source : deferredSources)
		preview->releaseSerpentBindings(source);
	candidate.effectPreview = std::move(preview);
}
}
