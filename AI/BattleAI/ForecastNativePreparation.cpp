/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativePreparation.h"
#include "HypotheticBattle.h"
#include <type_traits>
namespace forecast
{
struct NativePreparation::Saved
{
	decltype(HypotheticBattle::stackStates) units;
	decltype(HypotheticBattle::lifeDrainFriendlyObservation) observation;
	explicit Saved(const HypotheticBattle & branch)
		: units(branch.stackStates), observation(branch.lifeDrainFriendlyObservation) {}
};
NativePreparation::NativePreparation(HypotheticBattle & branch)
	: branch(branch), saved(std::make_unique<Saved>(branch)) {}
NativePreparation::~NativePreparation() noexcept
{
	if(accepted) return;
	static_assert(noexcept(branch.stackStates.swap(saved->units)));
	static_assert(noexcept(branch.lifeDrainFriendlyObservation.swap(saved->observation)));
	branch.stackStates.swap(saved->units);
	branch.lifeDrainFriendlyObservation.swap(saved->observation);
}
std::shared_ptr<StackWithBonuses> NativePreparation::unit(uint32_t id)
{
	return branch.getForUpdateImpl(id); // Never invalidates the observation.
}
void NativePreparation::accept(std::span<const uint32_t> affected) noexcept
{
	for(const auto id : affected) branch.invalidateLifeDrainFriendlyObservation(id);
	accepted = true;
}
}
