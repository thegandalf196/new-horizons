/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include <algorithm>
#include <stdexcept>
namespace forecast
{
namespace
{
void require(bool value)
{
	if(!value) throw std::logic_error("Admitted compact health/effect transaction rejected");
}
void require(const Result & result)
{
	if(result.refusal == Refusal::Exhausted) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	require(static_cast<bool>(result));
}
forecastEffects::Receipt cleanup(const State & state, UnitId id, bool damaged, bool terminal)
{
	const auto before = state.unit(id)->effects;
	require(before != nullptr);
	forecastEffects::Receipt combined{before, before, before->revision()};
	const auto append = [&](forecastEffects::Result result)
	{
		require(static_cast<bool>(result));
		combined.after = result.receipt->after;
		combined.dirty = combined.dirty | result.receipt->dirty;
		for(const auto & change : result.receipt->changes)
		{
			const auto found = std::ranges::find_if(combined.changes, [&](const auto & earlier) { return earlier.id == change.id; });
			if(found == combined.changes.end()) combined.changes.push_back(change);
			else found->after = change.after;
		}
	};
	// Native injury packet uses negative resolved healthDelta, not spell type.
	if(damaged) append(forecastEffects::Transitions::prepareStrike(combined.after,
		battle::timedEffects::StrikeClock::BeingDamaged));
	if(terminal) append(forecastEffects::Transitions::prepareRemoval(combined.after,
		forecastEffects::Removal::DeathOrResurrection, false));
	return combined;
}
void importHealth(const State & draft, UnitId id, StackWithBonuses & staged)
{
	// The detached native ledger has already performed damage/healing ONCE.
	// Never re-enter damageInternal's provider/cohesion/Bloodrage scans here.
	auto values = draft.unit(id)->health;
	staged.editHealth()->swapPreparedContents(values);
	staged.veteranPhysicalDamageSinceActivation = draft.unit(id)->veteranPhysicalDamage;
}
}
bool NativeAdapter::damage(HypotheticBattle & branch, battle::CUnitState & target,
	int64_t & amount, battle::DamageProvenance provenance, int64_t * postShield)
{
	if(!branch.compactForecast) return false;
	auto * isolated = dynamic_cast<StackWithBonuses *>(&target);
	require(isolated && branch.battleGetUnitByID(target.unitId()) == isolated);
	auto draft = *branch.compactForecast;
	const auto before = draft.unit(target.unitId())->health.available();
	const auto beforeShields = amount;
	const auto result = Transitions::applyDamage(draft, target.unitId(), amount, provenance, false, branch.searchBudget().get());
	require(result);
	const auto changedId = target.unitId();
	stagePureReadValue(draft, branch, changedId);
	refreshReadViews(draft, branch, std::span(&changedId, 1), false);
	if((result.receipt->beforeCount == 0) != (result.receipt->afterCount == 0))
		draft = prepareMembershipGeometry(branch, std::move(draft));
	const auto receipt = cleanup(draft, target.unitId(), result.receipt->appliedHealth > 0,
		before > 0 && draft.unit(target.unitId())->health.available() == 0);
	if(receipt.changes.empty())
	{
		// Ordinary no-effect hit retains the original single detached-ledger path.
		require(publish(draft, target.unitId(), branch, *isolated));
		branch.compactForecast = std::move(draft);
	}
	else
	{
		auto staged = prepareEffectDraft(*isolated);
		importHealth(draft, target.unitId(), *staged);
		require(stageEffects(draft, branch, target.unitId(), receipt, *staged));
		refreshReadViews(draft, branch, std::span(&changedId, 1), false);
		publishEffects(branch, std::move(draft), *isolated, *staged);
	}
	amount = result.receipt->appliedHealth;
	if(postShield) *postShield = beforeShields;
	return true;
}
std::optional<battle::HealInfo> NativeAdapter::heal(HypotheticBattle & branch, battle::CUnitState & target,
	int64_t & amount, EHealLevel level, EHealPower power)
{
	if(!branch.compactForecast) return {};
	auto * isolated = dynamic_cast<StackWithBonuses *>(&target);
	require(isolated && branch.battleGetUnitByID(target.unitId()) == isolated);
	auto draft = *branch.compactForecast;
	const bool dead = draft.unit(target.unitId())->health.available() == 0;
	const auto result = Transitions::applyHealing(draft, target.unitId(), amount, level, power, branch.searchBudget().get());
	require(result);
	const auto changedId = target.unitId();
	stagePureReadValue(draft, branch, changedId);
	refreshReadViews(draft, branch, std::span(&changedId, 1), false);
	if((result.receipt->beforeCount == 0) != (result.receipt->afterCount == 0))
		draft = prepareMembershipGeometry(branch, std::move(draft));
	const auto receipt = cleanup(draft, target.unitId(), false,
		dead && draft.unit(target.unitId())->health.available() > 0);
	if(receipt.changes.empty())
	{
		require(publish(draft, target.unitId(), branch, *isolated));
		branch.compactForecast = std::move(draft);
	}
	else
	{
		auto staged = prepareEffectDraft(*isolated);
		importHealth(draft, target.unitId(), *staged);
		require(stageEffects(draft, branch, target.unitId(), receipt, *staged));
		refreshReadViews(draft, branch, std::span(&changedId, 1), false);
		publishEffects(branch, std::move(draft), *isolated, *staged);
	}
	amount = result.receipt->appliedHealth;
	return result.receipt->healing;
}
}
