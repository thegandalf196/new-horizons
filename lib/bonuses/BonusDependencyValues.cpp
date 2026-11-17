/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusDependencyCapture.h"
#include "BonusDependencyOverlay.h"
#include <stdexcept>
namespace bonusDependencies
{
std::optional<CapturedModifiers::Materialized> CapturedModifiers::materialize(int count, Change changes,
	const std::vector<Values> * previous) const
{
	if(count < 0 || !canRefresh(changes) || (previous && previous->size() != recipes->size())) return {};
	Materialized result;
	auto unstacked = std::make_shared<BonusList>();
	try
	{
		for(size_t i = 0; i < recipes->size(); ++i)
		{
			const auto & recipe = (*recipes)[i];
			const auto value = previous && !intersects(recipe.dependencies(), changes)
				? (*previous)[i] : recipe.evaluate(count, changes);
			result.scalar.push_back(value);
			const auto type = static_cast<size_t>((*identities)[i].rawType);
			if(type < result.dirty.size() && (!previous || intersects(recipe.dependencies(), changes))) result.dirty[type] = true;
			if(value.decision != ILimiter::EDecision::ACCEPT) continue;
			auto & bonus = result.aliases[(*identities)[i].outputAlias];
			if(!bonus)
			{
				bonus = std::make_shared<Bonus>(*recipe.outputRecord());
				bonus->val = value.value;
				bonus->bonusOwner = value.owner;
			}
			else if(bonus->val != value.value || bonus->bonusOwner != value.owner)
				throw std::invalid_argument("Native output alias changed scalar meaning");
			result.origins.emplace(bonus.get(), OutputOrigin{rawOriginIdentity, (*identities)[i].outputAlias});
			unstacked->push_back(bonus);
		}
		auto stacked = std::make_shared<BonusList>(*unstacked);
		stacked->stackBonuses(); // SAME native reducer, before recipient layers.
		result.stacked = stacked;
		result.unstacked = unstacked;
		for(const auto & overlay : overlays)
		{
			result.stacked = overlay->apply(*result.stacked, result.origins, false);
			result.unstacked = overlay->apply(*result.unstacked, result.origins, true);
		}
		return result;
	}
	catch(const std::exception &) { return {}; }
}
std::optional<NumericFrame> CapturedModifiers::refresh(int currentCount, Change changes,
	const std::array<nativeDamage::PreparedUnit, 2> & base, int warfare, const NumericFrame * previous) const
{
	if(previous && (previous->captureIdentity != captureIdentity || previous->warfare != warfare)) return {};
	auto current = materialize(currentCount, changes, previous ? &previous->scalar : nullptr);
	if(!current) return {};
	NumericFrame result;
	result.captureIdentity = captureIdentity;
	result.warfare = warfare;
	result.scalar = std::move(current->scalar);
	try
	{
		for(size_t shooting = 0; shooting < 2; ++shooting)
		{
			// Base is health/identity only. Every unchanged numerical field survives.
			auto currentBase = previous ? previous->prepared[shooting] : base[shooting];
			const auto & health = base[shooting].unit;
			auto & values = currentBase.unit;
			values.count = currentCount;
			values.firstHealth = health.firstHealth;
			values.maxHealth = health.maxHealth;
			values.phantomIntegrity = health.phantomIntegrity;
			values.baseAmount = health.baseAmount;
			values.availableHealth = health.availableHealth;
			values.shooter = health.shooter;
			values.turret = health.turret;
			values.creatureKey = health.creatureKey;
			auto prepared = nativeDamage::prepareOwnedBonuses(*current->stacked, std::move(currentBase), warfare,
				shooting != 0, previous ? &current->dirty : nullptr);
			if(!prepared) return {};
			result.prepared[shooting] = std::move(*prepared);
		}
		return result;
	}
	catch(const std::exception &) { return {}; }
}
}
